# PARTY — 파티 · 모집 게시판 · 처치 보상

> 코드: `Server/HARVEN_GameServer/Content/Party/`(PartyService · PartyHelper) · 처치 보상 `Content/Reward/RewardHelper` · 규칙 `Database/GameData/PartyRules.h` · `RewardRules.h` ·
> 튜닝 값 시트 `PartyConst`(→ `Bin/GameData/PartyConst.json`) · 희귀도 알림 시트 `Rarity.loot_notice` ·
> 피해 기록 `Content/Reward/DamageLedger.h` · 패킷 `Shared/Protocol/Protocol.h`(104~107) · 시험 `Tools/Test/regress_party.py`.
> 서버만 — 클라 화면은 아직 없다(아래 [클라가 할 일](#클라가-할-일)).

## 1. 한눈에

| 무엇 | 어떻게 |
|---|---|
| 파티 만들기 · 초대 · 수락 · 거절 | `Create` · `Invite`(이름) · `InviteReply`. 파티가 없는 사람이 초대하면 **받아들일 때** 초대한 사람이 파티장인 파티가 생긴다(거절 · 만료면 아무것도 남지 않는다). 받아들일 때 초대한 사람이 그 파티를 나갔거나 · 쫓겨났거나 · 초대 권한을 잃었으면(설정 · 파티장 넘김) `InviteExpired` |
| 탈퇴 · 추방 · 파티장 위임 · 해산 | `Leave` · `Kick` · `Transfer` · `Disband`. 파티장이 나가거나 끊기면 접속 중인 다음 사람(먼저 들어온 순)이 파티장 |
| 인원 · 초대 권한 | 시트 `max_members`(2~8). `Settings` — 파티장만 초대(기본) / 누구나 초대 |
| 파티원 목록 · HP · MP · 직업 · 레벨 · 위치 · 접속 | `PARTY_INF Roster`(구성 — 바뀔 때마다) + `PARTY_INF Members`(바뀐 사람만 — 시트 `sync_interval_ms`마다) |
| 미니맵 위치 · 전투 중 | Members 줄의 지역(zone) · x · y · `PARTY_MEMBER_COMBAT`(5초 안에 때렸거나 맞았다) |
| 대상 공유 · 공격 집중 표시 | `Target`(내가 고른 대상 → 파티원에게) · `Mark`(파티장이 정한 집중 대상 → 모두에게) |
| 사망 · 부활 알림 | `PARTY_NFY Died`(쓰러진 지역 · 자리) · `Revived` |
| 파티 채팅 | `CS_CHAT` 채널 `ChatChannel::Party` → 접속 중인 파티원 모두(보낸 사람 포함) |
| 경험치 · 그로시 나눔 | 몬스터를 때린 **모두**(파티가 아니어도)에게 피해 비례 · 파티 몫 안에서 **쩔 보장**(아래 2장) · 받은 사람에게 `PARTY_NFY ExpShare` |
| 아이템 | **막타**가 가방에 바로(자동 획득) · 희귀 이상은 서버 전체 시스템 채팅 · 가방이 차 넘치면 **자신 > 파티원 > 누구나**(아래 3장) |
| 퀘스트 처치 목표 | 막타만이 아니라 **피해를 준 모두**(아래 3장) |
| 모집 게시판 | 목적(사냥 · 보스 · 퀘스트) · 제목 · 최소 · 최대 레벨 · 목표 인원 · 빈자리 · 신청 · 승인 · 거절 · 자동 가입 · 움직임 없으면 닫힘(아래 4장) |
| 끊긴 파티원 | 시트 `offline_keep_sec` 동안 파티원으로 남는다(다시 들어오면 그대로). 지나면 빠진다(`MemberTimeout`) |

파티는 **DB에 남기지 않는다** — 서버를 다시 켜면 흩어진다(결사와 다르다).

## 2. 그로시 · 경험치 — 때린 모두에게 피해 비례 · 파티 몫 안에서 쩔 보장

몬스터 하나가 쓰러지면 그 몬스터를 가진 Zone 스레드가 정한다(`CRewardHelper::HandleKill`).

1. **피해 기록** — 몬스터마다 이번 생애에 플레이어가 **실제로 깎은 HP**(`CDamageLedger` 16칸 — 넘친 막타는 남은 HP만큼). 위협 표(`CAggro`)와 따로다
   (위협에는 도발 · 감지 · 동료 도움이 섞인다). 다시 태어날 때 · 집에 돌아가 HP를 다 채울 때 지운다.
2. **나눠 받는 사람** — 기록에 있는 사람 + 막타 중 이 Zone 스레드가 가진 사람(그 사이 다른 Zone으로 간 사람 · 나간 사람은 빠진다) 가운데
   **살아 있고 쓰러진 자리에서 `exp_share_range`칸 안**인 사람. **파티가 아니어도, 서로 모르는 사람끼리도** 같다.
   빠진 사람의 몫은 남은 사람에게 간다(나눌 몫이 줄지 않는다). 아무도 없으면 막타가 다 받는다.
3. **그로시** — 한 번 굴린 양(`adena_chance` · `adena_min~max`)을 피해 비례로 나눠 **바로 지갑에**(`ADENA_INF` reason `Pickup`).
   "그로시 획득" 장비 옵션은 저마다 **자기 몫에만** 더한다. 바닥에 떨어뜨리지 않는다.
4. **경험치** — 모두에게 피해 비례(`RewardRule::SplitByWeight`)로 나눈 뒤, **같은 파티에서 둘 이상 받으면 그 파티 몫을 모아 파티 안에서 다시 나눈다**
   (`PartyRule::SplitExp` — 쩔 보장). 모르는 사람의 몫은 쩔 보장으로 줄지도 늘지도 않는다(`PartyRule::SplitKillExp`). **합은 언제나 몬스터 경험치.**
   받는 사람이 둘 이상이면 저마다 `PARTY_NFY ExpShare`(파티가 아니어도).

`SplitByWeight` · `SplitExp` · `SplitKillExp`(서버 · 클라 공용 헤더 — 컴파일 시험 `static_assert` · `regress_party.py`의 파이썬 사본과 같은 결과):

- **비례**: `total × 내 피해 / 모두의 피해`. 나머지는 소수 부분이 큰 순(같으면 피해가 큰 · 앞 사람)으로 1씩.
- **쩔 보장**(파티 몫 `pool` 안에서): 함께 받는 파티원 중 **최고 레벨과의 차이 ≤ `carry_level_range`**인 사람은 적어도 `floor = pool × carry_min_exp_pct / 100`.
  모자란 몫은 floor를 넘게 받은 파티원들에게서 넘는 만큼에 비례해 가져온다(그들도 floor 아래로 내려가지 않는다).
  보장받는 사람이 많아 `floor × 사람 수 > pool`이면 `floor = pool / 사람 수`.
- 레벨 차이가 범위 밖이면 보장 없이 비례만 — 아주 큰 차이의 쩔(만렙이 1레벨을 끌어올림)은 시트로 막는다.

| 예(경험치 1000, 시트 기본 20레벨 · 20%) | 피해 | 몫 |
|---|---|---|
| 파티 Lv40 + Lv25(차이 15 — 보장 안) | 3911 : 89(2.2%) | 800 : **200**(보장) |
| 파티 Lv40 + Lv12(차이 28 — 보장 밖) | 3952 : 48(1.2%) | 988 : 12(비례) |
| 모르는 둘(파티 없음) — 막타는 적게 때린 쪽 | 2900 : 1100 | 725 : 275(막타가 아니어도 비례 · 그로시도 같은 비율) |
| 모르는 Lv40 + 파티(Lv25 · Lv40) | 1666 : 150 : 2184 | 417 : **116**(파티 몫 583의 20%) : 467 — 모르는 사람 417은 비례 그대로 |

## 3. 아이템 — 막타 자동 획득 · 희귀도 알림 · 넘치면 자신 > 파티원 > 누구나 · 퀘스트

- **소모품 드롭 표 · 필드 장비는 막타의 것**이다. 굴림(직업 무기로 바꾸기 · 희귀품 발견 · 희귀 천장)도 막타로 하고, **가방에 바로 넣는다**
  (`SYSTEM_MESSAGE_INF LootAcquired` param1 = 아이템 · param2 = 수량, 칸은 `ITEM_SLOT_INF`). 막타가 그 사이 다른 Zone으로 갔으면(지속 피해가 늦게 들어감) 가장 많이 때린 사람.
  경제 1차부터 정수 · 결정(피해 비례 추첨 한 명) · 정예 보상(피해 10% 이상) · 보스 개인 굴림(피해 3% 이상 · 그 파티원)은 받을 사람마다 따로 —
  아래 가방 · 넘침 · 알림 규칙은 같고 주인 = 받는 사람이다([ECONOMY.md](ECONOMY.md) 18~20절).
- **희귀도 알림** — 시트 `Rarity.loot_notice`가 켜진 희귀도(기본 희귀 · 영웅 · 전설 · 신화)의 몬스터 전리품이 가방에 들어가면
  `SC_CHAT` 채널 `System`(2)으로 **서버 전체**에: 보낸 이 = 얻은 사람(ObjID · 이름), 글 = `성준 님이 [희귀] 서리 단검을 획득했습니다.`
  (희귀도 이름 `Rarity.name` · 아이템 이름 `Item.display_name` · 받침에 맞춘 을/를). 최근 대화에 남아 접속 직후에도 보인다. 신화는 예전처럼 `WORLD_NOTICE_INF RareLoot`도.
  플레이어가 버린 물건을 다시 줍는 것은 알리지 않는다(예전에는 신화를 버렸다 주워 공지를 되풀이할 수 있었다).
- **가방이 가득 차면** 죽은 자리 둘레에 떨어뜨린다(`LootOverflow`). 떨어질 때의 주인(막타) 파티 번호를 물건에 적고, 주울 때 시각으로 판정한다
  (`CItemHelper::CanLoot` — 예약 타이머 없음). 이 물건을 누가 주우면 그 사람 이름으로 희귀도 알림.

| 넘친 물건이 떨어진 뒤 | 주울 수 있는 사람 |
|---|---|
| ~ `loot_owner_ms`(기본 10초) | 주인(막타)만 |
| ~ `loot_party_ms`(기본 20초) | 주인 + 떨어질 때 주인의 파티원(파티가 없으면 이 단계가 없다) |
| ~ 30초 | 누구나 → 30초에 사라진다 |

주울 수 없는 물건은 "그 칸에 아무것도 없다"와 같다(`ITEM_PICKUP_ACK` 실패 — 남의 우선권인지 빈 칸인지 가르지 않는다, 예전과 같다).

- **퀘스트**(처치 · 지역 처치 · 수집 목표): 쓰러진 몬스터를 **때린 모두**가 오른다 — 거리 · 생사와 상관없이(그 사이 다른 Zone으로 간 사람은 그 Zone 스레드에서
  `Service::PostToOwner`). 수집 확률은 저마다 따로 굴린다. 보스 목표는 예전처럼 가까이 있던 모두(`OnBossKilled`). 성향치 · 보스 처치 공지는 막타.

## 4. 모집 게시판

- 글은 **파티 하나에 하나**, 파티장이 올린다(`RecruitPost` — 파티가 없으면 그때 만든다). 다시 올리면 고침 + 끌어올림.
- 칸: 목적(`PartyPurpose` 사냥 · 보스 · 퀘스트) · 제목(한글 15자, 결사 이름과 같은 금칙어 — `PartyRule::CheckTitle`) · 최소 · 최대 레벨 · 목표 인원(파티장 포함 2~`max_members`) · 자동 가입.
- 목록(`RecruitList` — 목적 거르기 · 3줄씩): **빈자리 있는 글 먼저 · 최근에 움직인 글 먼저**. 줄마다 지금 인원 / 목표 인원(빈자리 = 차이) · 파티장 이름 · 내 글 · 내가 신청한 글 표시.
- 신청(`RecruitApply`): 레벨 범위 · 빈자리 · 파티가 없을 것. **자동 가입 글이면 바로 들어간다**(ACK param 1), 아니면 승인 대기(param 0) — 파티장에게 `RecruitApplied`(이름 · 레벨 · 직업).
  한 사람에 신청 하나(다른 글에 내면 앞의 것을 거둔다). 글 하나의 대기 자리 8(넘치면 `Busy`).
- 파티장: `RecruitApplicants`(대기 목록) · `RecruitAccept` · `RecruitReject`(신청자에게 알림) · `RecruitClose`.
  파티가 목표 인원인 동안의 승인은 `Full` — 신청은 대기로 남는다(자리가 나면 다시 승인). 신청자가 다른 파티에 들어가거나 · 만들거나 · 접속을 끊으면 신청은 거둬진다.
- **움직임 없는 글은 닫힌다**: 마지막 움직임(올림 · 고침 · 승인 · 거절 · 들어옴 · 위임)에서 `recruit_idle_sec`(기본 10분)가 지나면. 파티는 그대로 — 파티장 · 대기 신청자에게 `RecruitClosed`(사유 `Idle`).
  해산하면 사유 `Disbanded`로 닫힌다. 파티가 목표 인원에 차도 글은 남는다(빈자리 0 — 신청은 `Full`).

## 5. 스레드 · 소유권

```
[요청한 사람의 Zone 스레드]                         [svc-party — CPartyService, 작업 큐 하나 · 락 없음]
 Route PARTY_REQ → CPartyHelper::HandleRequest ─Post─▶ Handle(ctx, 요청): 파티 · 초대 · 글 · 신청의 주인
   형식 · 멱등(seq) · 묻기 간격(0.5초)                     │ 알림: 세션 PostSend만(게임 상태는 만지지 않는다)
                                                          │ 파티 번호 사본: PostToOwner → CUser::m_PartyId(순번 — 늦은 옛 값이 덮지 않게)
[각 Zone 스레드 — EV_PARTY_TICK, sync_interval_ms]        ▼
 자기 섹터의 파티원 중 바뀐 사람만 한 묶음 ──Post──▶ OnSync → 파티마다 PARTY_INF Members 한 패킷
 (Zone마다 예약 하나 — 사람마다 예약을 걸지 않는다: 전역 객체 표 락을 타지 않는다. 파티원이 없는 Zone은 4배 간격)
[몬스터를 가진 Zone 스레드 — AttackMonster]
 CDamageLedger 기록 → 처치 때 CRewardHelper::HandleKill(그로시 · 경험치 · 막타 아이템 · 퀘스트) — 이 Zone 스레드가 가진 사람만 만진다
   (다른 Zone으로 간 기여자의 퀘스트만 PostToOwner) · 희귀도 알림은 SC_CHAT System을 세션마다 PostSend(전체 대화와 같은 길)
```

- 서비스 → 한 세션의 파티 패킷은 모두 서비스 스레드가 넣은 순서대로 간다 — `Roster`(version) 뒤의 `Members` 자리 번호가 어긋나지 않는다.
- 사망 · 부활 · 로그인 · 로그아웃 · 파티 채팅은 그 사람의 Zone이 서비스로 `Post`(`CPartyHelper::OnDied` · `OnRevived` · `OnEnterWorld` · `OnLeaveWorld` · `RelayChat`).
- 1초 틱(`svc-tick`): 초대 만료 · 오래 끊긴 파티원 · 움직임 없는 글.

## 6. 기획 데이터 — `PartyConst`(한 행)

| 열 | 기본 | 뜻 |
|---|---|---|
| `max_members` | 8 | 파티 인원(파티장 포함, 2~8 — R114) |
| `invite_expire_ms` | 30000 | 초대에 답할 시간 |
| `offline_keep_sec` | 300 | 끊긴 파티원을 남겨 두는 시간 |
| `sync_interval_ms` | 500 | 파티원 상태를 모아 보내는 간격(≥ 100) |
| `exp_share_range` | 20 | 그로시 · 경험치를 나눠 받는 거리(칸 — 1~30, R116 · 파티가 아니어도) |
| `carry_level_range` | 20 | 쩔 보장 조건 — 함께 받는 파티원 중 최고 레벨과의 차이 |
| `carry_min_exp_pct` | 20 | 쩔 보장 몫(파티가 받은 경험치의 %, 0 = 보장 없음) |
| `loot_owner_ms` | 10000 | 가방이 차 넘친 전리품 — 주인(막타)만 줍는 시간 |
| `loot_party_ms` | 20000 | 넘친 전리품 — 주인 + 파티가 줍는 시간(주인 시간 이상 · 30초 미만 — R115) |
| `recruit_idle_sec` | 600 | 모집 글이 움직임 없이 닫히기까지 |

구조 상수(패킷 줄 수 · 배열 · 제목 바이트 · 요청 간격)는 `PartyRules.h`에 둔다(`kMaxMembers` 8 · 구성 4줄 · 글 3줄 · 신청자 8 · 제목 46B · 묻기 500ms · 대상 공유 150ms).
시트 파일 `HARVEN_DATA/PartyConst_Table` — 표 머리 보호 · 서식은 `datatool push --tabs PartyConst --force-seed`(서비스 계정 키가 있는 PC에서 한 번, 값은 그대로).

희귀도 알림은 `Rarity` 표의 `loot_notice` 열(bool 체크박스 — 기본 희귀 · 영웅 · 전설 · 신화 켬). 시트 `Rarity_Table`에 열을 넣었고
`PartyConst_Table` 2행 설명도 새 규칙으로 고쳤다(2026-10-11 — 시트 = `Bin/GameData` 확인, `sync`해도 바뀌는 값 없음).

## 7. 패킷(104~107) — 묶음 패킷 + op, 끝에만 추가

| 방향 | 패킷 | 크기 | 내용 |
|---|---|---|---|
| C→S | `PARTY_REQ` (=104) | 99B | `op`(PartyOp 19가지) · `seq` · `arg8` · `arg32` · `obj`(대상 · 집중) · 글: `purpose` · `minLevel` · `maxLevel` · `slots` · `target[20]`(사람 이름) · `text[46]`(제목) |
| S→C | `PARTY_ACK` (=105) | 13B | 요청마다 하나(대상 공유 `Target`은 없음) — `op` · `result`(PartyResult 17가지) · `seq` · `param` |
| S→C | `PARTY_INF` (=106) | 231 · 146 · 240 · 185B | `kind`: Roster(구성 4줄씩 · version · 파티장 자리 · 초대 권한 · 인원 · 글 번호 · 집중 대상) / Members(바뀐 줄 — 자리 · 레벨 · 플래그 · 지역 · x · y · HP · MP) / RecruitList(글 3줄) / Applicants(신청자 8) |
| S→C | `PARTY_NFY` (=107) | 83B | `event`(PartyEvent 20가지) · `arg8` · `arg16` · `arg32` · `obj` · `target` · `who[20]` · `by[20]` |

- `PacketType` 104~107. 받는 버퍼 `MAX_CLIENT_PACKET_SIZE` 152 그대로(PARTY_REQ 99B). 모든 새 패킷 ≤ 255B `static_assert`.
- 기존 열거형에는 끝에만: `ChatChannel::Party`(7) · `SystemMessageCode::ChatNoParty`(23) · `LootAcquired`(24) · `LootOverflow`(25). 다른 기존 패킷 · 구조체는 그대로
  (희귀도 알림은 기존 `SC_CHAT` 채널 `System`(2), 그로시는 기존 `ADENA_INF` reason `Pickup`).
- `PARTY_ACK param`: 만들기 · 수락 → 파티 번호 · 초대 → 초대 번호 · 글 올리기 → 글 번호 · 신청 → 1 바로 들어감 / 0 대기 · 레벨 밖 → `(min << 8) | max` · 묻기 간격 → 남은 ms ·
  `Duplicate` → 이 세션에서 마지막으로 받은 순번(아래 「순번 계약」).

| PartyOp | 쓰는 칸 |
|---|---|
| `Info` · `Create` · `Leave` · `Disband` · `RecruitClose` · `RecruitApplicants` · `RecruitCancel` | — |
| `Invite` · `Kick` · `Transfer` · `RecruitAccept` · `RecruitReject` | `target` |
| `InviteReply` | `arg8` 1 수락 / 0 거절 · `arg32` 초대 번호(`InviteFrom`의 `arg32`) |
| `Settings` | `arg8` PartyInvitePolicy |
| `Target` · `Mark` | `obj`(npos = 없음 · 지움) |
| `RecruitList` | `arg8` 목적(0xFF 전체) · `arg32` 페이지 |
| `RecruitPost` | `purpose` · `minLevel` · `maxLevel` · `slots` · `arg8` 자동 가입 · `text` 제목 |
| `RecruitApply` | `arg32` 글 번호 |

| PartyEvent | 칸 |
|---|---|
| `InviteFrom` | `who` 초대한 사람 · `obj` · `arg32` 초대 번호 · `arg8` 초대한 사람의 파티 인원(0 = 없음) |
| `InviteDeclined` · `Joined` · `Left` · `Online` · `Offline` · `MemberTimeout` · `LeaderChanged` | `who`(`Joined` · `Online`은 `obj`도) |
| `Kicked` | `who` 추방된 사람 · `by` 파티장 |
| `Died` | `who` · `obj` · `arg16` 지역 · `arg32` 쓰러진 자리(위 16비트 x · 아래 16비트 y) |
| `Revived` | `who` · `obj` |
| `Target` | `obj` 고른 파티원 · `target` 그 대상 |
| `Mark` | `who` 파티장 · `target` 집중 대상(npos = 지움) |
| `SettingsChanged` | `arg8` PartyInvitePolicy · `by` |
| `RecruitApplied` | `who` 신청자 · `arg8` 레벨 · `arg16` 직업 · `arg32` 글 번호 |
| `RecruitAccepted` · `RecruitRejected` | `arg32` 글 번호 · `who` 파티장 |
| `RecruitClosed` | `arg32` 글 번호 · `arg8` PartyRecruitCloseReason(Leader · Idle · Disbanded) |
| `ExpShare` | `arg32` 받은 경험치 · `arg8` 내 피해 몫 %(나눠 받은 사람 중) · `arg16` 1 = 쩔 보장 · `target` 몬스터 — 파티가 아니어도 둘 이상 나누면 온다 |

바이트 자리(1바이트 정렬, ObjID = 16B `<HHIQ>`): `PARTY_REQ` op@3 seq@4 arg8@8 arg32@9 obj@13 purpose@29 target@33 text@53 ·
Roster partyId@4 version@8 leader@9 policy@10 max@11 total@12 first@13 count@14 recruitId@15 mark@19 rows@35(줄 49B) ·
Members partyId@4 version@8 count@9 rows@10(줄 17B) · RecruitList count@11 rows@12(줄 76B) · Applicants recruitId@4 count@8 rows@9(줄 22B) ·
`PARTY_NFY` arg8@4 arg16@5 arg32@7 obj@11 target@27 who@43 by@63.

### 순번 계약(seq) — 클라가 지킬 것

- 변경 요청(`Info` · `RecruitList` · `RecruitApplicants` · `Target`을 뺀 모든 op)은 **세션(로그인)마다 1부터 늘어나는 순번**을 단다.
  파티(`PARTY_REQ`) · 결사(`GUILD_REQ`) · 결사 성장(`GUILD_EX_REQ`)은 창이 따로다. 묻기 op의 순번은 응답에 그대로 돌아올 뿐 창에 들지 않는다.
- 소유 Zone이 사람마다 마지막으로 받은 순번을 기억하고(`CUser::m_LastPartySeq`), **그 이하이면 처리하지 않고 `Duplicate`**(param = 마지막으로 받은 순번)를
  돌려준다 — 재전송을 한 번만 처리하려는 것이다. 0은 언제나 Duplicate다. 순번을 받은 요청은 형식 거절(Invalid · TextInvalid …)이어도 창을 올린다.
- 순번은 **파티 상태와 상관없다** — 나가기 · 쫓겨남 · 해산 · 빈 구성(Roster partyId 0) · 초대 만료를 받아도 되돌리지 않는다.
  되돌리면 그 세션의 만들기 · 초대 · 수락 · 신청이 모두 Duplicate가 된다(2026-10-13 보고 「나간 뒤 다시 만들기 · 가입이 안 된다」 — 클라가 나간 뒤 0으로 되돌렸다).
- 다시 접속하면(새 세션 — 서버의 CUser가 새로 만들어진다) 1부터 다시 시작한다. 같은 계정 중복 접속 · 끊긴 뒤 파티원으로 돌아온 경우도 새 세션이다.
  Zone을 옮겨도(같은 세션) 창은 그대로다.
- 응답을 못 받아 **같은 요청을 다시 보낼 때만 같은 순번**을 쓴다 — 서버는 한 번만 처리하고 두 번째에 Duplicate(param ≥ 그 순번).
- **처음 보낸 요청이 Duplicate**면 클라 순번이 뒤처진 것이다: 순번을 `param`으로 맞추고 `param + 1`로 다시 보낸다(서버는 아무것도 바꾸지 않았다).
- Duplicate · 형식 거절은 소유 Zone이 바로, 처리 결과는 서비스가 보낸다 — 둘의 도착 순서는 정하지 않는다(응답은 `seq`로 맞춘다).

## 8. 예전 동작에서 바뀐 것

| 무엇 | 예전(파티 도입 전) | 지금 |
|---|---|---|
| 몬스터 경험치 | 막타 친 사람이 다 | 때린 모두(파티가 아니어도)에게 피해 비례 · 파티 몫 안에서 쩔 보장 |
| 그로시 | 바닥에 떨어진 물건(막타 우선 10초) | 피해 비례로 나눠 바로 지갑(자동 획득) |
| 아이템 | 바닥에(막타 우선 10초) | 막타의 가방에 바로(자동 획득) · 넘치면 바닥(막타 `loot_owner_ms` → 막타의 파티 `loot_party_ms` → 누구나) |
| 희귀품 알림 | 신화를 주우면 월드 공지(버렸다 주워도) | `loot_notice` 희귀도(기본 희귀 이상)를 얻으면 시스템 채팅 · 신화는 월드 공지도 — 몬스터 전리품만 |
| 퀘스트 처치 · 수집 | 막타만 | 때린 모두 |
| 줍기 우선권 해제 | 타이머 `EV_ITEM_LOOT_PRIORITY_EXPIRE` | 주울 때 시각으로 판정(타이머 없앰) |
| 성향치 · 보스 처치 공지 | 막타 | 그대로(막타) |
| 초대를 받아들일 때(2026-10-13) | 초대할 때의 파티가 있으면 들어갔다 | 초대한 사람이 아직 그 파티원이고 초대할 수 있어야 — 아니면 `InviteExpired` |
| 가득 찬 파티의 모집 승인(2026-10-13) | `Full`이면서 신청을 지웠다(알림 없음) | `Full` · 신청은 대기로 남는다 |
| `Duplicate`의 param(2026-10-13) | 0 | 마지막으로 받은 순번(7장 「순번 계약」) |

## 9. 시험

- `python3 Tools/Test/regress_party.py`(인메모리 빌드를 직접 띄운다 — 시험용 GameData 사본으로 시간 · 몬스터 · 드롭을 줄인다):
  manage · offline · recruit · sync · exp(파티 몫 · 쩔 보장 있음/없음) · share(모르는 둘: 경험치 · 그로시 피해 비례 · 막타만 아이템 · 멀리서도 희귀 알림 · 일반 등급은 안 알림) ·
  mix(모르는 사람 + 파티 = 파이썬 `split_kill_exp`) · loot(가방 가득 → 넘친 전리품 세 단계 · 주우면 알림) · quest(막타 · 때린 사람 오름 · 안 때린 사람 그대로).
  샌드박스 이름 `…_FI…`(가방 가득) · `…_Q<번호>…`(퀘스트를 받은 채) — `Tools/Sandbox/MemDBConnection.cpp`.
- `python3 Tools/Test/regress_social_edge.py`(인메모리 빌드를 직접 띄운다 — 파티 · 결사의 가장자리): 나간 뒤 만들기 · 다시 초대 · 추방 뒤 다른 사람의 파티 ·
  모집 신청(rejoin) · 순번 계약(seq — 되돌린 순번 Duplicate · param · 다시 접속하면 1부터) · 초대한 사람이 나감 · 쫓겨남 · 권한을 잃음 · 해산한 파티의 초대(inviter) ·
  가득 찬 파티의 승인 · 다른 파티에 들어간 신청자 · 흩어진 파티의 글(recruit) · 다시 접속 · 중복 접속 · 오래 끊김(relog) · 마지막 사람 · 인원 한도(last) ·
  결사 쪽 guild · gapply · glimit(GUILD.md 7장).
- `RewardRules.h` · `PartyRules.h`의 `static_assert` — 비례 나눔 · 쩔 보장 · 모르는 사람 + 파티 · 두 파티.
- `cmake --build --preset linux-memory --target PartyRuleTests` — 무작위 20만 번씩: `SplitExp`(합 · floor · 비례 · 단조) ·
  `SplitByWeight`(1~17명: 합 · 비례 ±1 · 단조) · `SplitKillExp`(합 · 파티가 아니면 1단계 몫 그대로 · 파티 합 = 파티 1단계 몫 합).
- 소유권 · 경합: `linux-tsan` 빌드로 `regress_party.py --server build/linux-tsan/GameServer` — 경고 0.
  검사기 빌드는 느려 시간에 기대는 시나리오(exp · loot)가 가끔 빗나간다 — 판정은 `linux-memory` 빌드로.
- `datatool check --gamedata` · `datatool verify` · `datatool d2`.

## 클라가 할 일

서버는 모두 준비돼 있다. 클라(Godot)는 아직 이 패킷을 보내거나 그리지 않는다.

0. **순번 계약**(7장): 순번은 세션마다 1부터 늘리기만 한다 — 나가기 · 해산 · 빈 구성을 받아도 되돌리지 않는다. 처음 보낸 요청이 `Duplicate`면 `param + 1`로 다시.
1. 파티 창(구성 · HP/MP 막대 · 직업 · 레벨 · 접속 · 파티장 표시) — `Roster` + `Members`(`version`이 다르면 버린다).
2. 우클릭 메뉴(초대 · 추방 · 위임) · 초대 받음 창(수락 · 거절, `invite_expire_ms` 시계) · 설정(초대 권한) · 해산.
3. 미니맵 파티원 점(같은 지역) · 대상 공유 표시 · 파티장 집중 표시(머리 위 표식) · 사망 · 부활 알림 · 전투 중 표시.
4. 채팅 탭 `Party`(7) · `ChatNoParty` 문구 · 시스템 채널(2) 전리품 알림 줄(보낸 이 이름을 눌러 귓속말 등).
5. 모집 게시판 창(목적 탭 · 페이지 · 빈자리 · 올리기/고치기 · 신청 · 파티장의 신청자 목록 · 승인 · 거절).
6. 경험치 알림(`ExpShare` — 내 몫 · 피해 % · 쩔 보장 표시 — 파티가 아니어도 온다).
7. 자동 획득 알림 `LootAcquired`(아이템 아이콘 · 이름 · 수량 토스트) · `LootOverflow`(가방이 가득 — 발밑에 떨어뜨림). 그로시는 `ADENA_INF`(+N).
   이제 몬스터를 잡아도 발밑에 물건이 떨어지지 않는다(가방이 찬 때만) — 줍기 단축키 안내를 바꾼다.
