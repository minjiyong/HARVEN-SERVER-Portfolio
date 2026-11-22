"""아이템 감사(2026-10-13) MySQL 프로시저 시험 — 격리된 로컬 mysqld를 새로 띄워 setup.sql을 깔고 프로시저를 직접 부른다(운영 DB에 붙지 않는다).

    python3 Tools/Test/regress_items_mysql.py --runtime <mysqld가 bin/에 있는 폴더> --libs <libmysqlclient 폴더>
    (우분투 mysql-server 8.0: 폴더 하나에 bin/mysqld → /usr/sbin/mysqld · bin/mysql → /usr/bin/mysql · share → /usr/share 를 이어 둔다)

시나리오
  enchantfirst  P_ENCHANT_TRADE — 최초 달성 기록은 서버가 넘긴 단계(EnchantConst.first_notice_level 이상일 때만 0이 아니다)를 그대로 쓴다.
                예전엔 프로시저가 11을 박아 두어 시트 값을 낮추면 MySQL만 기록을 빠뜨렸다(인메모리 DB는 시트 값을 따랐다).
                setup.sql과 migrate_user_id.sql(같은 프로시저 묶음) 둘 다 본다.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[2]


class Mysql:
    def __init__(self, runtime: Path, libs: Path):
        self.work = Path(tempfile.mkdtemp(prefix='harven-items-mysql-'))
        self.runtime = runtime.resolve()
        self.env = os.environ.copy()
        self.env['LD_LIBRARY_PATH'] = str(libs.resolve())
        self.options = ['--no-defaults', '--basedir=' + str(self.runtime), '--datadir=' + str(self.work / 'data'),
                        '--user=' + os.environ.get('USER', 'root')]
        subprocess.run([str(self.runtime / 'bin/mysqld'), *self.options, '--initialize-insecure'], env=self.env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.sock = str(self.work / 'mysql.sock')
        self.log = (self.work / 'mysql.log').open('w')
        self.proc = subprocess.Popen([str(self.runtime / 'bin/mysqld'), *self.options, '--skip-networking', '--socket=' + self.sock,
                                      '--pid-file=' + str(self.work / 'mysql.pid')], env=self.env, stdout=self.log, stderr=self.log)
        for _ in range(160):
            if Path(self.sock).exists():
                break
            assert self.proc.poll() is None, (self.work / 'mysql.log').read_text()
            time.sleep(0.25)

    def sql(self, text: str, ok: bool = True) -> str:
        r = subprocess.run([str(self.runtime / 'bin/mysql'), '--no-defaults', '--socket=' + self.sock, '--user=root', '--batch',
                            '--skip-column-names'], input=text, text=True, env=self.env, capture_output=True)
        assert (r.returncode == 0) == ok, (r.stdout, r.stderr)
        return r.stdout.strip()

    def close(self):
        self.proc.terminate()
        self.proc.wait(timeout=30)
        self.log.close()


def enchant_slots(level_before: int, level_after: int) -> str:
    """두루마리 칸 0(5장 → 4장) · 대상 칸 1(철검 level_before → level_after) — CDBConnection::SaveEnchant가 만드는 JSON과 같은 모양"""
    rows = [dict(slot=0, item=13, count=5, equipped=0, level=0, extra=0, keep=1, newCount=4, newLevel=0),
            dict(slot=1, item=3, count=1, equipped=0, level=level_before, extra=0, keep=1, newCount=1, newLevel=level_after,
                 graceLimit=0, graceBefore=0, graceAfter=0)]
    return json.dumps(rows, separators=(',', ':'))


def scenario_enchantfirst(db: Mysql, schema_file: str) -> list[str]:
    fails = []
    name = 'ItemsQA_' + schema_file.split('.')[0][-6:]
    text = (REPO / 'Shared/Database/mysql' / schema_file).read_text()
    if schema_file == 'setup.sql':
        db.sql(text.replace('GameServerDB', name))
    else:
        # migrate_user_id.sql은 옛 스키마를 옮기는 파일 — 프로시저 묶음(같은 본문)만 setup.sql 위에 다시 깐다
        db.sql((REPO / 'Shared/Database/mysql/setup.sql').read_text().replace('GameServerDB', name))
        block = text[text.index('-- New procedures: identical to the procedure block at the end of setup.sql.'):]
        db.sql(f'USE {name};\n' + block.replace('GameServerDB', name))
    db.sql(f'USE {name}; INSERT INTO User(UserID,Name,Password,Adena) VALUES(1,"A","p",0),(2,"B","p",0);'
           f' INSERT INTO UserInventory VALUES(1,0,13,5,0,0,0),(1,1,3,1,0,8,0),(2,0,13,5,0,0,0),(2,1,3,1,0,8,0);')
    # 시트 first_notice_level을 9로 낮춘 판: 서버는 9 이상 성공에만 in_Level = 도달 단계를 넘긴다
    out = db.sql(f"USE {name}; CALL P_ENCHANT_TRADE(1, '{enchant_slots(8, 9)}', 3, 9, 'A');")
    if out != '1\t1':
        fails.append(f'{schema_file}: 처음 +9 IsFirst {out!r}(기대 1 1)')
    rows = db.sql(f'USE {name}; SELECT ItemID, Enchant, UserID, Name FROM EnchantFirstAchievement;')
    if rows != '3\t9\t1\tA':
        fails.append(f'{schema_file}: 최초 기록 {rows!r}')
    out = db.sql(f"USE {name}; CALL P_ENCHANT_TRADE(2, '{enchant_slots(8, 9)}', 3, 9, 'B');")
    if out != '1\t0':
        fails.append(f'{schema_file}: 두 번째 +9 IsFirst {out!r}(기대 1 0)')
    # 0 = 기록하지 않음(서버가 문턱 아래 단계 · 실패에 넘기는 값)
    db.sql(f'USE {name}; UPDATE UserInventory SET Enchant = 9 WHERE UserID = 1 AND SlotIndex = 1;'
           f' UPDATE UserInventory SET `Count` = 5 WHERE UserID = 1 AND SlotIndex = 0;')
    out = db.sql(f"USE {name}; CALL P_ENCHANT_TRADE(1, '{enchant_slots(9, 10)}', 3, 0, 'A');")
    rows = db.sql(f'USE {name}; SELECT COUNT(*) FROM EnchantFirstAchievement;')
    if out != '1\t0' or rows != '1':
        fails.append(f'{schema_file}: in_Level 0은 기록하지 않는다 {out!r} {rows!r}')
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runtime', required=True, type=Path)
    ap.add_argument('--libs', required=True, type=Path)
    args = ap.parse_args()
    db = Mysql(args.runtime, args.libs)
    ok = True
    try:
        for schema in ('setup.sql', 'migrate_user_id.sql'):
            fails = scenario_enchantfirst(db, schema)
            print(f"{'PASS' if not fails else 'FAIL'} enchantfirst({schema}){': ' + ' · '.join(fails) if fails else ''}", flush=True)
            ok &= not fails
    finally:
        db.close()
        print('QA files:', db.work)
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
