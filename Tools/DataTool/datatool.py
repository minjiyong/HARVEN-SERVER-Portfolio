#!/usr/bin/env python3
"""하르벤 기획 데이터 도구 — 구글 시트(HARVEN_DATA 폴더, 표 하나 = 파일 하나) ↔ Bin/GameData/ ↔ 서버.

  python3 Tools/DataTool/datatool.py <명령> [옵션]

  sync       pull → check → export (기획자 · 개발자가 쓰는 한 줄)
  pull       표 파일마다 values.batchGet → .cache/sheet_raw.json
  check      스키마 · 참조 · id 불변 · 바이트 상한 · 묶음 규칙 검증(출력: 표!행 열: 이유, --to-sheet면 _Errors 파일에도)
  export     check 통과 시에만 Bin/GameData/ · manifest.json · Gen/*Ids.h(+ 시트의 #이름 보조 열을 채운다)
  watch      개발용 — 표 파일을 돌아가며 읽어 바뀌면 sync --to-sheet(--dev 서버가 GameData를 다시 읽는다, --tables면 그 표만)
  verify     상시 시험 — GameData = 로더 메모리 덤프 + 행 수 스냅샷(Tools/DataTool/rowcounts.json, --update로 갱신)
  d2         검증기 시험 — 규칙마다 틀린 데이터를 넣어 정확한 위치 · R번호로 잡히는지
  push       정본 JSON → 표 파일(새 표를 처음 올릴 때만 — 데이터가 있으면 거부)
  gen        C++ 열거형 이름 표(Database/GameData/Gen/EnumNames.h)를 헤더에서 다시 만든다
  lineup     장비 라인업 생성기 — Tools/DataTool/lineup/phase1.yaml → Item.json 장비 · 재료 행 + CraftRecipe(오프라인, ECONOMY.md)

키: 환경 변수 HARVEN_SHEETS_KEY(서비스 계정 JSON 경로, 기본 ~/.config/harven/sheets-sa.json).
표 → 파일 id: Shared/ToolsConfig/DataTool.json.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dt import paths  # noqa: E402


def cmd_gen(_args) -> int:
    from dt import enums
    data = enums.load_all(paths.DATA_SRC)
    paths.GEN_DIR.mkdir(parents=True, exist_ok=True)
    target = paths.GEN_DIR / "EnumNames.h"
    text = enums.render_cpp(data)
    old = target.read_text(encoding="utf-8") if target.exists() else ""
    if old != text:
        target.write_text(text, encoding="utf-8", newline="\n")
        print(f"[gen] {target.relative_to(paths.REPO)} 갱신 ({len(data)}개 열거형)")
    else:
        print(f"[gen] {target.relative_to(paths.REPO)} 그대로 ({len(data)}개 열거형)")
    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="datatool", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("gen", help="열거형 이름 표 생성").set_defaults(fn=cmd_gen)

    from dt import pipeline
    sp = sub.add_parser("push", help="정본 JSON → 표 파일(새 표를 처음 올릴 때)")
    sp.add_argument("--tabs", help="쉼표로 표 이름(기본: DataTool.json의 모든 표)")
    sp.add_argument("--source", help="정본 폴더(기본 Bin/GameData)")
    sp.add_argument("--force-seed", action="store_true", help="데이터가 있는 탭도 덮는다(기획자 수정이 사라진다)")
    sp.add_argument("--meta", action="store_true", help="_meta/_README · _Enums 파일만 다시 쓴다(표 파일은 건드리지 않는다)")
    sp.set_defaults(fn=pipeline.cmd_push)
    sub.add_parser("pull", help="표 파일들 → .cache/sheet_raw.json").set_defaults(fn=pipeline.cmd_pull)
    sp = sub.add_parser("watch", help="개발용 — 표 파일을 돌아가며 읽어 바뀌면 sync --to-sheet(--dev 서버가 다시 읽는다)")
    sp.add_argument("--interval", default="3", help="읽기 간격(초, 기본 3 — 매 차례 읽는 표 수는 읽기 할당량에 맞춘다)")
    sp.add_argument("--tables", help="고칠 표(쉼표) — 이 표만 매 차례 읽는다(시트 수정 → 게임 몇 초)")
    sp.add_argument("--count", type=int, default=0, help="sync를 이만큼 하고 끝(0 = 계속, 시험용)")
    sp.set_defaults(fn=pipeline.cmd_watch)
    sp = sub.add_parser("verify", help="상시 시험 — GameData = 메모리 덤프 + 행 수 스냅샷(Tools/DataTool/rowcounts.json)")
    sp.add_argument("--update", action="store_true", help="행 수 스냅샷을 지금 GameData로 갱신")
    sp.set_defaults(fn=pipeline.cmd_verify)
    sp = sub.add_parser("check", help="검증(시트 캐시 또는 --gamedata)")
    sp.add_argument("--gamedata", action="store_true", help="시트 대신 GameData/를 검사")
    sp.add_argument("--to-sheet", action="store_true", help="결과를 _meta/_Errors 파일에도")
    sp.set_defaults(fn=pipeline.cmd_check)
    sub.add_parser("export", help="check 통과 시 GameData/ · manifest · Gen/*Ids.h").set_defaults(fn=pipeline.cmd_export)
    sp = sub.add_parser("sync", help="pull → check → export")
    sp.add_argument("--to-sheet", action="store_true", help="check 결과를 _meta/_Errors 파일에도")
    sp.set_defaults(fn=pipeline.cmd_sync)
    sp = sub.add_parser("d2", help="D2 검증기 시험(규칙마다 틀린 데이터 → 정확한 위치 · R번호)")
    sp.add_argument("--only", help="쉼표로 사례 이름")
    sp.add_argument("--random", type=int, help="무작위 규칙 N개만")
    sp.add_argument("--seed", type=int, default=None)
    sp.set_defaults(fn=lambda a: __import__("dt.d2run", fromlist=["run"]).run(a.only.split(",") if a.only else None, a.random, a.seed))

    from dt import lineup
    sp = sub.add_parser("lineup", help="장비 라인업 생성기(YAML → Item.json 장비 · 재료 행 + CraftRecipe)")
    sp.add_argument("--yaml", help="라인업 YAML(기본 Tools/DataTool/lineup/phase1.yaml)")
    sp.add_argument("--dry-run", action="store_true", help="요약만 찍고 쓰지 않는다")
    sp.add_argument("--seed-econ", action="store_true", help="YAML seed 절로 LootConst · TierLoot · EliteGear · BossLoot · Dismantle도 쓴다(시트 파일이 생기기 전 첫 값)")
    sp.set_defaults(fn=lineup.cmd_lineup)

    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
