# GUILD — 결사 성장(레벨 · 기여도 · 주간 임무 · 게시판 · 기록 · 직위 권한 · 금고 · 상점 · 업적 · 출석 · 모집)

> 코드: `Server/HARVEN_GameServer/Content/Guild/`(GuildService — 기존 결사 · GuildGrowthService — 성장 · GuildProgress — 저장 · 규칙 적용) ·
> 규칙 `Database/GameData/GuildGrowth.h` · 로더 `Database/GameData/Load/Bind_Guild.cpp` ·
> 튜닝 값 시트 `GuildConst` · `GuildLevel` · `GuildMission` · `GuildShop` · `GuildAchievement` · `GuildTitle` · `GuildAttend`(→ `Bin/GameData/Guild*.json`) ·
> 패킷 `Shared/Protocol/Protocol.h`(108~111) · DB `Shared/Database/mysql/setup.sql`(GuildProgress · GuildLog · GuildPost) · `migrate_guild_growth.sql` ·
> 시험 `Tools/Test/regress_guild.py` · `tests/guild_rule_tests.cpp` · `Tools/DataTool/d2/guild.py`(R117~R123).
> 기존 결사(Phase 5 — 세우기 · 초대 · 직위 · 공지 · 문장 · 해산, 패킷 89~92)는 그대로 두고 그 옆에 붙였다.

## 1. 한눈에

| 기능 | 어떻게 |
|---|---|
| **결사 레벨 · 경험치** | 결사원이 몬스터를 쓰러뜨리면 몬스터 경험치의 `kill_exp_pct`%가 결사 경험치(한 마리에 한 번 — 함께 때린 결사원끼리 나눈다). 한 사람이 하루에 올려 줄 수 있는 양은 `member_daily_exp_cap`(작업장 · 부캐 몰아주기 막기). 임무 · 업적 · 출석도 경험치를 준다. `GuildLevel`의 누적 경험치로 레벨 |
| 레벨 보상 | 인원 한도(`max_members` — 1레벨 30 → 10레벨 55) · 문장 테두리(`frame` 0~3) · 칭호(`title_id`) · 상점 물건(`GuildShop.min_level`) · 임무 풀(`GuildMission.min_level`) |
| **기여도** | 처치마다 `kill_contrib`(보스 `boss_contrib`) · 협력 처치 덧 `coop_contrib_bonus` · 임무 달성 `contrib` · 출석 단계 `contrib`. **이번 주**(주 넘김에 0)와 **누적**. 결사원 명단에서 기여도 · 가입일 · 이번 주 활동(분)을 본다(`MemberStats` — 정렬 4가지) |
| **주간 임무** | 결사마다 한 주에 `mission_slots`(3)개를 무게대로 뽑는다(결사 번호 · 주 번호로 정해져 서버를 다시 켜도 같다). 종류: 처치(최소 몬스터 레벨) · 보스 · 수집(아이템 / 강화 재료) · 장비(최소 희귀도) · **활동**(N명이 저마다 M분 넘게) · 의뢰. 달성하면 경험치 · 결사 인장 · 참여자 기여도, 서로 다른 결사원이 `mission_bonus_members` 이상 참여했으면 `mission_bonus_pct`% 덧보상 |
| **공지 · 게시판 · 기록** | 공지(기존 `GuildOp::Notice`) · 가입 안내(`Intro`, 40자) · 게시판(공지 · 모집 · 공략 · 아이템 · 자유 — 제목 10자 · 본문 50자, 고정, `post_max`를 넘으면 고정하지 않은 가장 오래된 글이 내려간다) · **결사 기록**(들어옴 · 나감 · 추방 · 직위 · 결사장 · 위임 청구 · 레벨 · 임무 · 업적 · 출석 · 금고 입출 · 권한 · 칭호 · 공지 — 거르기 넷) |
| **직위 · 권한** | 직위(결사장 · 부관 · 정예 · 결사원)는 그대로, 권한은 결사마다 **권한표**(초대 · 신청 승인 · 추방 · 공지 · 게시판 관리 · 직위 바꾸기 · 상점). 결사장만 바꾸고 결사장은 늘 전부. 기본값은 시트(`perm_officer` · `perm_elite` · `perm_member`) = 예전 고정 규칙(정적 검사로 확인). 추방 · 직위는 권한에 더해 자기보다 낮은 직위에게만 — **서버가 판정** |
| **결사장 위임 청구** | 결사장이 `leader_absent_sec`(14일) 넘게 들어오지 않으면 결사장을 뺀 가장 높은 직위가 청구 → `claim_grace_sec`(3일) 기다림 → 결사장이 된다. 그 사이 결사장이 들어오면 취소. 청구 · 넘김 · 취소는 기록 · 알림 |
| **금고 · 결사 상점** | 결사 인장은 결사 금고에 쌓인다(임무 · 업적 · 출석). 상점 권한이 있는 결사원이 인장으로 산다 — 레벨 조건 · 사람마다 주 한도(`weekly_limit`). 회복 소모품 · 귀환/옛길 두루마리 · 각인 두루마리 · 강화권 — **거래되는 최상급 장비 · 강한 전투 버프는 넣지 않았다**(결사 크기 격차 · 부캐 악용) |
| **업적 · 칭호** | 누적 처치 · 보스 · 임무 달성 · 협력 처치 · 세운 뒤 날 · 출석한 날 · 레벨 · 인원. 이루면 인장 · 경험치 · 칭호. 결사장이 열린 칭호 하나를 고른다(모집 목록 · 공개 정보에 보인다) |
| **출석 · 함께 플레이** | **접속만으로는 세지 않는다** — 하루 `attend_active_min`분 넘게 *활동*(그 1분 안에 몬스터를 함께 쓰러뜨림)해야 출석. 하루 출석 수가 `GuildAttend` 단계에 닿으면 인장 · 경험치 · 출석자 기여도. 신규 결사원(`coop_new_days`)과 함께 쓰러뜨리면 **협력 처치**(기여도 덧 · 업적) |
| **모집 · 가입 신청** | 결사장(공지 권한)이 가입 안내 · 모집 중 · 최소 레벨을 정하면 결사 없는 사람이 모집 목록(레벨 높은 순) · 공개 정보를 보고 신청 → 승인 권한이 있는 결사원이 승인 · 거절. 한 사람에 신청 하나 · 결사마다 `apply_max` · `apply_expire_sec`에 사라진다(신청은 DB에 남기지 않는다). 다른 결사에 들어가거나 **스스로 세우면** 신청은 거둬지고, 신청한 결사가 해산하면 신청자에게 `ApplyResult` 2(만료와 같다) |
| **나갔다 다시 들어오면** | 기여도 · 가입일은 새로(결사의 것 — 나가면 사라진다). 다만 **오늘 · 이번 주 한도 몫은 잇는다** — 같은 날이면 하루 경험치(`member_daily_exp_cap`) · 오늘 활동 · 출석, 같은 주면 상점 구매(`weekly_limit`). 나간 사람의 몫은 서비스 메모리에만(주가 넘어가거나 서버를 다시 켜면 사라진다) |

## 2. 저장 — 진행 덩어리 · 금고 · 기록 · 게시판

| 표 | 무엇 | 쓰는 때 · 샤드 |
|---|---|---|
| `GuildProgress.Data` | 레벨 · 경험치 · 칭호 · 모집 설정 · 권한표 · 누적 기록 · 업적 · 이번 주 임무(참여자 몫) · 오늘 출석 · 위임 청구 · 결사원마다(가입일 · 기여 · 활동 · 오늘 경험치 · 이번 주 구매) — **버전 있는 바이너리**(`GuildModel::Serialize`, 60명이어도 5KB 아래 · 칸 16000) | 바뀌면 dirty → **10초 모아** 결사 번호 샤드(설정 · 권한 · 청구 · 들어옴 · 나감은 다음 틱에 바로). 저장 중이면 겹쳐 보내지 않는다 |
| `GuildProgress.Seals` | 금고(결사 인장) — **더하고 빼는 식으로만** | 보상: `P_GUILD_SEALS_ADD`(결사 번호 샤드 — 기록 한 줄과 한 트랜잭션). 상점: `P_GUILD_SHOP_BUY`(사는 사람 샤드 — 소지품 한 칸 · 금고 − · 기록이 한 트랜잭션). 차례가 바뀌어도 합이 맞는다 |
| `GuildLog` | 결사 기록(결사마다 최근 `log_keep`줄 — 켤 때 넘친 줄을 지운다) | 결사 번호 샤드(결과를 기다리지 않는다) |
| `GuildPost` | 게시판 글 | 결사 번호 샤드 — DB가 커밋되면 메모리에 |

메모리가 기준이다(서비스 스레드 하나 — 락 없음). 금고는 **DB를 먼저** 하고 커밋되면 메모리에 더한다(상점은 사는 동안 그 몫을 묶어 둔다 — `sealsReserved`).
진행 덩어리는 길게는 10초까지 잃을 수 있다(서버가 죽으면) — 경험치 · 기여 · 임무 진행 몇 초. 돈 같은 것(인장 · 소지품)은 잃지 않는다.
결사를 해산하면 세 표가 함께 지워진다(FK `ON DELETE CASCADE`). 예전 결사(진행 줄이 없음)는 1레벨 · 금고 0에서 시작하고, 예전 결사원의 가입일은 결사를 세운 날.

**운영 DB에 적용**: 서버를 멈추고 백업 → `migrate_guild_growth.sql`(여러 번 돌려도 된다) → `schema_verify.sql`이 `ok = 1`. 새 DB는 `setup.sql`만.

## 3. 스레드 · 소유권

- **사냥 훅**: 몬스터가 쓰러지면 그 Zone 스레드(`CRewardHelper::HandleKill`)가 피해를 준 사람 중 결사원을 **결사마다 묶어** 서비스에 `Post` 하나(`CGuildHelper::OnKill`).
  전리품(자동 획득) · 퀘스트 완료도 같은 식(`OnLoot` · `OnQuestDone`). 결사가 없는 사람은 원자 읽기 하나로 건너뛴다(부하 시험 봇은 결사가 없다).
- **규칙**: `GuildModel::OnKill` · `OnLoot` · `OnQuest` · `Roll`(주 · 날) · `CheckAchievements`가 진행을 바꾸고 **결과(Effects)** 를 돌려준다 — 금고 ± · 레벨 · 임무 · 업적 · 출석 · 칭호 · 기여.
  서비스가 그것을 DB · 기록 · 알림으로 옮긴다(`ApplyEffects`). 규칙 함수는 네트워크 · DB · 시각 읽기가 없어 단위 시험이 바로 부른다.
- **틱(1초)**: 주 · 날 넘김(`PeriodId` — 시트 `week_period_sec` · `week_offset_sec`, 기본 월요일 06:00 KST · 날 06:00) · 위임 청구 · 신청 만료 · 임무 진행 알림(결사마다 2초에 한 번 모아) · 기여 막대(Pulse — 3초) · 저장.
- **요청**: 소유 Zone이 멱등(순번) · 묻기 간격(250ms) · 글 쓰기 간격(5초) · 글 검사(UTF-8 · 글자 수 · 금칙어 — `GuildGrowth::CheckText`, 클라도 같은 함수)를 먼저 거르고 서비스 큐로.
- **상점 구매**: 서비스가 권한 · 레벨 · 주 한도 · 금고(묶인 몫 제외)를 보고 인장을 묶음 → 사는 사람의 Zone에서 소지품 칸을 잡고(`PlaceIncoming`) 사람 샤드 트랜잭션 → 실패면 칸을 되돌리고 묶음을 푼다.

## 4. 기획 데이터 — 7표(드라이브 `HARVEN_DATA`, 표 하나 = 파일 하나)

| 표 | 행 | 내용 |
|---|---|---|
| `GuildConst` | 1 | 주 · 날 경계, 처치 → 결사 경험치 %, 하루 한도, 처치 · 보스 · 협력 기여도, 새 결사원 날 수, 출석 활동 분, 임무 수 · 덧보상, 위임 청구(부재 · 기다림), 게시판 글 수, 기록 줄 수, 신청 만료 · 수, 직위 권한 기본값(`flags:GuildPerm`) |
| `GuildLevel` | 10 | 레벨 · 누적 경험치 · 인원 한도 · 문장 테두리 · 여는 칭호 |
| `GuildMission` | 14 | 임무 풀(id 영구) — 이름 · 종류(`enum:GuildMissionKind`) · 목표 · sub · 최소 결사 레벨 · 경험치 · 인장 · 기여도 · 무게 |
| `GuildShop` | 10 | 상점(id 영구) — 아이템 · 수량 · 값(인장) · 최소 결사 레벨 · 사람마다 주 한도 |
| `GuildAchievement` | 14 | 업적(id 영구) — 이름 · 무엇을 세나(`enum:GuildAchKind`) · 목표 · 인장 · 경험치 · 칭호 |
| `GuildTitle` | 11 | 칭호(id 1~31 영구) |
| `GuildAttend` | 3 | 출석 단계 — 출석 수 · 인장 · 경험치 · 출석자 기여도 |

시트 파일 `HARVEN_DATA/Guild*_Table` 일곱(2026-10-12 — Drive 커넥터로 만들어 `Shared/ToolsConfig/DataTool.json`에 등록 → `datatool push --tabs Guild…`로 심었다 —
서식 · 드롭다운 · 보호 · 이름 보조 열. 곧바로 `sync --to-sheet`: 표 50 · 오류 0 · 바뀐 파일 0 = 시트와 `Bin/GameData`가 같다).
검증 규칙 R117~R123(`Shared/Document/DATA_RULES.md`) · D2 21사례. 임무 · 상점 · 업적 · 칭호 id는 결사 진행에 저장되므로 지우지 말고 `deprecated` 열로 거둔다.
시트를 고치면 `datatool sync`(검증 → `Bin/GameData`) → 서버 `--dev`면 핫 리로드, 아니면 다시 켠다.

## 5. 패킷(108~111) — 묶음 패킷 + op, 끝에만 추가

- `GUILD_EX_REQ`(215B): op · seq · arg8 · arg32 · target(이름) · title(31B) · body(151B). op 23가지(`GuildExOp` — 개요 · 임무 · 기여도 · 기록 · 게시판 · 글 읽기/쓰기/내리기/고정 · 가입 안내 · 모집 목록 · 공개 정보 · 신청 · 거두기 · 신청자 · 승인/거절 · 권한 · 칭호 · 상점 · 사기 · 업적 · 칭호 목록 · 위임 청구).
- `GUILD_EX_ACK`(13B): op · result(`GuildExResult` 18가지) · seq · param.
- `GUILD_EX_INF`: kind 12가지(`GuildExInfKind`) — Overview(128B — 레벨 · 경험치 · 금고 · 권한표 · 출석 · 내 기여 · 누적 기록 · 위임 청구 · 청구가 열리기까지) · Missions · MemberStats · Log · Board · Post · Public · Recruit · Applicants · Shop · Achievements · Titles. 모두 255B 이하.
- `GUILD_EX_NFY`(63B): event 18가지(`GuildExEvent`) — 레벨업 · 임무 진행 · 임무 달성 · 업적 · 출석 단계 · 금고 · 구매 · 새 글 · 신청 · 신청 결과 · 청구 시작/취소/완료 · Pulse · 칭호 · 권한 · 가입 안내 · 칭호 열림.
- 기존 결사 패킷(89~92)은 바이트 하나 바뀌지 않았다 — 고정 판 클라도 그대로 붙는다(인원 한도 · 권한 판정만 서버에서 결사 레벨 · 권한표를 쓴다).
- **순번 계약**은 파티와 같다([PARTY.md](PARTY.md) 7장): `GUILD_REQ`(`m_LastGuildSeq`) · `GUILD_EX_REQ`(`m_LastGuildExSeq`) 창이 따로, 세션마다 1부터 늘린다 ·
  결사를 나가거나 해산해도 되돌리지 않는다 · 같은 순번 이하는 `Duplicate`이고 **param = 마지막으로 받은 순번**(처음 보낸 요청이 Duplicate면 `param + 1`로 다시).
  변경 op — `GUILD_REQ`: Info · Members · Name · Top을 뺀 모두 / `GUILD_EX_REQ`: 글 쓰기 · 내리기 · 고정 · 가입 안내 · 신청 · 거두기 · 승인/거절 · 권한 · 칭호 · 사기 · 위임 청구.

## 6. 예전 동작에서 바뀐 것

- **2026-10-13 감사**(`regress_social_edge.py` guild · gapply · glimit): 나갔다 다시 들어오면 하루 경험치 한도 · 출석 · 주 상점 한도가 0부터 다시 시작했다 → 잇는다 ·
  가입 신청을 내 둔 채 결사를 세우면 신청이 남았다 → 거둔다 · 해산한 결사의 신청자에게 알림이 없었다 → `ApplyResult` 2 ·
  나간 사람(탈퇴 · 추방 · 해산)에게 `GUILD_INF`만 비웠다 → `GUILD_EX_INF` Overview(결사 0)도 · `Duplicate` param 0 → 마지막으로 받은 순번 ·
  들어옴 · 세우기의 DB를 기다리는 사이 다시 접속하면 옛 세션이 결사원에 적혔다 → 지금 세션으로.

- **인원 한도**: 30 고정 → 결사 레벨(`GuildLevel.max_members`, 1레벨 30이라 예전과 같게 시작).
- **권한**: `GuildRule::CanInvite · CanKick · CanNotice · CanSetRank`(고정) → 결사마다 권한표. 기본값이 예전 규칙과 같다(`GuildGrowth.h` 정적 검사).
- 결사 사건 · 들어옴 · 나감 · 직위 · 넘김 · 공지 · 문장이 결사 기록에 남는다.

## 7. 시험

- `tests/guild_rule_tests.cpp`(`GuildRuleTests`): 진행 저장 왕복 3000번(결사원 0~60 · 깨진 덩어리는 터지지 않는다 · 가장 큰 덩어리 ≤ 16000B) · 임무 고르기(같은 결사 · 주 → 같은 임무, 겹침 없음, 조건) ·
  처치 규칙(둘이 한 마리 = 진행 1 · 덧보상 · 출석 2단계 · 하루 한도 · 협력 · 업적 · 칭호 · 레벨 · 주 넘김) · 권한 · 글 검사 — 27만 검사 실패 0.
- `Tools/Test/regress_guild.py`(인메모리 서버를 직접 띄운다): growth · perms · board · shop · recruit · claim · **persist**(서버를 다시 켜도 남는다) · **week**(주 60초 · 날 30초 서버) 9개.
- `Tools/Test/regress_social_edge.py` 결사 쪽: **guild**(탈퇴 → 다시 초대 · 추방 → 다시 · 스스로 세우기 → 해산 → 같은 이름으로 다시 · 나간 사람의 빈 개요 · 순번 Duplicate param · 다시 접속) ·
  **gapply**(신청 뒤 세우면 거둠 · 해산한 결사의 신청자에게 2) · **glimit**(하루 한도에 닿은 결사원이 나갔다 들어와도 결사 경험치가 늘지 않는다).
  `GuildRuleTests` TestRejoin — 같은 날 · 다음 날 · 다음 주 · 다른 사람의 몫.
  운영 빌드(ODBC) + MariaDB(TLS)로도 9개 모두 통과 — 기록 덧말 칸 길이 · 상점 CALL 자리표시자 수를 이것으로 잡았다.
- ASan+UBSan · TSan 빌드: `GuildRuleTests` 27만 검사 · `regress_guild.py` 9개 모두 통과, 새니타이저 보고 0.
- `datatool check --gamedata` · `verify`(58표) · `d2`(289사례 — 결사 21) · 기존 회귀 전부(smoke · session · inventory · zone · view A/B · monster_ai · party · player_skill A/B 189 시나리오 차이 0).
  party의 exp 시나리오는 한 번 처치 시간 제한에 걸렸다가 옛 서버 · 새 서버 각각 세 번 다시 돌려 모두 통과(부하 중 시간 문제 — 결사 변경과 무관).

## 클라(HARVEN-CLIENT `feature/guild-growth` — 클라 ADR 0012)

- 거울 헤더 `native/src/protocol/server_ext_guild.h`(파티와 같은 방식 — `tools/native/check_server_ext.py`가 이 헤더와 대조).
- 결사 창 탭 여덟: 정보(성장 · 출석 · 내 기여 · 결사 기록 · 위임 청구) · 구성원(명단 / 기여도 — 정렬 넷) · 임무 · 게시판 · 상점 · 업적/칭호 · 기록 · 관리(초대 · 공지 / 가입 안내 · 모집 · 신청자 / 권한표).
  결사가 없으면 모집 중인 결사 · 공개 정보 · 가입 신청. 권한은 개요의 권한표로 단추를 감출 뿐 — 판정은 서버.
- 연출: 결사 레벨업(문장 + 테두리 · 빛살 · 불티) · 임무 달성 도장 · 업적 카드 · 오른쪽 알림 줄(임무 · 출석 · 칭호 · 신청) · 머리 위 "+N 기여"(Pulse) · 금고 숫자 +N.
- 묻기는 250ms 간격을 지키는 차례(Busy면 다시). 끝에서 끝 `tools/regress/guild_e2e.py`(이 서버의 인메모리 빌드 + `regress_guild.py` 시험 데이터 · 봇).
