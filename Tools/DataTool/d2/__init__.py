"""D2 검증기 시험 — 규칙마다 일부러 틀린 GameData를 만들어 검사기가 정확한 위치 · R번호로 잡는지(SHEET_DATA_PROMPT [9]).

묶음마다 파일 하나(d2/<묶음>.py)에 CASES 목록 — Phase 2 일꾼은 자기 파일만 고친다.
사례: {"name", "rules": [R번호…], "table", "mutate": 함수(data) , "expect": {"rule", "where", "what"} (부분 일치)}
  · rules: 이 사례가 덮는 RULES.md 규칙(대조표 D2 칸에 이름이 들어간다)
  · engine: "cpp"(기본 — 서버와 같은 로더) · "py"(datatool check — 시트 수준 검사) · "code"(규칙이 코드에 남았는지 grep)
"""
