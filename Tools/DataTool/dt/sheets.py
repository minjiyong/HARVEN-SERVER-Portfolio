"""Google Sheets · Drive API — 서비스 계정 키로 REST를 직접 부른다.

시트 구조: 드라이브 HARVEN_DATA 폴더 — 표 하나 = 스프레드시트 파일 하나(파일 안 탭 하나, 탭 이름 = 표 이름),
_meta 폴더에 _README · _Enums · _Errors. 표 → 파일 id는 Shared/ToolsConfig/DataTool.json.
키: 환경 변수 HARVEN_SHEETS_KEY(서비스 계정 JSON 경로), 없으면 ~/.config/harven/sheets-sa.json. 키 · 토큰은 출력하지 않는다.
할당량: 읽기 · 쓰기 각각 분당 60(사용자당) — 429면 물러섰다 다시 한다.
"""
from __future__ import annotations

import json
import os
import threading
import time
from pathlib import Path

from .paths import CONFIG

SCOPES = ["https://www.googleapis.com/auth/spreadsheets", "https://www.googleapis.com/auth/drive.metadata.readonly"]
API = "https://sheets.googleapis.com/v4/spreadsheets"
DRIVE = "https://www.googleapis.com/drive/v3/files"


def key_path() -> Path:
    p = os.environ.get("HARVEN_SHEETS_KEY")
    if p:
        return Path(p)
    return Path.home() / ".config" / "harven" / "sheets-sa.json"


def service_email() -> str:
    return json.loads(key_path().read_text())["client_email"]


def config() -> dict:
    return json.loads(CONFIG.read_text(encoding="utf-8")) if CONFIG.exists() else {}


class Client:
    """인증 · HTTP 세션 하나를 파일 핸들(Sheets)들이 같이 쓴다(스레드 안전 — 토큰 갱신만 잠근다)."""

    def __init__(self):
        from google.oauth2 import service_account
        import google.auth.transport.requests as gtr
        import requests

        kp = key_path()
        if not kp.exists():
            raise SystemExit(f"[sheets] 서비스 계정 키가 없다: {kp} (HARVEN_SHEETS_KEY)")
        self._creds = service_account.Credentials.from_service_account_file(str(kp), scopes=SCOPES)
        self._req = gtr.Request()
        self._requests = requests
        self._lock = threading.Lock()
        self._local = threading.local()

    def _headers(self) -> dict:
        with self._lock:
            if not self._creds.valid:
                self._creds.refresh(self._req)
            return {"Authorization": "Bearer " + self._creds.token}

    def _session(self):
        s = getattr(self._local, "s", None)
        if s is None:
            s = self._local.s = self._requests.Session()
        return s

    def call(self, method: str, url: str, **kw):
        for attempt in range(7):
            r = self._session().request(method, url, headers=self._headers(), timeout=60, **kw)
            if r.status_code in (429, 500, 503):
                time.sleep(min(60, 2 ** attempt * 2))   # 할당량(분당 60) — 물러섰다 다시
                continue
            if r.status_code >= 400:
                raise SystemExit(f"[sheets] {method} {url.split('?')[0]} → {r.status_code}: {r.text[:400]}")
            return r.json() if r.text else {}
        raise SystemExit(f"[sheets] {method} 재시도 실패")

    def file(self, spreadsheet_id: str) -> "Sheets":
        return Sheets(spreadsheet_id, self)

    def folder_files(self, folder_id: str) -> list[dict]:
        """폴더 바로 아래 파일들(id · name · modifiedTime · version) — Drive 호출 한 번(시트 읽기 할당량을 쓰지 않는다)"""
        r = self.call("GET", DRIVE, params={"q": f"'{folder_id}' in parents and trashed=false", "pageSize": 1000,
                                            "fields": "files(id,name,mimeType,modifiedTime,version)"})
        return r.get("files", [])


class Sheets:
    """스프레드시트 파일 하나."""

    def __init__(self, spreadsheet_id: str, client: Client | None = None):
        if not spreadsheet_id:
            raise SystemExit("[sheets] 스프레드시트 id가 비었다 — Shared/ToolsConfig/DataTool.json")
        self.client = client or Client()
        self.id = spreadsheet_id

    def _call(self, method: str, url: str, **kw):
        return self.client.call(method, url, **kw)

    # ── 읽기 ──
    def meta(self) -> dict:
        return self._call("GET", f"{API}/{self.id}", params={"fields": "properties(title),sheets(properties,protectedRanges(protectedRangeId,description))"})

    def tabs(self) -> dict[str, dict]:
        return {s["properties"]["title"]: s for s in self.meta().get("sheets", [])}

    def batch_get(self, ranges: list[str]) -> dict[str, list[list]]:
        """values.batchGet 한 번 — UNFORMATTED_VALUE(천 단위 쉼표 · % · 날짜 서식이 섞이지 않게)"""
        out: dict[str, list[list]] = {}
        if not ranges:
            return out
        r = self._call("GET", f"{API}/{self.id}/values:batchGet",
                       params=[("ranges", x) for x in ranges] + [("valueRenderOption", "UNFORMATTED_VALUE"),
                                                                 ("dateTimeRenderOption", "SERIAL_NUMBER"),
                                                                 ("majorDimension", "ROWS")])
        for vr in r.get("valueRanges", []):
            name = vr["range"].split("!")[0].strip("'")
            out[name] = vr.get("values", [])
        return out

    def modified_time(self) -> str:
        r = self._call("GET", f"{DRIVE}/{self.id}", params={"fields": "modifiedTime"})
        return r.get("modifiedTime", "")

    # ── 쓰기 ──
    def batch_update(self, requests_: list[dict]) -> dict:
        if not requests_:
            return {}
        return self._call("POST", f"{API}/{self.id}:batchUpdate", json={"requests": requests_})

    def values_batch_update(self, data: list[dict], raw: bool = True) -> dict:
        """[{range, values}] — 값만 여러 범위를 한 번에"""
        if not data:
            return {}
        return self._call("POST", f"{API}/{self.id}/values:batchUpdate",
                          json={"valueInputOption": "RAW" if raw else "USER_ENTERED", "data": data})

    def values_clear(self, a1: str) -> dict:
        return self._call("POST", f"{API}/{self.id}/values/{a1}:clear", json={})

    def values_append(self, a1: str, values: list[list]) -> dict:
        """아래로 이어 쓴다(칸이 모자라면 행을 늘린다)"""
        return self._call("POST", f"{API}/{self.id}/values/{a1}:append",
                          params={"valueInputOption": "RAW", "insertDataOption": "OVERWRITE"},
                          json={"range": a1, "majorDimension": "ROWS", "values": values})

    def copy_tab_to(self, sheet_id: int, dest_spreadsheet_id: str) -> dict:
        """탭 하나를 다른 스프레드시트로 복사(값 · 서식 · 메모 · 열 너비 그대로) → 새 탭 속성"""
        return self._call("POST", f"{API}/{self.id}/sheets/{sheet_id}:copyTo", json={"destinationSpreadsheetId": dest_spreadsheet_id})
