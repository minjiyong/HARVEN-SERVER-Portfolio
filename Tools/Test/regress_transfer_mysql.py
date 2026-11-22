"""Isolated local MySQL transaction checks (never connects to the production DB).
python3 Tools/Test/regress_transfer_mysql.py --runtime build/enchant-mysql/runtime --libs build/enchant-mysql/deps/usr/lib/x86_64-linux-gnu
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--runtime',required=True,type=Path)
    ap.add_argument('--libs',required=True,type=Path)
    args=ap.parse_args()
    repo=Path(__file__).resolve().parents[2]
    runtime=args.runtime.resolve()
    work=Path(tempfile.mkdtemp(prefix='harven-transfer-mysql-'))
    env=os.environ.copy();env['LD_LIBRARY_PATH']=str(args.libs.resolve())
    options=['--no-defaults','--basedir='+str(runtime),'--datadir='+str(work/'data'),'--user='+os.environ.get('USER','root')]
    subprocess.run([str(runtime/'bin/mysqld'),*options,'--initialize-insecure'],env=env,check=True,stdout=subprocess.DEVNULL)
    sock=str(work/'mysql.sock')
    with (work/'mysql.log').open('w') as log:
        proc=subprocess.Popen([str(runtime/'bin/mysqld'),*options,'--skip-networking','--socket='+sock,'--pid-file='+str(work/'mysql.pid')],env=env,stdout=log,stderr=log)
        def sql(text,ok=True):
            r=subprocess.run([str(runtime/'bin/mysql'),'--no-defaults','--socket='+sock,'--user=root','--batch','--skip-column-names'],input=text,text=True,env=env,capture_output=True)
            assert (r.returncode==0)==ok,(r.stdout,r.stderr)
            return r.stdout.strip()
        try:
            for _ in range(120):
                if Path(sock).exists(): break
                assert proc.poll() is None,(work/'mysql.log').read_text()
                time.sleep(.25)
            setup=(repo/'Shared/Database/mysql/setup.sql').read_text().replace('GameServerDB','TransferQA')
            sql(setup)
            verify=(repo/'Shared/Database/mysql/schema_verify.sql').read_text().replace('GameServerDB','TransferQA')
            verify=verify.split("SHOW GRANTS FOR")[0]
            assert '0\t0\t0\t0\t1' in sql(verify)
            precheck=(repo/'Shared/Database/mysql/schema_precheck.sql').read_text().replace('GameServerDB','TransferQA')
            assert 'DONE -' in sql(precheck)
            sql('USE TransferQA; DROP PROCEDURE P_PLAYER_TRANSFER;')
            assert 'RUN migrate_player_transfer.sql' in sql(precheck)
            sql((repo/'Shared/Database/mysql/migrate_player_transfer.sql').read_text().replace('GameServerDB','TransferQA'))
            print('PASS schema verification / missing transfer migration detected')
            sql('USE TransferQA; INSERT INTO User(UserID,Name,Password,Adena) VALUES(1,"A","p",100),(2,"B","p",100); INSERT INTO UserInventory VALUES(1,0,1,1,0,300,1024);')
            row=dict(slot=0,has=1,item=1,count=1,equipped=0,enchant=300,extra=1024,keep=0,newItem=0,newCount=0,newEquipped=0,newEnchant=0,newExtra=0)
            incoming=dict(row,has=0,item=0,count=0,enchant=0,extra=0,keep=1,newItem=1,newCount=1,newEnchant=300,newExtra=1536)
            data=dict(kind=2,players=[dict(id=1,beforeAdena=100,afterAdena=125,offers=[dict(item=1,count=1,enchant=300,extra=1024)],slots=[row]),dict(id=2,beforeAdena=100,afterAdena=75,offers=[],slots=[incoming])])
            def call(receipt,payload,ok=True):
                return sql("USE TransferQA; CALL P_PLAYER_TRANSFER('"+receipt+"','"+json.dumps(payload,separators=(',',':'))+"');",ok)
            receipt='1'*32
            assert call(receipt,data)=='1'
            expected='1\t125\n2\t75\n2\t0\t1\t1\t0\t300\t1536\n1'
            query='USE TransferQA; SELECT UserID,Adena FROM User ORDER BY UserID; SELECT * FROM UserInventory ORDER BY UserID; SELECT COUNT(*) FROM PlayerTransfer;'
            assert sql(query)==expected,sql(query)
            assert call(receipt,data)=='1'
            assert sql(query)==expected
            print('PASS MySQL atomic sale / gear options / enchant 300 / receipt replay')
            conflict=json.loads(json.dumps(data));conflict['players'][0]['afterAdena']=126
            call(receipt,conflict,False)
            assert sql(query)==expected
            print('PASS receipt payload conflict / unchanged inventory and money')
            stale=json.loads(json.dumps(data));stale['players'][0]['beforeAdena']=125;stale['players'][0]['afterAdena']=150;stale['players'][1]['beforeAdena']=75;stale['players'][1]['afterAdena']=50
            call('2'*32,stale,False)
            assert sql(query)==expected
            print('PASS stale inventory / both-player rollback')
            rollback=json.loads(json.dumps(stale));rollback['players'][0]['slots']=[];rollback['players'][1]['slots'][0]['has']=0
            call('3'*32,rollback,False)
            assert sql(query)==expected
            print('PASS failure after first wallet update / complete rollback')
            sql((repo/'Shared/Database/mysql/migrate_player_transfer.sql').read_text().replace('GameServerDB','TransferQA'))
            assert call(receipt,data)=='1'
            print('PASS migration rerun preserves receipts')
        finally:
            proc.terminate();proc.wait(timeout=20)
    print('QA files:',work)

if __name__=='__main__': main()
