# 기획 데이터 파이프라인 — 시트 → Bin/GameData → 서버

> 검증 규칙(R번호): [DATA_RULES.md](DATA_RULES.md). 몬스터 행동 · 기술 · 장판 표(패턴 만드는 법): [MONSTER_AI.md](MONSTER_AI.md).

```
드라이브 HARVEN_DATA/<표>_Table (표 하나 = 스프레드시트 파일 하나 · `DataTool.json` `tables` 47개 — `Achievement`는 시트 파일을 만들 차례, 3장 끝) ──(datatool pull)──▶ Tools/DataTool/.cache/sheet_raw.json
   ──(datatool check)──▶ 형 · 범위 · 열거형 · 참조 · 유일 · id 잠금 · UTF-8 바이트 · NFC + 묶음 규칙(C++ 로더와 같은 코드)
   ──(datatool export)──▶ Bin/GameData/*.json · _src.json · manifest.json · ids.lock · Database/GameData/Gen/*Ids.h  ← git 커밋
   ──(서버 부팅: GameData::LoadFromDir)──▶ 불변 스냅샷 ──▶ 기존 파사드(MonsterTable::Find …)
HARVEN-DESIGN tools/mapgen ──▶ Bin/GameData/Town*.json(마을 격자 · 배치 — 시트가 아니다)
```

## 1. 기획자용 한 장

1. **드라이브 `HARVEN_DATA` 폴더의 표 파일만 고친다.** 표 하나 = 파일 하나(`Monster_Table` · `Item_Table` …), 파일 안 탭 하나(탭 이름 = 표 이름 — 바꾸지 않는다).
   1행 = 열 키(보호), 2행 = 설명, 3행부터 데이터. 규칙은 `_meta/_README`.
2. 드롭다운(열거형)은 고르기만, 여러 개는 `|`로(`Knight|NightHunter`). 색은 `#RRGGBB`. 체크박스 = 예/아니오. 드롭다운 이름의 원본은 `_meta/_Enums`.
3. **id는 영구** — 바꾸거나 지우지 않는다. 안 쓰는 행은 `deprecated` 체크. 새 행은 새 id로.
4. `#item_id` 같은 **이름 보조 열**은 그 id가 가리키는 이름이다. 파일이 나뉘어 수식으로 다른 파일을 읽지 않고 **datatool sync · watch가 채운다**(고쳐도 다시 덮인다).
5. 반영: 개발자 PC에서 `python Tools/DataTool/datatool.py sync --to-sheet` → 오류가 있으면 `_meta/_Errors`에 위치와 이유(`Monster!14 name: 27바이트 > 19` —
   `Monster_Table` 파일의 14행), **GameData는 그대로**. 오류가 없으면 `Bin/GameData/`가 바뀐다 → git 커밋(= 기획 변경 이력) → 서버 재시작.
   개발 중에는 `datatool watch`를 켜 두면 개발 서버(`--dev`)에 반영된다(값만 바꾼 경우 — 행을 더하거나 id를 바꾸면 서버 재시작이 필요하다고 로그에 나온다).
6. 행 순서 · 정렬은 자유(서버는 키로만 찾는다). `#`로 시작하는 행 · 열은 메모로 쓸 수 있다.

## 2. 개발자용

### 한 번 준비
- Python 3.11+: `pip install -r Tools/DataTool/requirements.txt`
- 서비스 계정 키: `~/.config/harven/sheets-sa.json`(권한 600) 또는 환경 변수 `HARVEN_SHEETS_KEY`. 키는 저장소 밖 — 커밋 · 출력 금지.
  서비스 계정은 `HARVEN_DATA` 폴더에 편집자로 공유돼 있다(폴더 안 새 파일은 자동으로 같은 권한).
- 표 → 파일 id: `Shared/ToolsConfig/DataTool.json`(`tables` 순서 = 처리 순서, `meta` = _README · _Enums · _Errors, `archive` = 분리 전 통합본 · `retired` = 지운 표의 파일 — 읽지 않는다).

### 명령
| 명령 | 하는 일 |
|---|---|
| `datatool sync [--to-sheet]` | pull → check → export(한 줄). 시트의 이름 보조 열(#…)도 채운다. 표 47개를 6개씩 동시에 읽어 5초 남짓 |
| `datatool watch [--tables A,B] [--interval 3]` | 개발용 — 바뀌면 sync --to-sheet. `--dev` 서버가 GameData를 다시 읽는다. 아래 "watch와 읽기 할당량" |
| `datatool check [--gamedata] [--to-sheet]` | 검증만. `--gamedata`면 시트 대신 Bin/GameData/ |
| `datatool verify [--update]` | **상시 시험** — GameData = 로더 메모리 덤프(바이트) + 표마다 행 수 = `Tools/DataTool/rowcounts.json`. 행을 더하면 실패 → 의도한 변경이면 `--update`로 스냅샷을 같은 커밋에 |
| `datatool d2 [--random 10]` | 검증기 시험 — 규칙마다 틀린 데이터를 넣어 정확한 위치 · R번호로 잡히는지(`Tools/DataTool/d2/<묶음>.py`) |
| `datatool push --tabs X [--force-seed]` | 정본 → 표 파일(새 표를 처음 올릴 때만 — 데이터가 있으면 거부) |
| `datatool push --meta` | `_meta/_README` · `_Enums`만 다시 쓴다(표 목록 · 열거형이 바뀌었을 때 — 표 파일은 건드리지 않는다) |
| `datatool gen` | 열거형 이름 표 생성(새 .cpp는 CMakeLists.txt가 알아서 모은다 — 등록할 것 없음) |
| `datatool lineup [--dry-run] [--seed-econ]` | 장비 라인업 생성기(오프라인) — `Tools/DataTool/lineup/phase1.yaml` → Item.json 장비 · 재료 행 + CraftRecipe(`--seed-econ`이면 경제 표 첫 값도). 시트에는 `push --force-seed`로([ECONOMY.md](ECONOMY.md)) |

### watch와 읽기 할당량
시트 읽기 할당량은 사용자당 분당 60이고, 표가 파일 47개로 나뉘어 한 번에 다 읽으면 47번이다. 그래서 watch는
- 처음에 한 번 다 읽고, 그 뒤 매 차례(기본 3초) **2개씩 돌아가며** 읽는다(한 바퀴 약 1분). 처음 고친 표는 그 표 차례가 오면(최대 1분 남짓) 잡힌다.
- 바뀐 표는 **10분 동안 매 차례** 읽는다(기획자는 보통 한 표를 이어서 고친다 — 두 번째 수정부터는 몇 초).
- 고칠 표를 알면 `--tables Monster,Item` — 그 표만 매 차례 읽어 처음부터 몇 초다.
- Drive의 수정 시각 · version은 시트 API로 값을 고친 뒤 2분 넘게 늦게 바뀌어(직접 재 봤다) 쓰지 않고 내용 해시로 본다. 이름 보조 열은 해시에서 뺀다.

### 서버
- 운영과 개발의 차이: 운영 서버는 부팅 때 한 번만 읽는다(스냅샷 불변). **개발 서버 `--dev`**만 핫 리로드를 켠다.
- `--dev` 핫 리로드: 감시 스레드가 `manifest.json`을 0.25초마다 본다(datatool export는 manifest를 마지막에 쓴다) → 새 판을 읽고 검증 →
  **모든 표의 행 수 · 키(첫 열)가 지금 판과 같을 때만**(값만 바뀐 판) 새 세대로 올린다. 행을 더하거나 빼거나 id를 바꾸면 거부하고 재시작을 안내한다.
  틀린 데이터면 거부하고 틀린 곳을 찍는다 — 서버는 지금 판으로 계속 돈다.
  - 로직 스레드(Zone · 기본)는 묶음(틱)을 시작할 때 세대를 고정한다(`GameData::PinThread`) → 한 틱 안에서는 한 판만 보이고 새 값은 다음 틱부터.
  - 옛 세대는 해제하지 않는다(장비 행 포인터 등이 옛 세대를 가리킬 수 있다) — 리로드마다 세대 하나(≈ 133 KB)가 남는다. 개발 서버 전용이라 재시작으로 비운다.
  - 대화창(개발 서버만): `/dataver`(지금 판 해시 · 세대 수) · `/reloaddata`(바로 다시 읽기).
- `GameServer [--data-dir <폴더>]` — 기본: 실행 파일 옆 `GameData/` → 위로 올라가며 `GameData/manifest.json` 또는 `Bin/GameData/manifest.json`이 있는 곳
  (빌드 출력 `build/<프리셋>/`에서는 저장소의 `Bin/GameData`). 부팅 첫 줄 `[GameData] loaded N rows / T tables in X ms (version …)`. 틀린 데이터면 오류를 모두 찍고 종료 코드 1.
- `--check-data <폴더>`(검증만) · `--dump-data <폴더>`(메모리 → 정본 JSON).
- 리눅스: `build/datatool/datacheck <폴더> [--dump <출력>]`(같은 로더를 g++로 — datatool이 자동으로 빌드). `HARVEN_DATACHECK=<GameServer>`면 따로 빌드하지 않고 서버로 같은 일을 한다.

## 3. 새 표 · 새 열을 더하는 법

> 코드 위치: 표 파사드 · 로더는 `Server/HARVEN_GameServer/Database/GameData/`(Load/ · Gen/), 정본 JSON은 `Bin/GameData/`, 도구는 `Tools/DataTool/`.

1. **스키마** `Tools/DataTool/schema/<묶음>.yaml` — 열 키 · 형 · 한글 설명(시트 2행) · `ref` · `min/max` · `max_bytes` · `nonempty` · `default` · `key` · `child_of` ·
   `rows_exact` · `id_lock` · `code_names`. 열 순서 · 형은 `Database/GameData/Load/Canon_<묶음>.h`와 같아야 한다(datatool이 메모리 덤프의 `_schema.json`과 대조한다).
   형: `u8` · `u16` · `u32` · `u64`(시트의 수는 배정밀도라 2^53 − 1까지 — 강화 성공 몫처럼 1조 단위 값, 시트에는 천 단위 쉼표로 보인다) · `i8` · `i16` · `i32` · `f32` · `bool` · `str` · `str?` · `color` · `enum:` · `flags:` · `mask:`.
2. **정본 쓰기** `Load/Canon_<묶음>.h` — 열 순서 · 형. 파이썬 `dt/canon.py`와 같은 바이트(verify가 매번 확인).
3. **스냅샷** `Load/Snap_<묶음>.h` — 런타임 구조체. 표마다 `std::vector<Row>`를 **id로 바로 색인** — 핫 경로에 해시맵 금지.
4. **Bind** `Load/Bind_<묶음>.cpp` — 파사드 구현(`GD()`로), `Bind<묶음>`(열마다 `RowReader`), `Validate<묶음>`(새 규칙은 다음 R번호 — `DATA_RULES.md`에 한 줄), `Dump<묶음>`.
   `const char*`는 스냅샷 문자열 영역(`RowReader`가 해 준다) — 임시 `std::string`의 `c_str()` 금지.
5. **구조 상수는 코드에** — 배열 크기 · 패킷 필드 · 열거형 수 · 이름 19바이트. 넘으면 로드 실패(자르지 않는다). 튜닝 값(확률 · 성능 · 알림 수치 등)은 코드에 두지 않는다 — 한 행짜리 `<묶음>Const` 표(`GrowthConst` · `EnchantConst`).
   코드가 부르는 id는 시트의 `code_name` 열 → `Gen/<표>Ids.h`. 시트에 새 `code_name`을 넣고 코드에서 부르면 `datatool sync` · `check` · `export`가 검사기 빌드 전에 이름 헤더부터 시트에 맞춘다(`[gen] … 갱신`).
6. **시트 파일** — 서비스 계정은 파일을 만들 수 없다(드라이브 저장 공간 0). 드라이브 `HARVEN_DATA`에 `<표>_Table` 스프레드시트를 만들고(사람 · 커넥터),
   `DataTool.json` `tables`에 id를 더한 뒤 `datatool push --tabs <표>` → `datatool push --meta`(_README 표 목록) → `datatool sync`.
7. **시험** — `datatool verify --update`(행 수 스냅샷) · `datatool d2`(새 규칙마다 `d2/<묶음>.py`에 사례) · `cmake --build --preset linux-memory`(서버 빌드).

**시트 파일보다 코드가 먼저인 새 표**(예: 개인 업적 `Achievement` — 묶음 `achieve`, 2026-10-13 — [ACHIEVEMENT.md](ACHIEVEMENT.md)):
첫 행은 명세에서 `dt.export.write`로 정본을 쓴다(`Bin/GameData/<표>.json` · `_src.json`(시트 행 = 키 순 + 3) · `ids.lock` · manifest — 클라 생성기가 바로 읽는다).
`DataTool.json` `tables`에는 파일 id가 생길 때까지 넣지 않는다(빈 id는 sync · check가 거부). 그동안 `check --gamedata`는 그 표의 C++ 규칙만,
`verify` · `d2`는 그 표도 본다. 시트 파일을 만들면 6 → `push --tabs <표>`(정본을 시트에) → `push --meta` → `sync`.
**시트 반영 완료(2026-10-13)**: `Achievement_Table`을 만들어 `DataTool.json`에 더하고 `push --tabs Achievement` · `push --meta` · `sync`(바뀐 파일 0) — 이제 시트가 원본이다.

## 4. 표를 지우는 법

> 예: 경제 1차 로직 2부가 `GearDrop` · `GearChance` · `ClassWeapon`을 지웠다(장비 굴림이 econ 묶음으로 — [ECONOMY.md](ECONOMY.md) 22절).

1. **코드가 먼저** — 그 표를 읽는 파사드 · 굴림 코드를 새 흐름으로 바꾸고 지운다(서버가 그 표 없이 빌드 · 동작해야 한다).
2. **로더** — `Load/Canon_<묶음>.h`(쓰기 함수 · V 구조체) · `Load/Snap_<묶음>.h`(스냅샷 칸) · `Load/Bind_<묶음>.cpp`(Bind · Validate · Dump의 그 표 부분)를 지운다.
3. **스키마** — `Tools/DataTool/schema/<묶음>.yaml`에서 그 표를 지운다(덤프의 `_schema.json`과 맞아야 한다 — `datatool check`가 대조).
4. **GameData** — `Bin/GameData/<표>.json`을 지우고 `_src.json`의 그 표 줄을 지운 뒤 manifest를 다시 쓴다(`dt.export.manifest_text` — 폴더의 *.json을 모두 센다).
   `ids.lock`에 그 표가 있으면(id_lock 표) 그 줄도 지운다. `Tools/DataTool/rowcounts.json`에서 그 표를 빼거나 `datatool verify --update`.
5. **시트 목록** — `Shared/ToolsConfig/DataTool.json` `tables`에서 빼고 파일 id는 `retired`로 옮긴다(datatool은 읽지 않는다 — 드라이브 파일은 사람이 지워도 된다). `datatool push --meta`(_README 표 목록).
6. **규칙** — 그 표만 보던 R번호는 [DATA_RULES.md](DATA_RULES.md)에 줄을 남기고 **폐지**(번호는 다시 쓰지 않는다), `d2/<묶음>.py`의 사례를 지운다.
7. **시험** — `datatool check --gamedata` · `datatool verify` · `datatool d2` · 서버 빌드 · 그 표를 고치던 회귀 시험의 시험용 GameData 수정(`patch_rows` 대상)을 새 표로.
