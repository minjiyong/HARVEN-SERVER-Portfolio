"""교환 · 주기 · 거래: 실제 TCP 패킷, DB 샤드, 응답 유실, 확정 중 종료, 재시작 복구.
python3 Tools/Test/regress_transfer.py --server build/transfer-memory/GameServer
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
from regress_craft import Bot as BaseBot, wait_port, wait_login, REPO

REQ, ACK, STATE, ITEM = 114, 115, 116, 117
REQUEST, ACCEPT, SET_ITEM, SET_PRICE, READY, CONFIRM, CANCEL = range(7)
EXCHANGE, GIFT, SALE = range(3)
OK, INVALID, BUSY, DUPLICATE, TOO_FAR, STALE, BAD_ITEM, BOUND, EQUIPPED, BAD_COUNT, BAG_FULL, LACK_ADENA, WALLET_FULL, NOT_READY = range(14)

class Bot(BaseBot):
    def __init__(self, port, name):
        self.transfer = None
        self.states, self.acks, self.items = [], [], {}
        self.tseq = 0
        self.levels = {}
        super().__init__(port, name)
        self.poll(.3)
    def on(self, p):
        super().on(p)
        if p[2] == 99:
            slot, level = struct.unpack_from('<HI',p,3)
            self.levels[slot] = level
        if p[2] == ACK:
            op, result, seq, tid, rev = struct.unpack_from('<BBIQI', p, 3)
            self.acks.append((seq, result, tid, rev))
        elif p[2] == STATE:
            tid, rev, kind, phase, result, side = struct.unpack_from('<QIBBBB', p, 3)
            price, ms, ready, confirmed, n0, n1 = struct.unpack_from('<IIBBBB', p, 55)
            self.transfer = dict(id=tid, rev=rev, kind=kind, phase=phase, result=result, side=side,
                                 price=price, ready=ready, confirmed=confirmed, counts=(n0,n1))
            self.states.append(self.transfer.copy())
            self.items.clear()
        elif p[2] == ITEM:
            tid, rev, side, index, slot, item, count, ench, extra = struct.unpack_from('<QIBBHHHIQ', p, 3)
            self.items[(side,index)] = (slot,item,count,ench,extra)
    def request(self, op, kind=None, peer=None, slot=0, count=0, price=0, rev=None, seq=None, name=''):
        self.tseq += 1
        seq = self.tseq if seq is None else seq
        t = self.transfer or dict(id=0, rev=0, kind=0)
        kind = t['kind'] if kind is None else kind
        tid = 0 if op == REQUEST else t['id']
        revision = 0 if op == REQUEST else t['rev'] if rev is None else rev
        self.send(struct.pack('<HBBBIQI16sHHI20s', 65, REQ, op, kind, seq, tid, revision,
                              peer.me if peer else bytes(16), slot, count, price, name.encode()))
        deadline = time.monotonic()+6
        while time.monotonic()<deadline:
            self.poll(.02)
            for ack in self.acks:
                if ack[0] == seq:
                    self.acks.remove(ack)
                    return ack[1]
        raise AssertionError(f'no ack {op=} {seq=}')

def pump(*bots, duration=.12):
    end = time.monotonic()+duration
    while time.monotonic()<end:
        for b in bots:
            if not b.closed: b.poll(.005)

def start(a,b,kind,by_name=False):
    assert a.request(REQUEST,kind,peer=None if by_name else b,name=b.name if by_name else '')==OK
    pump(a,b)
    assert b.request(ACCEPT)==OK
    pump(a,b)

def item(a,b,slot,count):
    result=a.request(SET_ITEM,slot=slot,count=count)
    assert result==OK,(a.name,slot,count,result,a.transfer,a.slots)
    pump(a,b)

def finish(a,b):
    assert a.request(READY)==OK; pump(a,b)
    assert b.request(READY)==OK; pump(a,b)
    if a.transfer['kind']==SALE:
        assert a.request(CONFIRM)==NOT_READY
        assert b.request(CONFIRM)==OK; pump(a,b)
        assert a.request(CONFIRM)==OK
    else:
        assert a.request(CONFIRM)==OK; pump(a,b)
        assert b.request(CONFIRM)==OK
    pump(a,b,duration=.9)

def total(b,itemid): return sum(n for item,n,_ in b.slots.values() if item==itemid)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--server',default=str(REPO/'build/transfer-memory/GameServer'))
    ap.add_argument('--port',type=int,default=14301)
    ap.add_argument('--keep',action='store_true')
    args=ap.parse_args()
    work=Path(tempfile.mkdtemp(prefix='harven-transfer-'))
    shutil.copytree(REPO/'Bin/GameData',work/'GameData')
    log=(work/'server.log').open('w')
    env=dict(os.environ,HARVEN_MEMDB_JOURNAL=str(work/'journal'),HARVEN_DB_TRANSFER_DELAY_MS='250',HARVEN_DB_TRANSFER_LOST_ACK='1',HARVEN_TRANSFER_INVITE_MS='2000',HARVEN_TRANSFER_EDIT_MS='6000')
    seeds=[]
    for pid,name,wallet in [(101,'transfer_gear',100),(102,'transfer_bound',100),(103,'transfer_cap',2000000000),(104,'transfer_many',100)]:
        seeds.append(f'P|{name.encode().hex()}|{b"pw".hex()}|{pid}|766|758|20|0|{wallet}\n')
        for slot in range(9 if pid==104 else 1):
            seeds.append(f'S|{pid}|{slot}|1|3|1|0|300|{1280 if pid==102 else 1024}\n')
    (work/'journal').write_text(''.join(seeds))
    bots=[]; server=None
    def run():
        p=subprocess.Popen([str(Path(args.server).resolve()),'--port',str(args.port),'--data-dir',str(work/'GameData'),'--dev'],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)
        assert wait_port(args.port,90),'server startup failed'
        assert wait_login(args.port,120),'server not accepting logins'
        return p
    def bot(suffix,full=False,poor=False,x=766):
        marker='CF' if full else 'CP' if poor else 'CR'
        b=Bot(args.port,f'lv20_xy{x}_758_{marker}{suffix}')
        assert b.pos==(x,758),b.pos
        bots.append(b); return b
    try:
        server=run()
        a,b=bot('a'),bot('b')
        start(a,b,EXCHANGE)
        assert b.request(SET_ITEM,slot=7,count=1)==EQUIPPED
        assert a.request(SET_ITEM,slot=0,count=101)==BAD_COUNT
        assert b.request(SET_ITEM,slot=2,count=3)==BOUND
        item(a,b,0,7); item(b,a,3,3)
        assert a.request(READY)==OK; pump(a,b)
        assert b.request(READY)==OK; pump(a,b)
        old=a.transfer['rev']
        item(a,b,0,8)
        assert a.transfer['ready']==0 and b.transfer['ready']==0
        assert b.request(CONFIRM,rev=old)==STALE
        assert b.request(CONFIRM)==NOT_READY
        a0,b0=total(a,61),total(b,61)
        shard_a,shard_b=total(a,64),total(b,64)
        finish(a,b)
        assert a.transfer['phase']==4 and b.transfer['phase']==4
        assert (total(a,61),total(b,61))==(a0-8,b0+8)
        assert (total(a,64),total(b,64))==(shard_a+3,shard_b-3)
        assert a.request(CONFIRM,seq=a.tseq)==DUPLICATE
        print('PASS exchange / partial stacks / changed consent / stale and duplicate packets / lost commit response',flush=True)
        a,b=bot('c'),bot('d')
        start(a,b,GIFT,by_name=True)
        assert b.request(SET_ITEM,slot=0,count=1)==INVALID
        assert a.request(SET_PRICE,price=100)==INVALID
        item(a,b,5,1)
        n=total(b,3)
        finish(a,b)
        assert total(b,3)==n+1 and 5 not in a.slots
        assert any((b.extra.get(s,0)&512) for s,v in b.slots.items() if v[0]==3)
        print('PASS gift / recipient consent / name request / equipment transfer history',flush=True)
        a,b=bot('e'),bot('f')
        start(a,b,SALE)
        item(a,b,5,1)
        assert b.request(SET_PRICE,price=100)==INVALID
        assert a.request(SET_PRICE,price=0)==INVALID
        assert a.request(SET_PRICE,price=1200)==OK; pump(a,b)
        wallets=a.adena,b.adena
        finish(a,b)
        assert (a.adena,b.adena)==(wallets[0]+1200,wallets[1]-1200)
        print('PASS sale / fixed roles / price / buyer before seller / conserved money',flush=True)
        a,b=bot('g',full=True),bot('h',full=True)
        start(a,b,EXCHANGE); item(a,b,5,1); item(b,a,6,1)
        finish(a,b)
        assert a.transfer['phase']==4 and b.transfer['phase']==4 and len(a.slots)==50 and len(b.slots)==50
        print('PASS full bags exchange using outgoing space',flush=True)
        a,b=bot('i'),bot('j',full=True)
        start(a,b,GIFT); item(a,b,5,1)
        a_snap,b_snap=a.slots.copy(),b.slots.copy()
        assert a.request(READY)==OK; pump(a,b)
        assert b.request(READY)==OK; pump(a,b)
        assert a.request(CONFIRM)==OK; pump(a,b)
        assert b.request(CONFIRM)==BAG_FULL
        pump(a,b)
        assert a.slots==a_snap and b.slots==b_snap
        assert a.request(CANCEL)==OK; pump(a,b)
        print('PASS gift bag full / unchanged inventories / cancellation unlock',flush=True)
        a,b=bot('k'),bot('l',poor=True)
        start(a,b,SALE); item(a,b,5,1)
        assert a.request(SET_PRICE,price=100)==OK; pump(a,b)
        assert a.request(READY)==OK; pump(a,b)
        assert b.request(READY)==OK; pump(a,b)
        assert b.request(CONFIRM)==OK; pump(a,b)
        assert a.request(CONFIRM)==LACK_ADENA
        assert a.request(CANCEL)==OK; pump(a,b)
        print('PASS insufficient money / no partial transfer',flush=True)
        a=Bot(args.port,'transfer_gear');bots.append(a);b=bot('gear')
        start(a,b,GIFT);item(a,b,0,1)
        assert a.items[(0,0)][3:]==(300,1024)
        finish(a,b)
        assert any(v[0]==3 and b.levels.get(s)==300 and b.extra.get(s)==1536 for s,v in b.slots.items())
        print('PASS enchant above 255 / exact gear options preserved',flush=True)
        a=Bot(args.port,'transfer_bound');bots.append(a);b=bot('bind')
        start(a,b,GIFT)
        assert a.request(SET_ITEM,slot=0,count=1)==BOUND
        assert a.request(CANCEL)==OK;pump(a,b)
        print('PASS instance binding restriction',flush=True)
        a=Bot(args.port,'transfer_cap');bots.append(a);b=bot('cap')
        start(a,b,SALE);item(a,b,0,1)
        assert a.request(SET_PRICE,price=1)==OK;pump(a,b)
        assert a.request(READY)==OK;pump(a,b)
        assert b.request(READY)==OK;pump(a,b)
        assert b.request(CONFIRM)==OK;pump(a,b)
        assert a.request(CONFIRM)==WALLET_FULL
        assert a.adena==2000000000 and 0 in a.slots
        assert a.request(CANCEL)==OK;pump(a,b)
        print('PASS seller wallet cap / money and items unchanged',flush=True)
        a=Bot(args.port,'transfer_many');bots.append(a);b=bot('many')
        start(a,b,GIFT)
        for slot in range(8):item(a,b,slot,1)
        assert a.request(SET_ITEM,slot=8,count=1)==BAD_COUNT
        assert a.request(CANCEL)==OK;pump(a,b)
        print('PASS eight offered slot limit',flush=True)
        a,b,c=bot('qa'),bot('qb'),bot('qc')
        assert a.request(REQUEST,GIFT,peer=b)==OK;pump(a,b)
        assert c.request(REQUEST,GIFT,peer=b)==BUSY
        pump(a,b,duration=2.3)
        assert a.transfer['phase']==5 and b.transfer['result']==15
        print('PASS occupied player / unanswered invitation timeout',flush=True)
        start(a,b,GIFT);item(a,b,5,1);a.close();pump(b,duration=.4)
        assert b.transfer['phase']==5 and b.transfer['result']==16
        print('PASS disconnect before commit / cancellation',flush=True)
        a,b=bot('m'),bot('n',x=773)
        assert a.request(REQUEST,GIFT,peer=b)==TOO_FAR
        print('PASS distance guard',flush=True)
        a,b=bot('o'),bot('p')
        start(a,b,GIFT); item(a,b,5,1)
        assert a.request(READY)==OK; pump(a,b)
        assert b.request(READY)==OK; pump(a,b)
        assert a.request(CONFIRM)==OK; pump(a,b)
        assert b.request(CONFIRM)==OK
        # 전송중 이동/버림은 거부되고 계정은 커밋 후에만 재로그인 가능하다.
        a.send(struct.pack('<HBHH',7,8,5,1))
        # 받는 사람은 커밋 커넥션과 다른 DB 샤드다. 즉시 재접속해도 예전 가방을 읽으면 안 된다.
        b.close()
        b2=Bot(args.port,b.name); bots.append(b2)
        pump(a,duration=.3)
        assert 5 not in a.slots and total(b2,3)==2
        print('PASS disconnect during commit / held account / relogin reads committed result',flush=True)
        expected={a.name:(a.slots.copy(),a.adena),b2.name:(b2.slots.copy(),b2.adena)}
        for bot_ in bots: bot_.close()
        time.sleep(.5)
        server.terminate(); server.wait(10)
        server=run()
        for name,(slots,adena) in expected.items():
            c=Bot(args.port,name); bots.append(c)
            assert c.slots==slots and c.adena==adena
        print('PASS server restart / single journal commit / inventory and wallet recovery',flush=True)
        return 0
    finally:
        for b in bots: b.close()
        if server and server.poll() is None:
            server.terminate()
            try: server.wait(10)
            except subprocess.TimeoutExpired: server.kill(); server.wait()
        log.close()
        print('QA files:',work,flush=True)
        if not args.keep: shutil.rmtree(work)

if __name__=='__main__': raise SystemExit(main())
