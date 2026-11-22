# PLAYER_SKILL — 플레이어 기술 수명 (REIW 방식)

> 코드: `Server/HARVEN_GameServer/Content/Skill/UserSkillHelper.*`(수명 · 판정 순서) · `UserSkillScheduler.h`(시전 · 일정 · 재사용 대기) ·
> `SkillDetail.*`(한 번의 적중 · 예전 판정) · 표 `Database/GameData/SkillData.h`(SkillAction · SkillHit · SkillKeyMap 시트).
> 몬스터 기술은 [MONSTER_AI.md](MONSTER_AI.md) — 같은 틀(시전 구조체 + 단계 + 한 번만 깨우기)이다.

## 1. REIW와의 대응

| REIW (`Content/Skill`) | 하르벤 플레이어 |
|---|---|
| `CSkillHelper::ExecuteSkill` — CC · 정보 · 대상 방식 · 일정 상태 검증 → `MakeNewSkill` | `CUserSkillHelper::ExecuteSkill` — 사망 · 직업 · 레벨 · 안전지대 · 기절 · 재사용 대기 · MP · 게이지 · 대상 · 사거리 → `Begin` |
| `CSkillBase` (시전마다 게임 객체 + BT) | `UserCast` (시전마다 작은 구조체 — 할당 · 타이머 사슬 없음) |
| `T_Skill_Cast` → `T_Skill_Fire` → `T_Skill_Hit` → `T_Skill_Terminate` | `EnumSkillStage` Cast → Fire → Hit → Terminate |
| `T_Skill_Cooldown` · `CooldownStartType` Start / End | 칸(SkillKey)마다 다시 쓸 수 있는 시각 — 누를 때부터(Start), 토글만 끌 때부터(End) |
| `CSkillScheduler` (IsCooldown · IsUsing · CancelCasting · Clear) | `CUserSkillScheduler` (`keyReadyAt` · `IsChanneling` · `OnMoved` · `Clear`) |
| `CSkillSequence` (무기별 연속 입력 창) | 콤보(`comboStage` · `comboAt`) · 체인(`chainSkill` · `chainStage` · `chainUntil`) |
| `OnUpdate`가 주기마다 BT를 돈다 | 플레이어마다 **가장 이른 일정 하나**에만 깨운다(`EV_SKILL_IMPACT` + 세대 번호) |
| `ExecuteNextSkill(OnDestroy)` · 장판 객체 | 갈래 「장판」 · 맹독 안개 · 울림 · 튕김은 그 시전의 일정으로 남는다(다 돌면 시전이 끝난다) |

## 2. 단계

- **Cast** — 시전 알림(`SKILL_NFY` Start · ChargeBegin · HoldBegin · ToggleOn · ChainStep · Identity)과 시전 창.
  창이 있는 것: 차지(놓을 때 · 최대+0.25초), 홀딩(놓을 때 · 최대 시간 — 창 동안 틱마다 타격), 토글(끌 때 — 틱마다 MP),
  캐스팅(`castMs` — 일정의 `castingUntil` · `castingSeq`가 센다). 나머지는 창 없이 곧바로 발사.
- **Fire** — 이동기(구르기 · 도약 · 잔상 회피 — 못 움직이면 `Blocked`로 거절하고 아무것도 쓰지 않는다), 돌진 · 그늘 걸음,
  특수 동작(함성 · 분신 · 안개 · 그믐 · 결계 · 서약 해방 · 울림 · 장막), 차지 놓음(`ChargeRelease`, 달 가르기 돌진), 홀딩 끝(`HoldEnd`).
- **Hit** — 일정(`UserCastStep`)이 남아 있다: 타격표 한 줄(표의 `at_ms`에 모양을 다시 계산 — 예고를 보고 피할 수 있다),
  홀딩 틱 · 토글 틱, 안개 틱, 장판 틱, 울림, 튕김, 기억한 대상 타격, 예전 판정(+메아리).
- **Terminate** — 남은 일정이 없고 시전 창이 닫혔다 → 일정에서 뺀다.

## 3. 끊김 규칙(예전과 같다)

| 무엇이 | 끊는 것 |
|---|---|
| 걷기(`OnMoved`) | 캐스팅 창(알림 `Cancel` — 창 뒤 타격은 버린다), 움직일 수 없는 차지(`Cancel`) · 홀딩(`HoldEnd`, 마무리 없음) |
| 이동기 사용 | 차지(알림 없음) · 홀딩(`HoldEnd`) · 캐스팅(창 뒤 타격을 버린다 — 창이 지난 뒤라도) |
| 다른 기술(받아들여질 때만) | 다른 차지(`Cancel`) · 다른 홀딩(`HoldEnd`) |
| 다른 캐스팅 기술 | 앞 캐스팅의 창 뒤 타격 |
| 기절(타격 시각) | 그 타격(각성기는 그대로), 홀딩(`HoldEnd`) |
| 죽음 · Zone 이동 중 | 그때 도착한 일정 |
| 되살아남(`Clear`) | 진행 중이던 시전 · 일정 모두(차지 · 홀딩 · 토글 포함 — 예전에는 창이 남아 다음 누름이 무시되거나 토글이 꺼졌다) |

## 4. 깨우기

- 일정은 플레이어마다 한 줄(`steps`)이고, 처리 순서는 이른 시각 → 같은 시각이면 넣은 순서(예전 타이머 큐와 같다).
- 요청 · 깨우기 처리가 끝날 때 가장 이른 일정에 `EV_SKILL_IMPACT`를 하나 건다. 이미 그 전에 깨우게 걸려 있으면 새로 걸지 않는다.
  더 이른 일정이 생기면 세대(`wakeEpoch`)를 올려 새로 걸고, 앞의 깨우기는 도착해서 번호가 맞지 않아 버려진다.
- 예전에는 타격 · 틱마다 타이머 하나(타이머 스레드 → 전역 조회 잠금 → Zone 큐)였다. 지금은 플레이어 하나에 걸린 타이머가 많아야 하나다.

## 5. 시험

- `Tools/Test/regress_player_skill.py` — 세 직업 × 모든 칸 × 끊김 경로(놓음 · 시간 끝 · 취소 · 걷기 · 다른 기술 · 이동기) ×
  갈래 구성(없음 · Ⅰ1/Ⅱ1 · Ⅰ2/Ⅱ2)을 훈련장 허수아비에게 써 보고 시전자가 받은 기술 패킷을 시각과 함께 적는다.
  - `--base <옛 서버>`: 같은 시나리오를 옛 서버와 함께 돌려 패킷 · 순서 · 시각(±70ms)을 맞대어 본다 — 구조를 고칠 때 손맛이 그대로인지.
  - 비교에서 빼는 것: 피해량 · HP(무작위), 치명타 비트, MP(자연 회복 시각), 서버 시각. 남은 재사용 대기 · 상태 남은 시간은 ±30ms.
  - 시험용 표 사본은 재사용 대기 ≤ 1초 · MP 소모 0(두 서버가 같은 사본).
- Debug 빌드는 `ASSERT_OWNED_BY_CURRENT_ZONE`이 켜진다 — 일정은 소유 Zone 스레드에서만 돈다.
