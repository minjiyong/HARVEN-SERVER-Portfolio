# HARVEN-SERVER

2D MMORPG 「하르벤」의 게임 서버 — **리눅스 전용**(Ubuntu 24.04) · C++20 · Boost.Asio(epoll) · MySQL 8.4(AWS RDS, unixODBC).
처음 3개월 1인 개발한 Windows IOCP 서버([포트폴리오](#포트폴리오))를 리눅스로 옮겼고, AWS 리눅스에 올리는 것을 목표로 합니다.

팀의 표준 개발 환경은 **Windows의 Visual Studio + WSL2 Ubuntu-24.04**입니다.
편집 · IntelliSense · 디버거는 Windows의 VS가, 빌드 · 실행은 WSL 안의 진짜 리눅스(g++ · gdb)가 맡습니다.

| 처음이면 | 읽을 곳 |
| --- | --- |
| 서버를 내 PC에서 띄우고 싶다 | [빠른 시작](#빠른-시작) |
| VS가 리눅스 코드를 어떻게 빌드 · 디버깅하는지 | [개발 환경 구조](#개발-환경-구조) |
| 클라우드 DB(RDS)에 붙이기 · 스키마 바꾸기 | [DB 설정](#db-설정) |
| 뭔가 안 된다 | [문제 해결](#문제-해결) |

**목차** — [빠른 시작](#빠른-시작) · [개발 환경 구조](#개발-환경-구조) · [Visual Studio 사용법](#visual-studio-사용법) · [DB 설정](#db-설정) · [문제 해결](#문제-해결) ·
[기획 데이터](#기획-데이터) · [시험](#시험) · [저장소와 작업 폴더](#저장소와-작업-폴더) · [폴더 구조](#폴더-구조) · [코드 컨벤션](#코드-컨벤션) · [관련 문서](#관련-문서) · [옮긴 기록](#옮긴-기록)

## 빠른 시작

처음 한 번만 하면 됩니다(30분쯤). 그다음부터는 VS에서 F5만 누릅니다.

**필요한 것** — Windows 10 22H2 이상 또는 11 · Git for Windows · Visual Studio 2022 이상(팀은 2026 Community) +
워크로드 **"Linux 및 Mac에서 C++를 사용한 개발"**(Visual Studio Installer → 수정. VS가 "Linux 및 Mac 지원을 설치하시겠습니까?"라고 물으면 그것이다).

1. **WSL Ubuntu 24.04 설치** — 관리자 PowerShell에서
   ```powershell
   wsl --install -d Ubuntu-24.04
   ```
   재부팅 뒤 Ubuntu가 처음 열리면 리눅스 사용자 이름 · 비밀번호를 만든다(이 비밀번호가 앞으로의 `sudo` 비밀번호).
   이미 `Ubuntu` 같은 다른 배포판이 있어도 괜찮다 — 아래 명령과 VS에서 **Ubuntu-24.04**를 고르면 된다.
2. **저장소 받기**
   ```powershell
   git clone -b dev https://github.com/SJKIM99/HARVEN-SERVER.git C:\HARVEN\HARVEN-SERVER
   ```
3. **WSL 준비 스크립트** — PowerShell에서
   ```powershell
   cd C:\HARVEN\HARVEN-SERVER
   wsl -d Ubuntu-24.04 -- bash Tools/Setup/setup_wsl.sh
   ```
   `sudo` 비밀번호를 한 번 묻는다(입력해도 화면에 안 보이는 게 정상). 1~2분 뒤 이렇게 끝난다.
   ```
   == 2/5 MySQL Connector/ODBC
      드라이버: MySQL ODBC 26.7 Unicode Driver
   == 3/5 RDS CA 묶음
      받음: Shared/Database/mysql/global-bundle.pem
   == 4/5 harven.env
      만듦: harven.env — HARVEN_DB_USER · HARVEN_DB_PASSWORD를 채운다
   == 5/5 RDS 연결 경로
      db.example.com:3306 에 닿는다
   끝.
   ```
   다시 돌려도 된다(된 것은 건너뛴다). `닿지 않는다`가 나오면 [내 IP를 보안 그룹에](#3-rds-보안-그룹에-내-ip) 넣어야 한다.
4. **DB 접속 정보** — 저장소 루트에 생긴 `harven.env`의 `HARVEN_DB_USER` · `HARVEN_DB_PASSWORD`를 채운다([DB 설정](#db-설정)).
   DB 없이 혼자 돌려 볼 거면 건너뛰고 6에서 `linux-debug-memory`를 고른다.
5. **Visual Studio로 열기** — `파일 → 열기 → 폴더` → `C:\HARVEN\HARVEN-SERVER`(`.sln`이 아니라 **폴더**를 연다).
6. **대상 · 프리셋 · 시작 항목** — 위 툴바에서
   - 대상 시스템: `로컬 컴퓨터` → **`WSL: Ubuntu-24.04`**
   - 구성(프리셋): **`linux-debug`**(클라우드 DB) 또는 `linux-debug-memory`(DB 없이)
   - `보기 → 출력` 창(출력 보기 선택: **CMake**)에 `CMake 생성이 완료되었습니다.`가 뜰 때까지 기다린다
   - 시작 항목: **`GameServer`**
7. **F5** — 처음엔 64개 파일을 빌드하느라 1~2분 걸린다. 아래 **원격 콘솔** 창에 이렇게 나오면 성공.
   ```
   [Env] /home/<나>/HARVEN-SERVER/harven.env: HARVEN_DB_DRIVER HARVEN_DB_HOST ... HARVEN_DB_CA
   [GameData] loaded … rows / … tables …
   DB Connect Success
   [Server] All 16 zone threads are ready.
   ```
   Windows에서 띄운 클라는 `127.0.0.1:4000`으로 붙는다.

## 개발 환경 구조

```
[Windows]                                   [WSL2 — 진짜 리눅스 커널이 도는 경량 VM]
C:\HARVEN\HARVEN-SERVER  (원본 · git)
      │ ① 소스 복사(rsync, 바뀐 것만)
      └───────────────────────────────▶  ~/HARVEN-SERVER          (복사본 — 빌드할 때마다 덮어쓴다)
Visual Studio ─ ② cmake · ninja · g++ ─▶  ~/HARVEN-SERVER/build/<프리셋>/GameServer   (리눅스 실행 파일)
Visual Studio ◀─ ③ gdb와 주고받기 ────▶  gdb ─ GameServer 프로세스(포트 4000) ─ TLS ─▶ AWS RDS(MySQL)
Windows 클라 ─── ④ 127.0.0.1:4000 ─────▶  (WSL2가 localhost 접속을 안으로 넘겨준다)
```

- **WSL2는 흉내가 아니다** — Windows 위의 가벼운 가상 머신에서 리눅스 커널이 돈다. 서버의 epoll · pthread · `/proc`이 그대로 동작하고,
  여기서 만든 실행 파일(ELF)은 AWS 리눅스에 올릴 것과 같은 종류다(다만 WSL은 x86_64 — Graviton이면 ARM).
- **VS는 조종석** — VS 자체는 Windows 프로그램이다. F5를 누르면 VS가 `wsl.exe`로 WSL 안에 명령을 보낸다.
  ① 소스를 `~/HARVEN-SERVER`로 복사하고(`CMakePresets.json`의 `sourceDir` · `rsync` — `/mnt/c`를 거쳐 빌드하면 느려서)
  ② WSL의 cmake(Ninja) · g++로 빌드하고 ③ WSL의 gdb로 실행한다. 중단점 · 호출 스택 · 조사식은 VS 화면이 gdb에 명령을 보내고 받은 답을 그린 것이다.
  디버그 정보 속 리눅스 경로(`/home/<나>/HARVEN-SERVER/...`)를 VS가 Windows 파일(`C:\HARVEN\...`)로 바꿔 열어 준다.
- **IntelliSense가 리눅스 코드를 아는 이유** — CMake 구성이 끝나면 VS가 파일마다 컴파일 옵션 · include 경로 · 매크로를 받아 오고,
  WSL의 리눅스 헤더(`/usr/include` · Boost)를 읽어 **g++처럼** 분석한다(출력 창의 "포함 경로를 추출했습니다").
  대상이 `로컬 컴퓨터`면 MSVC로 구성하려다 `CMakeLists.txt`의 "리눅스 전용" 검사에 걸려 실패하고, 프리셋도 목록에 안 나온다(`hostOS: Linux`).
- **프로세스는 WSL 안에 있다** — 작업 관리자에는 GameServer가 안 보이고 VM 전체가 `VmmemWSL` 하나로 보인다. WSL 터미널에서 `ps aux | grep GameServer` · `top -H`.

**파일은 어디에**

| 무엇 | 자리 | 비고 |
| --- | --- | --- |
| 소스 원본 · git | `C:\HARVEN\HARVEN-SERVER` | 고치고 커밋하는 곳은 **여기만** |
| 소스 복사본 | WSL `~/HARVEN-SERVER` | VS가 빌드 때마다 덮어쓴다 — 여기서 고친 것은 사라진다(.git도 없다) |
| 실행 파일 | WSL `~/HARVEN-SERVER/build/<프리셋>/GameServer` | |
| `harven.env` · RDS CA | 원본 루트 · `Shared/Database/mysql/global-bundle.pem` | git 밖. 소스와 함께 복사되어 WSL의 서버가 읽는다 |
| `crash.log` | 서버의 작업 폴더 | WSL에서 `find ~/HARVEN-SERVER -name crash.log` |

Windows 탐색기에서 WSL 파일 보기: 주소창에 `\\wsl.localhost\Ubuntu-24.04\home\<나>\HARVEN-SERVER`.
같은 공유기의 다른 기기에서 서버에 붙어야 하면 Windows 11에서 `%UserProfile%\.wslconfig`에 `[wsl2]` · `networkingMode=mirrored`를 넣고 `wsl --shutdown`.

## Visual Studio 사용법

| 툴바 | 고르는 것 |
| --- | --- |
| 대상 시스템 | `WSL: Ubuntu-24.04`(준비 스크립트를 돌린 배포판) |
| 구성 | 아래 프리셋 중 하나 |
| 시작 항목 | `GameServer`(봇 `StressTest` · 시험 `ConcurrencyTests`도 고를 수 있다) |

| 프리셋 | DB | 쓰는 때 |
| --- | --- | --- |
| `linux-debug` | 클라우드(RDS · `harven.env`) | 평소 개발 · 디버깅(`_DEBUG` 검사 · assert) |
| `linux-debug-memory` | 인메모리(서버를 끄면 사라진다) | RDS 없이 혼자 디버깅 |
| `linux-release` | 클라우드 | 운영과 같은 빌드 |
| `linux-memory` | 인메모리 | 회귀 시험 · 부하 시험 |
| `linux-asan` · `linux-tsan` | 인메모리 | 메모리 오류 · 정의되지 않은 동작 / 스레드 경쟁 검사 |

- **빌드** — `빌드 → 모두 빌드` 또는 `GameServer 빌드`. 결과는 `출력 → 빌드`.
- **디버깅** — F5 · 중단점 · `디버그 → 모두 중단`(⏸) · 호출 스택 · 조사식 그대로. 서버 출력(`std::cout`)은 **원격 콘솔** 창.
- **실행 인자**(`--dev` · `--port 4001` · `--load-test` …) — `디버그 → 디버그 및 시작 설정`이 여는 `launch.vs.json`의 GameServer 항목에
  `"args": [ "--dev" ]`를 넣는다(이 파일은 내 PC의 `.vs/`에 생기고 git에 안 올라간다).
- **CMake가 꼬였을 때** — 알림 줄의 `캐시 삭제 및 다시 생성`(또는 `프로젝트` 메뉴의 CMake 캐시 삭제 · 다시 구성).
  `CMakeLists.txt` · `CMakePresets.json`을 고치면 저장할 때 다시 구성한다.
- **Git** — Windows 쪽에서 한다(VS의 Git 메뉴 · Git for Windows). 줄 끝은 `.gitattributes`가 맞춘다(셸 스크립트 · 기획 데이터는 LF).
- **VS 없이 WSL 터미널에서** — VS가 복사해 둔 곳에서 `cd ~/HARVEN-SERVER && cmake --preset linux-debug && cmake --build --preset linux-debug`
  → `./build/linux-debug/GameServer`(포트 4000, `--port N`). 저장소를 WSL 안에 따로 클론해 써도 된다(`harven.env`는 그 루트에).

## DB 설정

서버는 DB를 두 방식으로 씁니다 — 프리셋이 정합니다.

| 방식 | 프리셋 | 준비 |
| --- | --- | --- |
| 인메모리(`Tools/Sandbox/MemDBConnection.cpp`) | `linux-debug-memory` · `linux-memory` · `linux-asan` · `linux-tsan` | 없음. 데이터는 서버를 끄면 사라진다 |
| 클라우드 — 개발 RDS(MySQL 8.4) | `linux-debug` · `linux-release` | 아래 1~4 |

개발 RDS: 인스턴스 `harven-dev-db`(AWS 서울 `ap-northeast-2`) · DB `GameServerDB` · TLS 필수(`require_secure_transport=ON`, 서버는 `SSLMODE=VERIFY_IDENTITY`로 인증서와 호스트 이름까지 확인).
서버는 DB 연결 8개(DB 샤드 8개)를 쓰고, 실행 중 DB가 끊기면 저장을 버리지 않고 순서대로 다시 시도한다(로그인 · 경매 · 결사 거래는 바로 실패).

### 개발자마다 할 일

#### 1. MySQL ODBC 드라이버 · RDS 인증서
[빠른 시작](#빠른-시작) 3의 `setup_wsl.sh`가 한다 — MySQL Connector/ODBC **Unicode** 드라이버(repo.mysql.com의 Ubuntu 24.04 최신판)와
AWS RDS CA 묶음(`Shared/Database/mysql/global-bundle.pem`). 확인은 WSL에서 `odbcinst -q -d`(`[MySQL ODBC 26.7 Unicode Driver]`가 보여야).
MariaDB ODBC 드라이버는 `SSLMODE=VERIFY_IDENTITY`를 몰라 쓰지 않는다.

#### 2. harven.env
저장소 루트의 개인 설정 파일(git 밖 — `.gitignore`). 견본은 [`harven.env.example`](harven.env.example), 준비 스크립트가 없으면 만들어 준다.

| 변수 | 값 |
| --- | --- |
| `HARVEN_DB_DRIVER` | `odbcinst -q -d`에 보이는 Unicode 드라이버 이름 그대로(스크립트가 채운다) |
| `HARVEN_DB_HOST` | RDS 엔드포인트(견본에 있다) |
| `HARVEN_DB_PORT` · `HARVEN_DB_NAME` | `3306` · `GameServerDB`(비우면 이 값) |
| `HARVEN_DB_USER` · `HARVEN_DB_PASSWORD` | **팀에서 따로 받는다.** 게임 서버는 `harven_app`(EXECUTE 권한만) |
| `HARVEN_DB_CA` | `Shared/Database/mysql/global-bundle.pem`(상대 경로는 `harven.env` 폴더 기준) |

- 서버는 시작할 때 실행 파일 폴더에서 위로 올라가며 처음 만나는 `harven.env`를 읽어 환경 변수로 올린다(`Libs/Base/Core/LocalEnv`).
  VS가 소스와 함께 WSL로 복사하므로 F5만 누르면 된다. 다른 파일을 쓰려면 환경 변수 `HARVEN_ENV_FILE`.
- **이미 있는 환경 변수가 먼저다** — 운영(systemd `EnvironmentFile=` · 컨테이너 비밀)은 이 파일 없이 환경 변수로 준다.
- 형식은 한 줄에 `KEY=VALUE`. `#`로 시작하는 줄은 주석, 줄 중간의 `#`은 값(비밀번호에 써도 된다). 빈 값은 없는 것으로 본다. CRLF · 메모장 BOM 괜찮다.
- 로그에는 파일 경로와 **변수 이름만** 찍힌다(`[Env] …/harven.env: HARVEN_DB_DRIVER …`). 값은 안 찍는다.
- **비밀번호는 git · 채팅 · 스크린샷 · 로그에 남기지 않는다.** 화면 공유 전에 `harven.env` 탭을 닫는다.

#### 3. RDS 보안 그룹에 내 IP
RDS는 허락한 IP에서만 3306을 받는다. 준비 스크립트가 `닿지 않는다`고 하면 찍힌 공인 IP를 AWS 권한이 있는 사람에게 보낸다.
추가하는 법: AWS 콘솔 → RDS → `harven-dev-db` → 연결 & 보안 → VPC 보안 그룹 → 인바운드 규칙 편집 → 규칙 추가
(유형 `MySQL/Aurora` · 포트 3306 · 소스 `x.x.x.x/32` · 설명에 이름). **`0.0.0.0/0`은 열지 않는다.** 집 · 카페처럼 IP가 바뀌면 다시 추가한다.

#### 4. 확인
`linux-debug`로 F5 → 원격 콘솔에 `DB Connect Success`. 실패하면 [DB 문제 해결](#db-문제-해결).

### DB 담당이 할 일 — 스키마

스키마 파일은 [`Shared/Database/mysql/`](Shared/Database/mysql/README.md)에 있다. 마스터 계정 `harven_admin`은 **스키마 설치 · 마이그레이션에만** 쓴다.

| 파일 | 하는 일 |
| --- | --- |
| `setup.sql` | 빈 DB에 새로 깐다(표 14개 · 프로시저 50개 — 결사 성장 · 경제 1차 포함). 옛 스키마가 있으면 거부한다 |
| `migrate_guild_growth.sql` | 결사 성장(PR #31) — 이미 돌고 있는 DB에 표 3개 · 프로시저 11개를 더한다. 기존 행은 그대로, 다시 돌려도 안전 |
| `migrate_economy_phase1.sql` | 경제 1차 — `P_ITEM_BATCH_TRADE`(제작 · 분해)를 더하고 `P_AUCTION_BUY`를 인자 9개(거래 이력 bit 9)로 바꾼다. **이 판 서버보다 먼저** 돌린다. 다시 돌려도 안전 |
| `migrate_user_id.sql` | 옛 이름(`Players` · `AuctionMaxIds` …) → 새 이름(`User` · `P_…`). 행은 그대로, 검사 후 변경, 다시 돌려도 안전 |
| `migrate_enchant_blacksmith.sql` · `migrate_harven_grace.sql` | 더 옛날 스키마일 때 `migrate_user_id.sql` 앞에(그 뒤에 `migrate_guild_growth.sql` → `migrate_economy_phase1.sql`) |
| `schema_precheck.sql` | **읽기 전용** — 지금 상태와 다음에 돌릴 파일(`next_step`) |
| `schema_verify.sql` | **읽기 전용** — 끝난 뒤 확인(`ok = 1`) · `harven_app` 권한 |

스키마를 바꾸는 순서(WSL에서, `sudo apt-get install -y mysql-client`):
1. 팀에 알리고 이 DB를 쓰는 **게임 서버를 전부 끈다**(옛 서버와 새 이름이 섞이면 저장이 실패한다).
2. **RDS 스냅샷** — 콘솔에서 `harven-dev-db` → 작업 → 스냅샷 생성, 또는
   `aws rds create-db-snapshot --region ap-northeast-2 --db-instance-identifier harven-dev-db --db-snapshot-identifier harven-dev-db-before-<무엇>`
3. 저장소 루트에서
   ```bash
   export H=db.example.com
   export CA=Shared/Database/mysql/global-bundle.pem
   m() { mysql -h "$H" -P 3306 -u harven_admin -p --ssl-mode=VERIFY_IDENTITY --ssl-ca="$CA" -t "$@"; }   # 비밀번호는 물을 때 입력
   m < Shared/Database/mysql/schema_precheck.sql     # 맨 아래 next_step을 본다
   m < Shared/Database/mysql/migrate_economy_phase1.sql   # next_step이 시킨 파일(들) — 결사 성장까지 반영된 RDS는 이것 하나
   m < Shared/Database/mysql/schema_verify.sql       # ok = 1
   ```
4. 서버를 켜고 `DB Connect Success` 뒤에 `does not exist`가 없는지 본다. 이상하면 2의 스냅샷으로 복원한다(복원은 새 인스턴스 — 엔드포인트가 바뀐다).

> **2026-10-09 현재** 개발 RDS는 아직 옛 이름(`Players` …)이다. dev 서버(PR #17 이후)로 붙으면 `DB Connect Success` 뒤에
> `PROCEDURE GameServerDB.P_AUCTION_MAX_ID_SELECT does not exist`가 난다 — 위 순서로 `migrate_user_id.sql`을 한 번 돌리면 된다.
> 돌린 뒤에는 PR #17 이전 코드로 만든 서버(예: `kgm_godot_ver` 브랜치)는 이 DB에서 돌지 않는다 — 모두 dev 기준으로.

새 RDS를 만들 때(공개 접근 · 파라미터 그룹 · `harven_app` 계정 만들기)는 [`Shared/Database/mysql/README.md`](Shared/Database/mysql/README.md).

### DB 문제 해결

| 원격 콘솔에 보이는 것 | 원인 | 고치는 법 |
| --- | --- | --- |
| `[Env] …` 줄이 없다 | `harven.env`를 못 찾음 | 저장소 **루트**에 `harven.env`가 있는지(이름에 `.txt`가 붙지 않았는지) |
| `Missing DB setting: HARVEN_DB_PASSWORD …` | 그 변수가 비었다 | `harven.env`를 채우고 **저장** |
| `Can't open lib 'MySQL ODBC … Unicode Driver'` · `[IM002]` | 드라이버가 없거나 이름이 다르다 | `setup_wsl.sh`를 다시(드라이버 설치 · 이름 고침) · `odbcinst -q -d` |
| `Can't connect to MySQL server on '…:3306' (110)` | 보안 그룹이 내 IP를 막음 | [3. 보안 그룹](#3-rds-보안-그룹에-내-ip) |
| `Access denied for user '…'@'내 IP'` | 계정 · 비밀번호가 틀림 | 팀에서 다시 받기(복사할 때 앞뒤 공백 주의) |
| `SSL connection error` · 인증서 오류 | CA 파일이 없거나 경로가 틀림 | `setup_wsl.sh`를 다시 · `HARVEN_DB_CA` |
| `DB Connect Success` 뒤 `PROCEDURE … P_… does not exist` | RDS 스키마가 옛 판 | [DB 담당이 할 일](#db-담당이-할-일--스키마) |
| `[Phase5] 결사 · 경매장 DB 불러오기 실패` | 위 오류들의 결과 | 위의 원인을 고치면 사라진다(서버는 계속 뜨지만 결사 · 경매는 꺼진다) |

## 문제 해결

| 증상 | 원인 | 고치는 법 |
| --- | --- | --- |
| "C++ IntelliSense 정보가 최신 상태가 아닐 수 있습니다" · `CMake 생성에 실패했습니다` · 구성이 `x64 Debug` | 대상이 `로컬 컴퓨터`(Windows MSVC로 구성 — 리눅스 전용이라 실패) | 대상을 `WSL: Ubuntu-24.04`로 → 알림 줄 `캐시 삭제 및 다시 생성` |
| CMake 출력에 `Could not find a package configuration file provided by "Boost"` | WSL에 의존성이 없다 | `setup_wsl.sh` |
| F5 → "대상 'GameServer'을(를) 찾을 수 없습니다. launch.vs.json…" | 이 프리셋의 CMake 구성이 아직 안 끝났다 | 출력(CMake)에 `CMake 생성이 완료되었습니다`가 뜬 뒤 시작 항목을 다시 고르고 F5 |
| 구성 목록에 `linux-*`가 없다 | 대상이 Windows · CMakePresets 사용 꺼짐 | 대상 WSL · `도구 → 옵션 → CMake → 일반`에서 CMakePresets.json 사용 |
| 대상 목록에 WSL이 없다 | VS 워크로드 없음 | VS Installer → "Linux 및 Mac에서 C++를 사용한 개발" |
| 구성 직후 빨간 밑줄(예: `thread`는 형식 이름이 아닙니다) | WSL 헤더를 아직 다 읽지 않음 | 잠시 기다리면 사라진다. 계속되면 VS 재시작 |
| 출력 창 · 원격 콘솔이 안 보인다 | 아래 도구 창이 접혀 있다 | `보기 → 출력` · 경계를 위로 끌어올리기 · `창 → 창 레이아웃 다시 설정` |
| `.sh`가 `$'\r': command not found` | CRLF로 받은 셸 스크립트 | `.gitattributes`가 LF로 받게 한다 — `git checkout -- <파일>`로 다시 받기 |
| WSL `~/HARVEN-SERVER`에서 고친 것이 사라졌다 | 빌드 때마다 Windows 원본으로 덮어쓴다 | 고치는 곳은 Windows 원본만 |
| 클라가 `127.0.0.1:4000`에 안 붙는다 | 서버가 안 떴거나 포트가 다르다 | 원격 콘솔의 `[Server] All 16 zone threads are ready.` · `--port` 확인 |
| 서버가 바로 종료(`[GameData] … ERROR …`) | 기획 데이터가 틀렸다 | 찍힌 위치(`Monster!14 name`)를 고친다 — [기획 데이터](#기획-데이터) |

## 기획 데이터

서버는 시작할 때 `Bin/GameData/`(실행 파일 옆 `GameData/`, 없으면 위로 올라가며 `Bin/GameData/`)를 읽는다. 틀린 데이터면 위치(`Monster!14 name`)를 모두 찍고 뜨지 않는다.
원본은 구글 드라이브 `HARVEN_DATA` 폴더의 시트(**표 하나 = 스프레드시트 파일 하나**) — `datatool sync` → `Bin/GameData/*.json`(git) → 서버 부팅 때 불변 스냅샷.

- `--data-dir <폴더>` · `--check-data <폴더>`(검증만) · `--dump-data <폴더>` · `--dev`(값만 바뀐 판을 다시 읽기 — 개발 서버)
- 시트 → GameData는 [DATA_PIPELINE](Shared/Document/DATA_PIPELINE.md) · 검증 규칙 [DATA_RULES](Shared/Document/DATA_RULES.md)

## 시험

- **동시성** — `cmake --build --preset linux-memory --target ConcurrencyTests` → `build/linux-memory/Server/HARVEN_GameServer/tests/ConcurrencyTests`
  (`--bench`면 SList 방식 · 뮤텍스 · malloc과 성능 비교). `linux-tsan`으로도 돌린다.
  처치 보상 나누기 `--target PartyRuleTests`(무작위 20만 번씩 — 비례 나눔 · 쩔 보장 · 모르는 사람 + 파티),
  처치 전리품 굴림 `--target LootRuleTests`(필드 장비 등급 · 천장 · 직업 무기 바꾸기 · 피해 가중 추첨 · 레벨 차 · 정예 · 보스 자격 · 보스 굴림),
  개인 업적 `--target AchieveRuleTests`(통계 → 달성 · 받음 · 업적 절 왕복 · 진행 기록 판 4 왕복 · 판 3 → 업적 빈 채 · 크기 상한).
- **회귀** — 인메모리 서버를 띄우고 `Tools/Test/smoke_login.py` · `regress_session.py`(로그인 · 세션 · 중복 접속 · 옛 패킷) · `regress_zone.py`(Zone 경계 — `linux-debug` · `linux-asan`이면 소유 Zone assert까지).
  파티 · 처치 보상은 `regress_party.py`가 서버를 직접 띄운다(관리 · 끊긴 파티원 · 모집 게시판 · 상태 공유 · 경험치 나눔과 쩔 보장 · 모르는 사람끼리 피해 비례 경험치 · 그로시 ·
  막타 자동 획득 · 희귀도 시스템 채팅 · 넘친 전리품 줍기 우선권 · 때린 모두의 퀘스트 — [PARTY](Shared/Document/PARTY.md)).
  파티 · 결사의 가장자리(나간 뒤 다시 만들기 · 들어가기 · 신청 · 순번 계약 · 초대한 사람이 나감 · 다시 접속 · 중복 접속 · 결사 탈퇴 · 추방 · 해산 뒤 다시 ·
  가입 신청 · 나갔다 들어와도 한도)는 `regress_social_edge.py`가 서버를 직접 띄운다(PARTY 9장 · GUILD 7장).
  경제 1차(가방 50칸 · 제작 · 분해 · 경제 로그)는 `regress_craft.py`가 서버를 직접 띄운다(두 쪽 소지품 목록 · 재료 작은 묶음부터 · 수수료 · 분해 묶음 합치기 · 거래 이력 절반 ·
  강화와 서로 Busy · DB 반영 실패 · `/economy` · economy.log — [ECONOMY](Shared/Document/ECONOMY.md) 15절).
  경제 1차 처치 보상은 `regress_economy.py`가 서버를 직접 띄운다(필드 장비 · 희귀 천장 · 정수 · 결정 · 레벨 차 · 정예 표시 · 배율 · 자격자 보상 · 보스 개인 굴림 ·
  귀속 경매 · 버리기 거부 · 거래 이력 bit 9 · 경제 로그 — [ECONOMY](Shared/Document/ECONOMY.md) 23절).
  개인 업적은 `regress_achieve.py`가 서버를 직접 띄운다(Summary 소급 · 처치 · 정예 · 보스 · 파티 · 쓰러짐 · 장비 · 정수 · 그로시 · 제작 · 분해 · 강화 · 퀘스트 · 결사 ·
  경매 판매 · 구매 · 교환 · 지역 16곳 · List 쪽 · 분야 · TooFast · Claim · ClaimAll · BagFull · 지갑 상한 · Busy · Duplicate · 다시 접속 — [ACHIEVEMENT](Shared/Document/ACHIEVEMENT.md) 8장).
- **부하** — 서버를 `--load-test`로 띄운다(새 캐릭터 · 부활을 Zone들에 흩어 세운다).
  봇은 `cmake --build --preset linux-memory --target StressTest` → `StressTest <초> [csv]`(포트는 환경 변수 `HARVEN_STRESS_PORT`, 최대 봇 수는 CMake `HARVEN_STRESS_MAX_BOTS`).
  전/후 비교는 서버를 CPU 0, 봇을 CPU 1에 묶고(`taskset`) 같은 봇 수에서 서버 CPU 시간 · 문맥 전환 · 지연을 본다.
  클라 · 봇 · 녹화 쪽 샌드박스는 HARVEN-CLIENT [tools/sandbox](https://github.com/SJKIM99/HARVEN-CLIENT/tree/raylib-v1/tools/sandbox).
- **크래시** — 작업 폴더의 `crash.log`에 신호 · 호출 스택 · 메모리가 남는다(줄 번호는 `addr2line -e GameServer <주소>`).
- **알려진 문제** — 부팅 직후(Zone이 몬스터를 넣는 중) 접속이 몰리면 드물게 섹터 널 읽기 크래시가 났다. 원인 하나(로그인이 Zone 등록보다 먼저 게임 중이 되던 것)는
  고쳤지만 그것뿐인지 확정하지 못했다 — 서버를 띄우고 몇 초 뒤 접속하면 피할 수 있다. `linux-asan` · `linux-tsan`에서 부팅 직후 접속 25회가 0이면 지운다.

## 저장소와 작업 폴더

하르벤은 네 저장소로 나뉩니다(서버는 브랜치 `dev`, 나머지는 `raylib-v1`). 작업 폴더 하나(예: `C:\HARVEN`)에 아래처럼 둡니다.

| 저장소 | 담는 것 | 자리 |
|---|---|---|
| [HARVEN-SERVER](https://github.com/SJKIM99/HARVEN-SERVER) | 이 저장소 — 리눅스 게임 서버(`Server/HARVEN_GameServer`) · 엔진 코드(`Libs`) · 프로토콜 · DB 스키마(MySQL) · 문서(`Shared`) · 기획 데이터(`Bin/GameData`) · 도구(`Tools`) | 작업 폴더 |
| [HARVEN-CLIENT](https://github.com/SJKIM99/HARVEN-CLIENT) | raylib 2D 클라이언트 「하르벤」 · 샌드박스 시험 도구 · 고도화 기록 | 작업 폴더 |
| [HARVEN-DESIGN](https://github.com/SJKIM99/HARVEN-DESIGN) | 기획 — 세계관 · 디자인 시스템 · 컨텐츠 · 레벨 설계와 마을 원본 | 클라 안 |
| [HARVEN-ART](https://github.com/SJKIM99/HARVEN-ART) | 그림 · 소리 · 글꼴과 생성 도구 | 클라 안 |

```
C:\HARVEN\
├─ HARVEN-SERVER\            이 저장소(CMake — VS + WSL에서 빌드)
└─ HARVEN-CLIENT\            클라(CMake)
   ├─ HARVEN-DESIGN\
   └─ HARVEN-ART\
```

```bat
cd C:\HARVEN
git clone -b dev https://github.com/SJKIM99/HARVEN-SERVER.git
git clone -b raylib-v1 https://github.com/SJKIM99/HARVEN-CLIENT.git
cd HARVEN-CLIENT
git clone -b raylib-v1 https://github.com/SJKIM99/HARVEN-DESIGN.git
git clone -b raylib-v1 https://github.com/SJKIM99/HARVEN-ART.git
```

- 서버는 **이 저장소만으로** 빌드됩니다. 클라는 이 저장소의 공유 헤더(`Shared/Protocol/Protocol.h` · `Server/HARVEN_GameServer/Database/GameData/*.h`)를 그대로 include하므로
  (`../HARVEN-SERVER`) 패킷 · 게임 데이터 형을 바꾸면 클라도 다시 빌드합니다. 기존 패킷은 바꾸지 않고 새 타입은 끝에만 붙입니다.
  지금 raylib 클라는 SERVER `64edda8`(시트 이관 · 폴더 재배치 전)에 고정해 빌드합니다 — 새 판으로 옮기는 법은 프로젝트 요청서 SERVER-001 · SERVER-002.
- 마을 격자 · 배치(`Bin/GameData/Town*.json`)는 생성물입니다 — 원본과 생성기는 HARVEN-DESIGN `tools/mapgen`. 새로 만든 파일은 여기서 커밋합니다.

## 폴더 구조

실무 프로젝트(REIW)의 저장소 셋(server · server-libs · shared)을 한 저장소 안 폴더로 둡니다.

```
HARVEN-SERVER/
├─ CMakeLists.txt · CMakePresets.json   리눅스 빌드(프리셋 linux-release · linux-debug · linux-debug-memory · linux-memory · linux-asan · linux-tsan)
├─ harven.env.example         로컬 설정 견본 — harven.env로 복사(클라우드 DB 접속 정보 · git 밖)
├─ Bin/
│  └─ GameData/               시트에서 내보낸 기획 데이터 JSON(datatool · mapgen이 쓴다 — 손으로 고치지 않는다)
├─ build/<프리셋>/            빌드 출력(GameServer · StressTest · 시험 — git 밖)
├─ Server/                    ← reiw-server-dev/Server
│  └─ HARVEN_GameServer/      GameServer.cpp(main) · pch · GameServer_DefType.h(공유 포인터 별칭)
│     ├─ Base/                GameObject · GameObjectManager · ServerGlobal
│     ├─ Content/             ContentHelper · ServiceCommon
│     │  ├─ Auction/ · Guild/ 결사 · 경매장(단일 스레드 서비스)
│     │  ├─ Party/            파티 · 모집 게시판(단일 스레드 서비스 + Zone 쪽 상태 모으기)
│     │  ├─ Buff/ · Combat/ · Skill/ · Stat/
│     │  ├─ Chat/ · Npc/ · Quest/
│     │  ├─ Field/            Zone · ZoneManager · Sector(시야)
│     │  ├─ Inventory/(Item/) · Reward/(DropTable · DamageLedger — 몬스터가 받은 피해 · RewardHelper — 처치 보상 나눔 · 자동 획득 · 희귀도 알림)
│     │  └─ Navigator/        AStar · Collision
│     ├─ Database/            DBThread · GameData/(표 파사드 · EnchantRules · Load 로더 · Gen 생성 헤더 · Item 표)
│     ├─ Entity/              Transform · Subject/(Subject · Monster/)
│     ├─ User/                User · UserHelper
│     ├─ Worker/              GameLogicThread(Zone · 서비스 메일함) · TimerThread · WorkerThread(IO 스레드) · Route/(패킷 처리 — _영역.inl의 OnRecv_패킷이름)
│     └─ tests/               동시성 시험(concurrency_tests.cpp — lock-free 스택 · 풀 · 메일함)
├─ Libs/                      ← reiw-server-libs-dev
│  ├─ Base/Core · Base/ObjID  CoreGlobal · CoreTLS · Types(정수 별칭) · LockFreeStack · ObjectPool · CrashHandler · LocalEnv(harven.env) / ObjID · ContentID
│  ├─ Database/               DBConnection · DBConnectionPool(unixODBC — MySQL)
│  ├─ Network/                SocketManager(io_context 풀) · GameSession(Boost.Asio) · GameSessionManager
│  ├─ WorkerThread/           ThreadManager · SafeMessageQueue
│  └─ ThirdParty/             nlohmann/json.hpp(MIT) · moodycamel/(ConcurrentQueue · LightweightSemaphore — BSD)
├─ Shared/                    ← reiw-shared-dev  서버 · 클라 · 도구가 같이 보는 것
│  ├─ EnumCategory.h · Protocol/Protocol.h   패킷 정의 하나(레거시 세션 없앰, 번호는 값으로 고정)
│  ├─ Database/mysql/         MySQL 스키마 · 프로시저(setup.sql) · 마이그레이션 · 점검(schema_precheck · schema_verify) · RDS 안내
│  ├─ ToolsConfig/DataTool.json  표 → 스프레드시트 파일 id
│  └─ Document/               DATA_PIPELINE · DATA_RULES(기획 데이터) · PARTY(파티) · Portfolio/(IOCP 시절 포트폴리오)
└─ Tools/
   ├─ DataTool/               시트 ↔ GameData(datatool.py) · 스키마 · 검사기
   ├─ StressTest/             봇 부하 도구(epoll — 1초마다 수치)
   ├─ Sandbox/                인메모리 DB(MemDBConnection.cpp — linux-memory 프리셋)
   ├─ Setup/                  setup_wsl.sh — WSL 개발 환경 한 번에(도구 · MySQL ODBC · RDS CA · harven.env)
   └─ Test/                   회귀 시험(smoke_login · regress_session · regress_zone)
```

include는 뿌리 셋(`Server/HARVEN_GameServer` · `Libs` · `Shared`) 기준 경로로 씁니다 — `#include "Content/Field/Sector.h"` · `"Network/GameSession.h"` · `"Protocol/Protocol.h"`.
파일 이름은 그대로 두고 그 안의 이름만 REIW식으로 씁니다(`User.h`의 `CUser`) — 아래 "코드 컨벤션".

## 코드 컨벤션

실무 프로젝트(REIW)의 이름 규칙을 서버(`Server/HARVEN_GameServer`) · 엔진(`Libs`) 코드에 씁니다. 공백은 `.editorconfig`(4칸 · LF · 끝 줄바꿈).

- 클래스는 `C` 접두사 — `CUser` · `CMonster` · `CGameSession` · `CZoneManager`. 데이터만 담는 구조체(`DB_USER_INFO` · 패킷 · `Listing`)는 그대로.
- 도우미 이름공간도 `C` — `CUserHelper` · `CSectorHelper` · `CItemHelper`. 패킷 처리 `RouteItem` · `RouteUser` …는 그대로.
- 멤버는 `m_` + 파스칼. 형 접두사는 포인터 · 열거형 · bool에만 — `m_pSession`(shared_ptr · unique_ptr · 날 포인터) · `m_wpOwner`(weak_ptr) · `m_eState`(열거형) · `m_bEquipped`(bool), 나머지는 `m_PlayerId` · `m_Slots`. 전역 · 스레드 지역은 `G` · `L` 그대로(`GZoneManager` · `LCurrentZoneId`).
- 정수는 `Int8` … `UInt64`(`Libs/Base/Core/Types.h`). 서버 객체의 공유 포인터는 `UserSharedPtr` · `UserWeakPtr` · `SubjectSharedPtr` · `MonsterSharedPtr` · `ItemSharedPtr` · `SessionSharedPtr` …(`GameServer_DefType.h`).
- 클라와 같이 쓰는 헤더 — `Shared/**`(Protocol.h · EnumCategory.h) · `Database/GameData/**` · `Libs/Base/ObjID` — 는 이름 · 형을 바꾸지 않는다(클라가 그대로 include). `Database/GameData/Gen/`은 datatool 생성물이라 들여쓰기도 생성기 출력(탭) 그대로.
- 지역 변수 · 매개변수 이름은 이번에 바꾸지 않았다.

## 관련 문서

- 포트폴리오 원본 PDF — [MMORPG-with-IOCP_포트폴리오.pdf](Shared/Document/Portfolio/MMORPG-with-IOCP_포트폴리오.pdf)
- 파티 · 모집 게시판 — 생성 · 초대 · 탈퇴 · 추방 · 위임 · 파티 채팅 · 상태 공유 — [PARTY](Shared/Document/PARTY.md)
- 처치 보상 — 때린 모두(모르는 사람끼리도)에게 그로시 · 경험치 피해 비례(파티 몫 안에서 쩔 보장) · 아이템은 막타 자동 획득 ·
  희귀 이상 서버 전체 시스템 채팅(시트 `Rarity.loot_notice`) · 넘친 전리품 줍기 우선권(자신 > 파티원 > 누구나) · 때린 모두의 퀘스트 처치 — [PARTY](Shared/Document/PARTY.md)
- 개인 업적 — 서버가 세는 18가지(처치 · 정예 · 보스 · 파티 · 레벨 · 장비 · 강화 · 제작 · 퀘스트 · 결사 · 경매 · 교환 · 지역 …) · 달성 알림 · 받기 · 진행 기록 판 4 — [ACHIEVEMENT](Shared/Document/ACHIEVEMENT.md)
- 시연 영상 — [youtube.com/watch?v=BjdfZrlOad4](https://www.youtube.com/watch?v=BjdfZrlOad4)
- 2D MMORPG 클라이언트 「하르벤」 — [HARVEN-CLIENT](https://github.com/SJKIM99/HARVEN-CLIENT) · 서버 컨텐츠 확장(종족·보스, 그로시·상점·각인 강화, 마을·사망 패널티, PK·성향) — [HARVEN-DESIGN/LINEAGE_CONTENT.md](https://github.com/SJKIM99/HARVEN-DESIGN/blob/raylib-v1/LINEAGE_CONTENT.md)
- 세계관 · 이름 규칙 — [HARVEN-DESIGN/WORLD.md](https://github.com/SJKIM99/HARVEN-DESIGN/blob/raylib-v1/WORLD.md) · 디자인 시스템 「새김」 — [HARVEN-DESIGN/DESIGN_SYSTEM.md](https://github.com/SJKIM99/HARVEN-DESIGN/blob/raylib-v1/DESIGN_SYSTEM.md)
- 고도화 기록(HARVEN-CLIENT `docs/upgrade`) — 조작 · 이동 [PHASE_1](https://github.com/SJKIM99/HARVEN-CLIENT/blob/raylib-v1/docs/upgrade/PHASE_1.md) · 기술 시스템 · 전투 연출 [PHASE_2](https://github.com/SJKIM99/HARVEN-CLIENT/blob/raylib-v1/docs/upgrade/PHASE_2.md) · 직업 36기술 움직이는 그림 [HARVEN-DESIGN/skills](https://github.com/SJKIM99/HARVEN-DESIGN/blob/raylib-v1/skills/README.md) ·
  창 시스템 · HUD · 상시 채팅창(서버 채널 채팅 `CS_CHAT` — Zone 소유 스레드 도배 막기 · 이름 색인 귓속말) [PHASE_3](https://github.com/SJKIM99/HARVEN-CLIENT/blob/raylib-v1/docs/upgrade/PHASE_3.md) ·
  마을 3배 · 높낮이(층 0~4 — 서버 충돌 격자 그대로, 강제 이동 대각 규칙) · 레벨디자인 · 건물 19종 [PHASE_4](https://github.com/SJKIM99/HARVEN-CLIENT/blob/raylib-v1/docs/upgrade/PHASE_4.md) · 설계도 [LEVEL_DENICA](https://github.com/SJKIM99/HARVEN-DESIGN/blob/raylib-v1/LEVEL_DENICA.md) ·
  결사(길드) · 경매장 — 전용 단일 스레드 서비스(액터) + 정산함 · 거래 하나 = 샤드 하나 = 트랜잭션 하나 · 경합 · 멱등 · 장애(강제 종료 뒤 총량 보존) [PHASE_5](https://github.com/SJKIM99/HARVEN-CLIENT/blob/raylib-v1/docs/upgrade/PHASE_5.md) ·
  통합 QA · 폴리싱 — 접속부터 경매까지 한 편 녹화(클라 README 대표 영상) · 붐비는 마을 · 대규모 전투 성능 · 일관성 자동 점검 · 마지막 자기 검토(읽히는 싸움) [PHASE_6](https://github.com/SJKIM99/HARVEN-CLIENT/blob/raylib-v1/docs/upgrade/PHASE_6.md)
- 기획 데이터 — 스킬 · 아이템 · 레벨 · 몬스터 · NPC대사 · 퀘스트 · 기술성장 · 마을 · 지형의 표는 구글 드라이브 `HARVEN_DATA` 폴더의 시트가 원본이다(**표 하나 = 스프레드시트 파일 하나**, `Monster_Table` …).
  `datatool sync` → `Bin/GameData/*.json`(git) → 서버 부팅 때 불변 스냅샷 → 기존 파사드. 개발 서버 `--dev`는 시트를 고치면 다시 읽는다 —
  [DATA_PIPELINE](Shared/Document/DATA_PIPELINE.md) · 검증 규칙 [DATA_RULES](Shared/Document/DATA_RULES.md)
- 게임 그림 · 목소리 — 캐릭터 · 몬스터 · NPC · 배경 · 소품 · 아이콘 · 효과 · UI · 대사 음성을 Higgsfield로 생성 — [HARVEN-ART](https://github.com/SJKIM99/HARVEN-ART) · 규칙 [디자인 시스템 9장](https://github.com/SJKIM99/HARVEN-DESIGN/blob/raylib-v1/DESIGN_SYSTEM.md#9-게임-그림--higgsfield)
