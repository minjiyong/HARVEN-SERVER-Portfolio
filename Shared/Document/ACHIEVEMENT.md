# ACHIEVEMENT — 개인 업적

> 코드: `Server/HARVEN_GameServer/Content/Achieve/`(AchieveHelper — 소유 Zone의 요청 · 사건 · 보상 · 저장 / AchieveBook — 사람 한 명의 진척 · 달성 · 받음, 순수) ·
> 진행 기록 `User/ProgressCodec`(판 4) · 규칙 · 색인 `Database/GameData/AchieveRules.h` · 로더 `Database/GameData/Load/Bind_Achieve.cpp` ·
> 표 시트 `Achievement`(→ `Bin/GameData/Achievement.json`) · 패킷 `Shared/Protocol/Protocol.h`(121~123) ·
> 시험 `Tools/Test/regress_achieve.py` · `tests/achieve_rule_tests.cpp`(`AchieveRuleTests`) · `Tools/DataTool/d2/achieve.py`(R136~R140).
> 계약(서버 · 클라 공통): 2026-10-13 개인 업적 명세. 순서는 업적 → 칭호 → 랭킹 — 이번 차수는 업적만(칭호 열은 칭호 차수에서 더한다).

## 1. 한눈에

| 무엇 | 어떻게 |
|---|---|
| **세기** | 서버가 센다(클라는 세지 않는다). 사람마다 통계 = (kind, param) → u32 — 표가 세는 키만 만든다. 누적(처치 · 그로시 …)은 u32에서 멈추고, 레벨 · 강화 단계는 최댓값, 지역 수는 들어가 본 지역 비트(16)의 개수 |
| **달성** | 통계가 업적 목표(`target`)에 닿는 순간 한 번 — 점수(`points`)는 바로 더하고(랭킹), `ACHIEVE_INF Completed` + 시스템 채팅 한 줄 「업적 달성: <이름>」(그 사람에게만) + 진행 기록을 바로 저장 |
| **받기** | 달성한 업적의 보상(아이템 한 묶음 + 그로시)을 「받기」로. 업적마다 다 들어갈 때만 받는다 — 가방 · 지갑이 모자라면 `BagFull`(아무것도 받지 않는다). 「모두 받기」는 id 순으로 들어가는 것만 받고 나머지는 남긴다 |
| **묶음(연작)** | 같은 `group`은 단계(`step` 1부터)가 이어지는 연작 — 화면에서 한 줄. 단계마다 따로 달성 · 받기(앞 단계를 받지 않아도 뒤 단계가 달성된다) |
| **소급** | 로그인마다 되돌릴 수 없는 값을 채운다 — 레벨 · 지금 지역 · 표가 바뀌어 이미 닿은 것. 가진 장비(가방 · 착용)의 최고 강화 단계는 **판 4 미만 기록(업적 전 · 새 캐릭터)을 처음 읽을 때 한 번만**(접속마다 하면 남의 +9를 빌려 들고 다시 접속하는 것으로 채운다). 알림(Completed · 채팅) 없이 달성만 — 받기는 그대로 해야 한다(Summary의 `claimable`) |
| **목록** | `List`(묻기 — 사람마다 500ms에 한 번) → 30줄씩 `Page`. 분야(6) 거르기 · 0xFF = 모두. 진척은 목표에서 자른다 |
| **멱등** | `Claim` · `ClaimAll`은 접속마다 1부터 늘어나는 순번(파티 순번 계약과 같다 — PARTY.md 7장) |

## 2. 기획 데이터 — `Achievement`(새 묶음 `achieve`, id_lock)

| 열 | 형 | 뜻 |
|---|---|---|
| `id` | u16 | 업적 id **1~1023**(영구 — 진행 기록의 비트 번호) |
| `category` | enum `AchCategory` | `eHunt` 사냥 · `eBoss` 보스 · `eGrowth` 성장 · `eGear` 장비 · `eCraft` 제작 · `eWorld` 세계 |
| `group` · `step` | u16 · u8 | 묶음 · 단계(1부터 연속) |
| `name` · `desc` | str ≤ 30 · 90바이트 | 이름 · 설명 |
| `kind` · `param` | enum `AchKind` · u16 | 무엇을 세는가 · kind마다 뜻(0 = 아무거나) |
| `target` | u32 | 목표(≥ 1) |
| `points` | u16 | 업적 점수(달성 때 바로) |
| `reward_item` · `reward_count` · `reward_adena` | u16 ref Item(0 = 없음) · u16 · u32 | 보상(아이템이 있으면 1 ~ max_stack, 없으면 0) |

| AchKind(값 = 순서, 끝에만) | 세는 것 | param | 진척 |
|---|---|---|---|
| 0 `eKill` | 몬스터 처치(피해를 준 모두 — 퀘스트 처치와 같은 자격, 허수아비 밖) | 종 id(0 = 아무 몬스터, 보스 · 정예 포함) | 누적 |
| 1 `eKillElite` | 정예 처치(같은 자격) | 0 | 누적 |
| 2 `eKillBoss` | 보스 처치(같은 자격) | 보스 종 id(0 = 아무 보스) | 누적 |
| 3 `ePartyKill` | 파티에 든 채 처치(같은 자격 · 처치 순간 `m_PartyId ≠ 0`) | 0 | 누적 |
| 4 `eLevel` | 레벨 | 0 | 최댓값 |
| 5 `eDeath` | 쓰러짐(몬스터 · PvP) | 0 | 누적 |
| 6 `eAdena` | 사냥 그로시(`AdenaReason::Pickup` — 처치 몫 · 떨어진 그로시 줍기) | 0 | 누적 합 |
| 7 `eGear` | 장비가 가방에 들어온 순간(필드 · 정예 · 보스 전리품 · 제작 결과 · 퀘스트 보상) | 등급(ItemGrade 0~5) — **그 등급 이상** | 누적 |
| 8 `eEnchant` | 강화 성공으로 도달한 단계 | 0 | 최댓값 |
| 9 `eCraft` · 10 `eDismantle` | 제작 · 분해 성공 | 0 | 누적 |
| 11 `eEssence` | 사냥 정수 · 결정(TierLoot의 정수 · 결정 · 정예 결정 = 재료 61~65)이 처치 · 정예 · 보스 보상으로 가방에 | 0 | 누적 개수 |
| 12 `eQuest` | 퀘스트 완료(반복 퀘스트는 끝낼 때마다) | 퀘스트 id(0 = 아무거나) | 누적 |
| 13 `eGuildJoin` | 결사 가입 · 창설 | 0 | 누적 |
| 14 `eAuctionSell` · 15 `eAuctionBuy` | 경매 판매 · 구매 성립 | 0 | 누적 |
| 16 `eTrade` | 교환 · 직거래 성립(`Content/Transfer`) | 0 | 누적 |
| 17 `eZone` | 서로 다른 지역(Region) 들어가 본 수 | 0 | 지역 수 |

- 1차 49행은 명세 그대로(이름 · 설명 · 보상은 기획 가칭 — 시트에서 고친다). 사냥 9 · 보스 8 · 성장 9 · 장비 8 · 제작 6 · 세계 9, 점수 합 1050.
- **지역 업적 49(target 16)는 그대로** — 지역 16곳 모두 들어갈 수 있다: 마을 지역(5 데니차 들녘 — 시작 · 부활)과 옛길 목적지 15곳(`teleport_fee > 0`,
  도착 칸은 R077이 그 지역 안 · 걸을 수 있는 칸으로 검사). `regress_achieve.py zone`이 한 캐릭터로 길잡이 옛길 15곳을 실제로 다녀 16/16을 본다.
- 구조 상수(코드): id ≤ 1023 · 이름 30 · 설명 90바이트 · 세는 (kind, param) 종류 ≤ 128 · 지역 비트 16 · 목록 30줄 · 묻기 500ms · 저장 60초(`AchieveRules.h`).
- 검증(DATA_RULES R136~R140): 표 구조(id · 겹침 · 바이트 · target ≥ 1 · 키 수) · kind/param(없는 종 · 허수아비 · 보스가 아닌 종 · 등급 > 5 · 없는 퀘스트 ·
  쓰지 않는 param ≠ 0) · 목표가 닿는다(레벨 ≤ 50 · 강화가 멈추는 단계 위 · 들어갈 수 있는 지역 수 위) · 묶음(step 연속 · 같은 분야 · 앞 단계와 같은 키면 target이 커진다 —
  param이 오르는 연작(장비 고급 → 희귀 → …)은 target이 같아도 된다) · 보상(아이템 · 화폐 아님 · 1 ≤ 개수 ≤ max_stack · 아이템 없으면 0).
- **시트 반영 완료(2026-10-13)** — 드라이브 `HARVEN_DATA`에 `Achievement_Table`을 Drive 커넥터로 만들고 `DataTool.json` `tables`에 id를 더한 뒤
  `datatool push --tabs Achievement` → `push --meta` → `sync`(표 54 · 오류 0 · 바뀐 파일 0). 이제 시트가 원본이다 — 업적 행은 시트에서 고치고 `sync`.

## 3. 저장 — 진행 기록 판 4(DB 마이그레이션 없음)

`UserProgress.data`(VARBINARY(4000)) — 바이트 형식은 `User/ProgressCodec`(CUser는 표로 거르기만):

```
'P' '2' 판(4) | classId | alloc[6] i16 | 판 2~: pityRare · pityHeroic u16 | 판 3~: 퀘스트(진행 중 칸 · 끝낸 비트 32B · 반복 대기)
판 4~: 업적 절 — u8 통계 수 · {u8 kind, u16 param, u32 값} × 수 · u8 달성 바이트 수(0~128) · 달성 비트 · u8 받음 바이트 수 · 받음 비트 · u16 지역 비트
       (비트 i = 업적 id i, 바이트 수는 마지막으로 켜진 비트까지. 값 0인 통계 · 지금 표가 세지 않는 키는 쓰지 않는다)
```

- 옛 판(1~3)을 읽으면 업적은 빈 채(로그인 소급이 채운다 — 이때만 최고 강화도). 다음 저장부터 판 4(`CUser::m_LoadedProgressVersion`이 읽은 판을 기억한다).
- **모르는 kind의 통계 줄**(새 서버가 쓴 7바이트)은 건너뛰고 읽기를 이어 간다 — 그 줄은 그대로 들고 있다가 되쓴다(아는 줄 먼저, 모두 합쳐 128줄까지). 판을 올리지 않고 kind를 더해도 옛 서버가 기록을 지우지 않는다.
- **정말 깨진 업적 절**(잘림 · 비트 바이트 수 > 128 · 절 뒤에 남은 바이트 · 이 서버보다 새 판)은 비우지 않고 **얼린다**(`AchieveBook::Freeze`): 그 접속 동안 메모리는 빈 채 ·
  사건은 세지 않고 · `Claim` · `ClaimAll`은 `Invalid` · 저장은 읽은 원본 절 바이트를 그대로 되쓴다(새 판은 그 판 번호로). 로그인 때 서버 로그 한 줄
  `[Achieve] 이름(번호) 업적 기록이 깨져 읽기 전용 — 까닭`. 빈 절로 덮으면 받음 비트를 잃고 → 소급 · 다시 받기가 된다(두 번 받기). 고치는 길은 운영(DB의 기록을 손본다).
  판 4인데 퀘스트 절부터 잘렸으면 업적 절이 아예 없다 — 지킬 바이트가 없어 빈 채(쓸 수 있다).
- 크기: 업적 절 최대 1157B(키 128 · 비트셋 둘 가득), 기록 전체 최대 1467B — `ProgressCodec::kMaxBytes ≤ 4000 / 2`를 `static_assert`로 묶었다(1차 표 · 무작위 시험 최대 ≈ 470B).
- 점수 · 달성 수 · 받을 수는 저장하지 않는다 — 표와 비트셋으로 그때 센다(표의 points가 바뀌어도 맞다).
- **언제**: 달성 · 받기는 바로(`CUserHelper::SaveProgress`). 그 밖 진척은 더러움 표시 뒤 60초(`EV_ACHIEVE_SAVE` — 사람마다 예약 하나) · 로그아웃(기존 길).
  같은 사람의 진행 기록 · 퀘스트 · 천장 저장(기존 SaveProgress 호출)도 업적 절을 함께 쓰고 더러움을 지운다.
- 받기: **받음 비트를 담은 진행 기록을 보상보다 먼저** 같은 사람의 DB 샤드(`playerId % 8` — 한 스레드 · 줄 선 차례대로)에 넣는다. 한 트랜잭션은 아니다(프로시저를 더하지 않으려고).
  그 사이 서버가 죽으면 받음만 남고 보상은 없을 수 있다(경제 로그로 운영이 메운다) — **크래시는 보상을 잃게 할 수는 있어도 두 번 주게 하지는 않는다**(7장).

## 4. 스레드 · 소유권

- 진척을 쓰는 곳은 그 사람을 가진 **Zone 스레드 하나**(`CUser::m_Achieve` — 인벤토리와 같은 전제, 락 없음). 다른 스레드는 `Service::PostToOwner`로 넘긴다:
  결사 서비스(가입 · 창설 · 로그인) · 경매 DB 완료(구매 · 판매 대금) · 다른 Zone에서 일어난 처치(아래).
- 처치: `CRewardHelper::HandleKill`이 퀘스트 처치를 주는 바로 그 자리에서 — 피해 기록의 기여자 중 이 Zone 스레드가 가진 사람은 바로,
  그 사이 다른 Zone으로 간 기여자는 그 사람의 Zone에서(`PostToOwner` — 퀘스트 `OnKill`과 같은 방식). 파티 여부는 받는 쪽 Zone의 `m_PartyId`.
- 요청(`ACHIEVE_REQ`)은 Route가 소유 Zone으로 넘긴다(다른 게임 요청과 같다). 업적에는 서비스 스레드가 없다.

## 5. 사건 표 — 어디서 세는가

| kind | 훅(소유 Zone) | 규칙 |
|---|---|---|
| eKill · eKillElite · eKillBoss · ePartyKill | `CRewardHelper::HandleKill` | 퀘스트 처치와 같은 자격(피해 기록의 모두, 막타가 이 Zone에 없어도). 허수아비는 세지 않는다. 종이 없는 옛 몬스터는 param 0만 |
| eLevel | `CUserHelper::GainExp`(레벨이 오를 때) · 로그인 소급 | 최댓값 |
| eDeath | `CContentHelper::HandlePlayerDeath` | 몬스터 · PvP 모두 |
| eAdena | `CInventory::AddAdena(…, Pickup)` | 실제로 늘어난 양(지갑 상한에서 잘린 뒤) — 처치 몫(`ShareAdena`) · 떨어진 그로시 줍기. 교환 확정 중에 받은 몫은 미룰 때 세지 않고 확정 뒤 `FinishTransfer`가 다시 넣을 때 한 번. 교환 · 거래 대금(DirectTrade)은 세지 않는다 |
| eGear | `CLootHelper::Grant`(가방에 들어간 때) · 제작 완료(결과 장비) · 퀘스트 `Finish`(보상 장비) | 그 등급 이하 param 모두 +개수. **가방이 차서 발밑에 넘친 전리품은 세지 않는다**(줍기는 버린 물건 다시 줍기와 구별되지 않는다 — 같은 장비로 여러 번 세는 길을 막는다). 경매 정산 받기 · 교환 · 상점 구매는 세지 않는다 |
| eEssence | `CLootHelper::Grant`(가방에 들어간 때) | 아이템이 TierLoot 정수 · 결정이면 +개수(소스 무관 — 지금은 처치 · 정예 · 보스 보상만 준다) |
| eEnchant | 강화 완료(DB 커밋 · 성공) · 로그인 소급(가진 장비의 최고 단계 — 판 4 미만 기록을 처음 읽을 때만) | 최댓값 |
| eCraft · eDismantle | 제작 · 분해 완료(DB 커밋 뒤 소유 Zone) | 그 사람이 나간 사이 커밋된 것은 세지 못한다(드문 경우 — 진척은 소유 Zone에만 쓴다) |
| eQuest | `CQuestHelper::Finish`(보고 · 보고 없이 완료) | 0 · 그 퀘스트 id |
| eGuildJoin | 결사 `AddMember`(초대 수락 · 가입 승인) · `FinishCreate`(창설) → PostToOwner · **로그인 때 결사원이면 최소 1** | 나가 있는 동안 승인되면 그때는 못 세고 다음 로그인에 최소 1(최댓값이라 두 번 세지 않는다). 업데이트 전부터 결사원이던 사람도 1 |
| eAuctionBuy | 경매 구매 DB 커밋 → 구매자 PostToOwner | 커밋이 승부를 정한다 |
| eAuctionSell | **판매 대금 정산 줄(`AuctionSettleReason::Sold`)을 받을 때**(받기 DB 커밋 → PostToOwner) | 줄마다 한 번(받기는 한 번뿐) — 판 사람이 오프라인 · 다른 Zone이어도 대금을 받는 순간 그 사람의 Zone에서 센다. 받기 전에는 세지 않는다 |
| eTrade | 교환 · 직거래 확정(`End(committed)`) — 두 사람 | 같은 Zone 스레드가 둘을 가졌다(교환은 같은 Zone · 확정 중 이동 금지) |
| eZone | Zone 옮김 `HandleEnterZone`(걸어서 · 옛길 · 귀환) · 부활 `FinishRespawn` · 로그인 | 지금 자리의 `RegionIdAt` 비트 |

## 6. 패킷(121~123) — 끝에만 추가

```cpp
ACHIEVE_REQ = 121,   // C→S
ACHIEVE_ACK = 122,   // S→C
ACHIEVE_INF = 123,   // S→C
enum class AchieveOp : uint8_t { List = 0, Claim = 1, ClaimAll = 2 };
enum class AchieveResult : uint8_t { Ok, Invalid, NotDone, AlreadyClaimed, BagFull, Busy, Duplicate, TooFast, NothingToClaim };
enum class AchieveInfKind : uint8_t { Summary = 0, Page = 1, Completed = 2, Claimed = 3 };
struct ACHIEVE_REQ_PACKET { unsigned short size; char type; uint8_t op; uint32_t seq; uint16_t achId; uint8_t category; };   // 11
struct ACHIEVE_ACK_PACKET { unsigned short size; char type; uint8_t op; uint8_t result; uint32_t seq; uint16_t achId;
                            uint16_t claimed; uint32_t adena; uint16_t itemId; uint16_t itemCount; };                       // 21
struct ACHIEVE_ENTRY { uint16_t id; uint32_t progress; uint8_t state; };                                                      // 7
struct ACHIEVE_INF_PACKET { unsigned short size; char type; uint8_t kind; uint32_t points; uint16_t done; uint16_t total;
                            uint16_t claimable; uint8_t page; uint8_t pageCount; uint8_t count; ACHIEVE_ENTRY entries[30]; }; // 227
```

바이트 자리(1바이트 정렬): REQ op@3 seq@4 achId@8 category@10 · ACK op@3 result@4 seq@5 achId@9 claimed@11 adena@13 itemId@17 itemCount@19 ·
INF kind@3 points@4 done@8 total@10 claimable@12 page@14 pageCount@15 count@16 entries@17(줄 7B: id@0 progress@2 state@6 — 0 진행 · 1 달성 · 2 받음).
`AdenaReason` 끝에 `Achievement`(14). 다른 패킷 · 열거형은 그대로.

| 때 | 서버가 보내는 것 |
|---|---|
| 로그인(월드 입장 — 소급 뒤) | `Summary` 한 번(page 0 · pageCount 0 · count 0) |
| 달성 순간 | `Completed` — entries[0] = 그 업적(진척 = 목표 · 상태 1), 요약 칸 = 이 사건의 달성을 모두 담은 새 값. 한 사건에 여럿이면 여러 장(달성 차례) + 시스템 채팅(채널 2) 「업적 달성: <이름>」 |
| `List` | `Page` 여러 장(30줄씩 · page 0부터 · pageCount = 장 수, 줄이 없어도 한 장). **성공은 ACK 없이** — 500ms 안에 다시 오면 `TooFast` ACK, 분야 > 5(0xFF 아님)면 `Invalid` ACK |
| `Claim` 성공 | ACK `Ok`(claimed 1 · 보상 그로시 · 아이템 · 개수) + `Claimed`(entries = 받은 업적, 상태 2) — 칸 · 그로시 알림은 기존 `ITEM_SLOT_INF` · `ADENA_INF`(reason 14) |
| `ClaimAll` 성공 | ACK `Ok`(claimed = 받은 수 · 그로시 합 · 아이템은 받은 것 중 첫 것) + `Claimed`(30줄씩). 일부만 받았으면 `Ok`이고 `Claimed`의 `claimable` > 0 이 남은 수 |

| AchieveResult | 언제 |
|---|---|
| `Invalid` | 모르는 op · 없는 업적 id · 분야 값 · 죽음 · Zone 이동 중 · 업적 기록이 깨져 얼린 책(3장) |
| `NotDone` · `AlreadyClaimed` | 달성 전 · 이미 받음(Claim) |
| `BagFull` | 보상 아이템이 가방에 들어가지 않는다 **또는 그로시가 지갑 상한(20억)을 넘긴다** — 아무것도 받지 않았다. ClaimAll은 하나도 못 받았을 때 |
| `Busy` | 강화 · 제작 · 분해 · 교환(칸 잠금) · 경매 요청 · 결사 그로시(창설 · 상점) 진행 중 |
| `Duplicate` | seq 0 또는 이 접속에서 받은 seq 이하 — `claimed` = 마지막으로 받은 seq(아래 16비트). 아무것도 바뀌지 않았다 |
| `TooFast` | List 간격 |
| `NothingToClaim` | ClaimAll — 받을 것이 없다 |

### 순번 계약(seq)

파티와 같다(PARTY.md 7장): `Claim` · `ClaimAll`은 **접속(세션)마다 1부터 늘어나는 순번** — 다시 접속하면 1부터. 서버는 사람마다 마지막으로 받은 순번(`CUser::m_LastAchieveSeq`)
이하를 처리하지 않고 `Duplicate`(claimed = 마지막 순번). 순번을 받은 요청은 결과가 거절이어도 창을 올린다. 응답을 못 받아 다시 보낼 때만 같은 순번,
처음 보낸 요청이 Duplicate면 `claimed + 1`로 다시. `List`는 순번을 보지 않는다(묻기).

## 7. 받기 규칙

- 업적마다 **다 들어갈 때만**, 차례가 계약이다(`CAchieveHelper` `Grant`):
  1. 드라이런 — 지갑 상한 · 가방 자리(`CInventory::CanAddItem` — `TryAddItem` 1단계와 같은 코드: 겹치는 묶음 · 빈 칸, 잠긴 칸은 건너뛴다). 안 되면 아무것도 바꾸지 않고 `BagFull`.
  2. 받음 비트 → 3. 진행 기록 저장을 **먼저** 줄 세운다(`CUserHelper::SaveProgress`) → 4. 아이템(`TryAddItem` — 드라이런이 보장했으니 실패는 `ASSERT_CRASH`) · 그로시(`AddAdena`).
  같은 사람의 DB 샤드가 줄 선 차례대로 반영하므로 DB에서도 받음 비트가 보상보다 먼저다. 크래시는 보상을 잃게 할 수는 있어도 두 번 주게 하지는 않는다.
- 아이템: 장비면 등급 바닥 희귀도로 옵션을 굴리고(제작 결과와 같다) `ItemRules::OnAcquire`(획득 귀속 bit 8 — 장비 파편 같은 겹치는 귀속 재료는 행으로). 1차 보상은 소비품 · 재료뿐.
- 경제 로그: 그로시는 `Achievement` 사유(AddAdena가 센다) · 장비 `GearSource::Achievement` · 재료 `MaterialSource::Achievement`(소비품은 세지 않는다 — 경제 1차 §8).
- `ClaimAll`: id 순으로 받을 것을 차례로 — 안 들어가는 것(가방 · 지갑)은 건너뛰고 뒤의 것은 계속 본다(그로시만 · 이미 있는 묶음에 겹치는 보상은 받을 수 있다).
  받을 때마다 위 1~4(받은 업적마다 진행 기록 한 번 — 그 보상보다 먼저). 가방이 N개만 받아 주면 정확히 N개 · 기록의 받음 비트도 N개.

## 8. 시험

- `cmake --build --preset linux-memory --target AchieveRuleTests` — 색인 · 통계 → 달성(처치 0 · 종 · 정예 · 보스 · 파티 · 레벨 최댓값 · 장비 「그 등급 이상」 · 퀘스트 · 지역 · u32 포화) ·
  받기 · 업적 절 무작위 300번 왕복 · 진행 기록 판 4 왕복 · **판 3 기록 → 업적 빈 채** · 가장 큰 기록(1467B ≤ 2000B) · Recheck ·
  **모르는 kind 줄(kind 22 · eZone 통계) → 아는 부분 · 받음 비트 왕복 + 그 줄 그대로** · **잘림 · 비트 길이 · 뒤에 남은 바이트 · 새 판(5) → 얼림: 사건이 바꾸지 않고
  Write · Encode가 원본 바이트 그대로**(모든 자름 자리에서) — 4만 5천 검사(ASan+UBSan로도). `ServicePostTests` — PostToOwner 판정 표 · 도망치는 사람 쫓기 6만 3천 검사.
- `python3 Tools/Test/regress_achieve.py`(인메모리 서버를 직접 띄운다 — 시험용 GameData 사본 · `HARVEN_DB_BATCH_DELAY_MS=300` · 메모리 DB 저널로 지갑 20억 · 진행 기록 판 3 · 4 · 깨진 업적 절 캐릭터):
  kill(Summary 소급 · 첫 처치 Completed(1) + 채팅 + 요약 값 · 장비 · 정수 · 사냥 그로시 · List 두 장 · 분야 · Invalid · TooFast · Claim(아이템 · 그로시 14 · Claimed · 상태 2) ·
  AlreadyClaimed · NotDone · 없는 업적 · Duplicate(claimed) · seq 0 · ClaimAll · NothingToClaim · /economy · **다시 접속 — 진척 · 받음이 남고 seq는 1부터**) ·
  bag(BagFull · 한 칸 비우면 ClaimAll 일부 · 또 비우면 받기) · wallet(지갑 상한 → BagFull) · retro(Lv40 · 철검 +2 소급, Completed 없음) · party(파티원만 파티 처치 · 모두 처치) ·
  elite · boss(모로즈) · death(PvP · 마을 부활 = 지역 하나 더) · craft(제작 · 분해 · 강화 +1 · 제작 DB 대기 중 받기 Busy) · quest · guild(세움 · 가입 · 다시 접속 그대로 1) ·
  auction(구매 커밋 · 판매 대금 받기 전에는 안 셈) · trade · zone(걸어서 Zone 경계 · 옛길 15곳 — 다시 접속하며 16/16) ·
  tradeadena(`HARVEN_DB_TRANSFER_DELAY_MS=3000` — 거래 커밋이 DB에 머무는 동안 막타: 미룬 사냥 그로시가 대금 뒤에 한 번 · 진척 = 받은 사냥 그로시 · 판 대금 1,000은 세지 않는다) ·
  frozen(잘린 업적 절 → Summary 0 · 받기 Invalid · 나갈 때 원본 절 그대로 · 로그 한 줄 / 모르는 kind 줄 → 건너뛰고 읽어 받음 비트가 남고 되쓴 기록에 그 줄) ·
  enchretro(판 4 + 가방 +9 → 강화 진척 그대로 / 판 3 + +9 → 한 번 소급 · 판 4로 저장) ·
  claimorder(메모리 DB 일지의 반영 차례: 받음 비트를 담은 기록 G가 그 보상의 칸 S · 그로시 A보다 먼저 · BagFull은 기록을 쓰지 않는다 · 두 칸만 비면 ClaimAll 정확히 둘).
  새 네 시나리오는 고치기 전 코드로 되돌려 빌드하면 실패한다(차례 · +9 소급 · 200 = 100).
  `linux-asan` 빌드로도 돌린다(`--server build/linux-asan/GameServer`).
- `datatool check --gamedata` · `verify`(62표 · Achievement 49) · `d2`(achieve 31사례 — R136~R140).
- 다른 시스템이 그대로인지: regress_economy · craft · party · guild · items_edge · social_edge · transfer · smoke · session · inventory · zone.

## 클라가 할 일

명세 4절 그대로(HARVEN-CLIENT가 같은 명세로 따로 만든다):

1. 업적 표 생성 — 이 저장소 `Bin/GameData/Achievement.json`(이름 · 설명 · 보상 · 분야 · 묶음 · 단계 · 목표 · 점수)에서 `gen_items`처럼.
2. 메뉴 허브 「업적」 칸(비어 있는 단축키 — 예 `O`) · 받을 것이 있으면 빨간 점(`claimable` > 0 — Summary · Completed · Claimed가 늘 새 값을 준다).
3. 업적 창(공방 UI): 위 요약(점수 · 달성 n/total · 「모두 받기」) · 분야 탭 6 · 줄마다 아이콘 · 이름 · 설명 · 진척 막대(묶음은 지금 단계만 크게 + 단계 점) · 점수 · 보상 ·
   「받기」(상태 1) / 「받음」(2) / 진척(0). 열 때 `List`(500ms 간격을 지킨다 — `TooFast`면 잠시 뒤 다시) — 탭을 바꿀 때마다 묻지 말고 0xFF로 한 번 받아 거르는 것이 싸다.
4. 받기 연출(보상이 가방으로 날아든다 · 점수 숫자 오름) — 결과는 ACK, 상태는 `Claimed`, 칸 · 그로시는 기존 알림. `BagFull`이면 「가방(또는 지갑)이 가득 찼습니다」.
5. 달성 순간: 화면 위 가운데 업적 띠(금빛 · 종소리 · 점수 +N) — 여러 장이 한꺼번에 오면 줄 세워 하나씩. 채팅 시스템 줄은 서버가 보낸다(따로 만들지 않는다).
6. 순번 계약(6장): `Claim` · `ClaimAll` seq는 접속마다 1부터 늘리기만.
