"""Friends / reciprocal block / party and trade guards / reports on an isolated memory DB server."""
from pathlib import Path
import argparse,json,os,struct,subprocess,tempfile,time
from regress_transfer import Bot as TransferBot,pump
from regress_craft import REPO,wait_port

class Bot(TransferBot):
    def __init__(self,port,name):
        self.social_ack=[]; self.social_rows=[]; self.party_ack=[]; self.invites=[]
        self.sseq=0; self.pseq=0; self.party_id=0
        super().__init__(port,name)
    def on(self,p):
        super().on(p)
        if p[2]==119: self.social_ack.append(struct.unpack_from('<BBIQ',p,3))
        elif p[2]==120:
            if p[7]==0: self.social_rows=[]
            elif 1<=p[7]<=4: self.social_rows.append((p[7],p[9:29].split(b'\0')[0].decode()))
        elif p[2]==105: self.party_ack.append(struct.unpack_from('<BBII',p,3))
        elif p[2]==106 and p[3]==0: self.party_id=struct.unpack_from('<I',p,4)[0]
        elif p[2]==107 and p[3]==0: self.invites.append(struct.unpack_from('<I',p,7)[0])
    def social(self,op,target='',reason=0,note=''):
        time.sleep(.32); self.sseq+=1
        self.send(struct.pack('<HBBIB20s128s',157,118,op,self.sseq,reason,target.encode(),note.encode()))
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            self.poll(.04)
            for ack in self.social_ack:
                if ack[2]==self.sseq: self.social_ack.remove(ack); self.poll(.1); return ack
        raise AssertionError('social ACK timeout')
    def party(self,op,target='',arg8=0,arg32=0):
        time.sleep(.35); self.pseq+=1
        self.send(struct.pack('<HBBIBI16sBBBB20s46s',99,104,op,self.pseq,arg8,arg32,bytes(16),0,1,20,3,target.encode(),b'block QA'))
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            self.poll(.04)
            for ack in self.party_ack:
                if ack[2]==self.pseq: self.party_ack.remove(ack); self.poll(.1); return ack
        raise AssertionError('party ACK timeout')
    def chat_send(self,text,target='',channel=0):
        self.send(struct.pack('<HBB20s128s',152,88,6 if target else channel,target.encode(),text.encode()))

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--server',default=str(REPO/'build/social-memory/GameServer'))
    ap.add_argument('--port',type=int,default=14321); ap.add_argument('--result',default='')
    args=ap.parse_args(); work=Path(tempfile.mkdtemp(prefix='harven-social-e2e-')); bots=[]; server=None
    log=(work/'server.log').open('w')
    env=dict(os.environ,HARVEN_MEMDB_JOURNAL=str(work/'journal'),HARVEN_SOCIAL_STORE=str(work/'social/state.json'),HARVEN_ENV_FILE=str(work/'no-env'))
    def run():
        p=subprocess.Popen([str(Path(args.server).resolve()),'--port',str(args.port),'--data-dir',str(REPO/'Bin/GameData'),'--dev'],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT)
        assert wait_port(args.port,90),'server startup'; return p
    def bot(name):
        b=Bot(args.port,name); bots.append(b); return b
    try:
        server=run(); a=bot('lv20_xy766_758_CRa'); b=bot('lv20_xy766_758_CRb'); c=bot('lv20_xy766_758_CRc')
        assert a.social(1,b.name)[1]==0; pump(a,b)
        assert (3,a.name) in b.social_rows
        assert b.social(2,a.name)[1]==0; pump(a,b)
        assert (1,b.name) in a.social_rows and (1,a.name) in b.social_rows
        print('PASS mutual friend request and accept')
        assert a.party(2,b.name)[1]==0; pump(a,b)
        assert b.invites
        assert b.party(3,arg8=1,arg32=b.invites[-1])[1]==0; pump(a,b)
        assert a.party_id!=0 and a.party_id==b.party_id
        assert a.request(0,1,peer=b)==0; pump(a,b)
        assert a.social(5,b.name)[1]==0; pump(a,b,c,duration=.4)
        assert a.party_id==0 and b.party_id!=0
        assert a.transfer['phase']==5 and b.transfer['phase']==5
        assert (2,b.name) in a.social_rows and (1,a.name) not in b.social_rows
        print('PASS block removes friendship, leaves shared party and cancels active transfer')
        assert b.social(1,a.name)[1]==3
        assert b.party(2,a.name)[1]!=0
        for kind in range(3):
            time.sleep(2.1); assert b.request(0,kind,peer=a)!=0
        oldA=len(a.chat); oldB=len(b.chat)
        b.chat_send('blocked whisper',a.name); a.chat_send('blocked reverse',b.name)
        pump(a,b,duration=.4)
        assert not any(name==b.name for name,_ in a.chat[oldA:])
        assert not any(name==a.name for name,_ in b.chat[oldB:])
        b.chat_send('blocked normal'); pump(a,b,duration=.3)
        b.chat_send('blocked world',channel=1); pump(a,b,duration=.3)
        b.chat_send('blocked trade',channel=2); pump(a,b,duration=.3)
        assert not any(name==b.name for name,_ in a.chat[oldA:])
        assert c.social(1,a.name)[1]==0
        assert b.party(2,c.name)[1]==0; pump(b,c)
        assert c.party(3,arg8=1,arg32=c.invites[-1])[1]==0; pump(b,c)
        assert b.party(8,arg8=1)[1]==0
        assert c.party(2,a.name)[1]!=0
        post=b.party(12,arg8=1); assert post[1]==0,post
        assert a.party(14,arg32=post[3])[1]!=0
        print('PASS reciprocal whisper/public-chat/trade/party guards; unrelated player unaffected')
        ack=a.social(7,b.name,0,'QA report'); assert ack[1]==0 and ack[3]>0
        assert a.social(7,b.name)[1]==5
        reports=[json.loads(x) for x in (work/'social/state.json.reports.jsonl').read_text().splitlines()]
        assert reports[-1]['receipt']==ack[3] and reports[-1]['reporter_note']=='QA report'
        print('PASS server report receipt, audit record and cooldown')
        for who in bots:who.close()
        bots.clear(); server.terminate(); server.wait(timeout=10); server=run()
        a=bot('lv20_xy766_758_CRa'); b=bot('lv20_xy766_758_CRb')
        assert (2,b.name) in a.social_rows and b.social(1,a.name)[1]==3
        assert a.social(6,b.name)[1]==0; pump(a,b)
        assert b.social(1,a.name)[1]==0
        print('PASS server restart retains blocks; unblock restores requests')
        result={'passed':True,'receipt':ack[3],'work':str(work)}
        if args.result:Path(args.result).write_text(json.dumps(result))
        print('PASS social TCP end-to-end; isolated data at',work)
    finally:
        for who in bots:who.close()
        if server and server.poll() is None:server.terminate(); server.wait(timeout=10)
        log.close()
if __name__=='__main__':main()
