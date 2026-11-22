"""저장소 안 경로 · 공용 상수(폴더 구조는 README '폴더 구조' — REIW식 Server · Libs · Shared · Bin · Tools)."""
from __future__ import annotations

from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent          # Tools/DataTool
REPO = TOOL_DIR.parent.parent                              # HARVEN-SERVER
SERVER = REPO / "Server" / "HARVEN_GameServer"              # 게임 서버 소스(CMakeLists.txt가 모두 모은다)
LIBS = REPO / "Libs"                                       # 네트워크 · DB · 스레드 · 기반 코드
SHARED = REPO / "Shared"                                   # 프로토콜 · 문서 · 도구 설정
DATA_SRC = SERVER / "Database" / "GameData"                # 표 파사드 · 로더(Load) · 생성 헤더(Gen)
GEN_DIR = DATA_SRC / "Gen"
GAMEDATA = REPO / "Bin" / "GameData"                       # 시트에서 내보낸 정본 JSON(서버 · 클라가 읽는다)
CONFIG = SHARED / "ToolsConfig" / "DataTool.json"          # 표 → 스프레드시트 파일 id
SCHEMA_DIR = TOOL_DIR / "schema"
CACHE = TOOL_DIR / ".cache"
ROWCOUNTS = TOOL_DIR / "rowcounts.json"                    # verify의 행 수 스냅샷
BUILD = REPO / "build" / "datatool"
