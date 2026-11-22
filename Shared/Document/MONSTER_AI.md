# MONSTER_AI — 몬스터 행동 · 기술 · 장판 (패턴 만드는 법)

> 표: `MonsterSkill` · `MonsterSkillHit` · `MonsterAoE` · `MonsterAI` · `MonsterAISkill`(드라이브 `HARVEN_DATA/<표>_Table`) — 검증 규칙 R105~R113([DATA_RULES.md](DATA_RULES.md)).
> 코드: 행동 트리 `Libs/Base/BehaviorTree/` · 몬스터 행동 `Server/HARVEN_GameServer/Entity/Component/Behavior/` · 기술 `Content/Skill/MonsterSkillHelper.*` · 장판 `Entity/Subject/Skill/AreaOfEffect.*`.
> 시험: `python3 Tools/Test/regress_monster_ai.py`(서버를 직접 띄워 돌진 · 버프 장판 · 회복 · 보스 단계를 패킷으로 확인) · `BehaviorTreeTests` · `datatool d2`.

```
몬스터마다 생각 예약 하나(EV_MONSTER_THINK) ─▶ 행동 트리를 한 번 돈다(AI 종류마다 트리 하나 — 불변, 모든 Zone이 함께 읽는다)
                                              └─ 잎(T_)이 "다음에 깨울 시각"을 남긴다 → 그 시각에 다시(가장 이른 것 하나)
바깥 사건 — 보임 · 맞음 · 도발 · 반격 · 기절 · 죽음 ─▶ 곧바로 한 번 더(Poke)
기술 한 번 = 시전(예고) → 발사(이동 · 장판 · 자기 강화) → 타격(시각표) → 후딜 → 재사용 대기      ← MonsterSkill · MonsterSkillHit
장판 = 놓인 자리(또는 주인을 따라)에서 간격마다: 적에게 피해 · 상태 / 동료에게 회복 · 강화            ← MonsterAoE
종족 = 트리 종류 · 감지 · 돕기 · 보스 단계 · 오라 + 기술 칸(우선순위 · 가중치 · 조건)               ← MonsterAI · MonsterAISkill
```

## 1. 돌진 속도 — 어디서 무엇을

**`MonsterSkill.move_ms`(칸당 ms — 작을수록 빠르다).** 지금: 그라목 지옥불 돌진 50 · 사냥개 · 우르간 55 · 늑대 · 서리늑대 60 · 들개 65 · 파약 기사 75 · 곰 85.
돌진 시간 = `move_ms × 칸 수`(늑대 5칸 = 0.3초). 예고 길이는 `cast_ms`, 거리는 `move_tiles` · `range_min/max`.

예전에 느려 보인 까닭(고쳤다): 패턴 돌진이 `SetLastStepMs(70)`을 걸었지만 칸마다 부른 `AStarMove`가 그 값을 몬스터의 **걷는 간격**으로 덮어써서,
`SUBJECT_MOVE_NFY.move_time`이 걷는 속도로 나갔다(클라는 그 시간 동안 보간) — 도약 · 넉백 · 끌어당김도 같은 버그였다.
지금은 끝 칸을 먼저 정하고 `SUBJECT_FORCED_MOVE_NFY(Dash, durationMs = move_ms × 칸)`를 **먼저** 보낸 뒤 한 번에 옮긴다(`CMonsterSkillHelper::ForcedMove`).
넉백 · 끌어당김도 한 번(칸당 95 · 90ms). 돌진 선은 대상 쪽으로 곧게 긋는다(예전엔 8방향으로 잘라 대각선에서 조금 벗어난 대상을 비껴 지나갔다).

## 2. 표

### MonsterSkill — 기술 하나(id 1부터 연속 · 영구)
| 열 | 뜻 |
|---|---|
| `target` | `Enemy`(쫓는 대상 — 거리 `range_min~max` 안일 때) · `Self`(대상이 `range_max` 안일 때, 자기 중심) · `Ally`(`range_max` 안 체력 비율이 가장 낮은 동료 — 회복 장판이면 다친 동료가 있어야) · `AllyGroup`(동료 수 조건) |
| `cast_ms` · `lock_ms` | 시전(예고) 시간 → 발사. 마지막 타격 뒤 멈춤(후딜) |
| `cooldown_ms` · `cooldown_jitter_ms` | 재사용 대기(**시전 시작부터** — 끊겨도 바로 다시 쓰지 않는다). 무리가 한꺼번에 쓰지 않게 0~jitter를 더한다 |
| `pattern` | 클라 예고 연출(차오름 끝 효과): `Charge` · `Slam` · `Ranged` · `Leap` · `None` |
| `fx` | 효과: `Fire` · `Poison` · `Frost` · `Thunder` · `Rune` · `Steam` · `Heal` · `Haste` · `Guard`(맞은 대상 · 장판 · 시전자에) · `BossSlam`(보스 내려찍기 경고 원 + 큰 흔들림 — 첫 타격의 예고를 대신한다) |
| `move` · `move_tiles` · `move_ms` | 발사 순간 이동: `Dash`(대상 쪽 직선, 지형 · 마을 · Zone 경계 · **플레이어 앞**에서 멈춘다) · `Leap`(조준 칸 옆 빈 칸으로 포물선, `move_ms` = 전체 — 클라 0.6초 포물선이라 600 권장) · `Blink`(조준 칸 옆으로 순간이동) · `Retreat`(뒤로 구르기) |
| `counterable` | 예고 중 기절 · 빙결 · 무력화(Broken) · 반격 타격(HF_Counter)에 끊긴다(연타 도중 기절도). 보스 단계 기술은 끄기 권장 |
| `aoe` · `aoe_anchor` | 발사 순간 놓는 장판과 자리 |
| `self_status` · `_ms` · `_power` | 발사 순간 자신에게(예: `Empower` 40 = 주는 피해 +40%) |

### MonsterSkillHit — 타격(기술마다 최대 8, seq 1부터, 시각은 **발사 기준**)
| 열 | 뜻 |
|---|---|
| `at_ms` · `warn_ms` | 타격 시각 · 그보다 몇 ms 앞서 예고(시전 시작보다 앞이면 시전 시작에). **예고를 보내는 순간 중심이 정해진다** — 예고한 자리에 맞으므로 피할 수 있다. 0이면 예고 없이 그 순간 자리 |
| `coeff` | 피해 = 공격력 × %(±15%, 광폭이면 더). 0 = 피해 없이 상태 · 밀기만 |
| `shape` · `a` · `b` | `Target`(한 칸) · `Circle`(체비셰프 반경 a) · `Cone`(반경 a, 반각 b°) · `Line`(길이 a, 폭 b — **a = 0이면 돌진 경로**) · `Ring`(a 바깥 · b 안쪽) |
| `anchor` · `along` | 기준점: `Caster`(그 순간 자기 자리 — 돌진 · 도약 뒤면 도착 자리) · `Aim`(시전 시작 때 대상 칸) · `Target`(그 순간 대상 — 적 기술은 플레이어, Ally 기술은 그 동료). `along` = 조준 방향으로 칸 |
| `react` · `push` | `Knockback`(중심에서 밀기) · `Pull`(몬스터 쪽으로 — 바로 앞에서 멈춘다), 칸 |
| `status` · `status_ms` · `status_stacks` | 맞은 플레이어에게(화상 · 중독은 몬스터 공격력 × Status.tick_percent로 틱 피해) |
| `max_targets` | 가까운 순서로 최대(0 = 무제한) |

잔상 회피 · 무적(이동기 · 각성기)으로 피한 타격은 상태이상 · 밀기도 없다. 피해는 방어 · 철벽 태세(반감 · 반격)를 거친다.

### MonsterAoE — 장판 · 오라(id 1부터 연속 · 영구)
| 열 | 뜻 |
|---|---|
| `duration_ms` · `interval_ms`(≥ 200) · `first_ms` | 지속 · 틱 간격 · 놓인 뒤 첫 틱. 한 번만 터뜨리려면 `duration < first + interval`(예: 치유의 이슬 100 · 200 · 0) |
| `radius` · `affect` | 체비셰프 반경 · `Enemies`(플레이어) · `Allies`(몬스터 — 놓은 몬스터 자신도) · `All` |
| `coeff` · `heal_percent` | 적 틱 피해 = 놓을 때의 공격력(광폭 포함) × %(±10%) · 동료 틱 회복 = 최대 HP × % |
| `status` · `status_ms` · `status_power` · `status_stacks` | 적에게는 해로운 상태, 동료에게는 강화. 동료 강화는 남은 시간이 `간격 + 0.5초`보다 길고 세기가 같거나 크면 다시 걸지 않는다(상태 알림이 틱마다 나가지 않게 — `status_ms`를 간격의 2.6배쯤) |
| `follow` · `owner_bound` | 주인을 따라 움직인다 · 주인이 죽거나 잠들면 사라진다. **둘 다면 오라** — 시간으로 끝나지 않고 주인이 깨어 있는 동안 |
| `fx` | 닿은 대상(적 4 · 동료 6까지)과 가운데 |

마을(안전지대) · 다른 Zone에는 놓이지 않는다(오라는 주인 자리). Zone 스레드마다 표 하나(잠금 없음) — 장판 하나 = 틱 예약 하나.

### MonsterAI — 종족의 행동(종족 하나에 한 행 — 없으면 근접 · 기술 없음)
| 열 | 뜻 |
|---|---|
| `ai` | 트리 종류: `Melee` · `Ranged`(사거리 지키기 + 붙으면 4초에 한 번 물러서기) · `Support`(Ranged와 같다 — 칸을 회복 · 강화 먼저) · `Boss`(단계) · `Stationary`(제자리) · `Dummy`(행동 없음) |
| `aggro_range` | 선공 감지(칸, 0 = 맞을 때만 반격). 선공 종족은 예전 그대로 3 |
| `assist_range` | 이 칸 안의 동료가 싸우기 시작하면 함께 싸운다(도우러 온 몬스터는 다시 부르지 않는다 — 연쇄 없음) |
| `keep_range` | 원거리 · 지원이 대상과 지키는 거리 |
| `skill_gap_ms` · `first_skill_ms` | 기술 사이 최소 간격(후딜 끝부터) · 전투 시작 뒤 첫 기술까지 |
| `phase2_hp` · `phase3_hp` · `phase2_skill` · `phase3_skill` | 보스 단계 문턱(체력 %)과 들어설 때 곧바로 쓰는 기술(진행 중인 기술을 끊는다 — 2단계를 건너뛰어 바로 3단계면 3단계 기술) |
| `aura` | 깨어 있는 동안 두르는 오라(`follow` 장판) |

### MonsterAISkill — 기술 칸(종족마다 최대 8, seq 1부터)
고르는 법: **우선순위가 가장 높은 쓸 수 있는 칸들 중 가중치 무작위**(방금 쓴 칸은 가중치 절반 — 보스가 한 기술로 굳지 않게) → `chance` 굴림(실패하면 그 칸은 1.5초 쉼).
"쓸 수 있다" = 단계(`phase_min~max`) · 재사용 대기 · `hp_below`(내 체력 % 이하) · 기술 대상 조건(거리 · 돌진 첫 칸이 열림 · 동료) · `allies_min`(사거리(최소 3칸) 안 동료 수).

### Status에 더한 셋 — 세기(%)는 거는 쪽(기술 · 장판의 power)이 정한다
`Empower`(광폭 — 주는 피해 +세기%) · `Fortify`(굳건 — 받는 피해 −세기%, 최대 90 · R113) · `Swift`(질풍 — 이동 · 공격 간격 ÷ (1 + 세기%)).

## 3. 기술 하나가 도는 순서(엔진 규칙)
1. **고르기**(트리의 `T_UseSkill`이 생각마다): 시전 중 · 기절(공격 불가) · `skill_gap` · `first_skill` 대기면 넘어간다 → 위 "고르는 법".
2. **시전**: 조준(대상 칸 · 동료 칸)과 방향을 정하고 재사용 대기를 건다. 예고(`MONSTER_TELEGRAPH_NFY`)는 타격마다 `warn_ms` 앞서 — 직선은 시작 → 끝, 나머지는 원(부채꼴 · 고리는 지금 클라가 원으로 그린다 — SERVER-003).
   `BossSlam`이면 `EFFECT_NFY(BossSlamCast, 반경)`가 첫 타격의 예고를 대신한다.
3. **끊김**(`counterable`): 예고 중 기절 · 빙결 · 무력화 · 반격 타격 → 예고 끊김 알림(시간 0) · 다음 기술까지 `skill_gap`의 절반.
4. **발사**: 이동(`FORCED_MOVE` 먼저) → 자기 강화 → 장판 → (타격 없는 기술이면) 효과.
5. **타격**: 시각표대로 — 판정 · 피해(`DAMAGE_BOSS_SKILL`) · 상태 · 밀기 · 효과. 몬스터가 반격에 쓰러지면 그 자리에서 멈춘다.
6. **후딜** `lock_ms` → 끝. 다음 기술은 `skill_gap_ms` 뒤.

## 4. 패턴 만드는 법(시트만 — 코드 수정 없음)
값만 바꾸면 개발 서버(`--dev`)가 핫 리로드로 바로 쓴다. 행을 더하면 서버 재시작.

- **일반 몬스터에 돌진**: MonsterSkill 행(`target Enemy` · `range 2~5` · `cast 600` · `move Dash` · `move_tiles 5` · `move_ms 60`) + Hit 1행(`at_ms 150` · `warn_ms 600` · `coeff 160` · `Line` · `Caster` · `a 0` · `b 1`) + MonsterAISkill 칸.
  돌진 중 맞는 시각(`at_ms`)은 돌진 시간의 절반쯤이 자연스럽다.
- **원거리**: `ai Ranged` · `keep_range 3` · `skill_gap_ms 600~800` + 재사용 대기가 짧은(2초 남짓) `Ranged` 기술 하나 — 원거리 몬스터의 "평타" 노릇.
- **버프 장판(깃발 · 북)**: MonsterAoE(`affect Allies` · `status Empower`(또는 `Fortify` · `Swift`) · `status_power 25` · `status_ms 2600` · `interval 1000` · `radius 3`) +
  MonsterSkill(`target Self` · `aoe` · `aoe_anchor Caster`) + 칸 `allies_min 1`(곁에 동료가 있을 때만). 예: 그락 전사 「전쟁 깃발」 · 칼둔 전사 「전쟁의 북」.
- **오라(늘 두르는 강화)**: MonsterAoE(`follow` · `owner_bound` 둘 다) → MonsterAI `aura`. 예: 백골 수비병 「뼈 결계」(주변 동료 굳건 20%) — 먼저 잡아야 하는 몬스터.
- **회복 지원**: `ai Support` · `assist_range 4` · 칸 우선순위 「치유의 이슬」(`target Ally` · 장판 `heal_percent 20` 한 번 · `aoe_anchor Target` — 움직이는 동료를 따라간다) >
  「회복의 숲」(`AllyGroup` · `allies_min 2`) > 공격 기술.
- **보스 단계**: `ai Boss` · `phase2_hp 60` · `phase3_hp 30` · `phase2_skill` · `phase3_skill`(`counterable` 끔) + 칸마다 `phase_min/max`.
  단계 기술에 `aoe`로 오라 장판(`follow` + `owner_bound`)을 달면 그 단계부터 끝까지 두른다(예: 모로즈 「혹한의 포효」 → 혹한 오라 둔화).
- **쫓아오는 연속 분출**: Hit 여러 행을 `anchor Target` · `warn_ms 800`으로 시각만 다르게(0 · 700 · 1400) — 예고 순간의 플레이어 자리에 하나씩(용암 석인 「용암 분출」 · 즈마이 「유성 낙하」).
- **끌어당기고 내려찍기**: Hit 1(`Circle a 5` · `react Pull` · `push 3` · 피해 작게) → Hit 2(`at_ms 700` · `warn_ms 700` · `Circle a 1` · 큰 피해) — 세베린 「망자의 손아귀」.
- **위기 때 자기 강화**: `target Self` · `self_status Fortify 50` + 칸 `hp_below 50`(갑주게 「껍질 웅크리기」) · `Empower 40` + `hp_below 40`(「광폭」).

**행동 자체가 다른 몬스터**(새 보스 기믹 — 소환 · 분열 · 지형 파괴 …)만 코드: `MonsterAiType`에 종류 하나 · `MonsterBehaviorHelper.cpp`에 트리 하나(`CreateXxxBT`) · 필요하면 잎(`Task/Monster/…/T_Xxx` — `OnEnter` · `OnUpdate` · `OnLeave`)을 더한다.

## 5. 행동 규칙(트리 · 상태)
```
Melee      [전투? → (기술 > 평타 > 추격)] > [귀환? → 귀환] > 대기
Ranged     [전투? → (기술 > 물러서기(4초에 한 번) > 평타 > 사거리 지키기)] > 귀환 > 대기      (Support도 같다)
Boss       [전투? → (단계 기술 > 기술 > 평타 > 추격)] > 귀환 > 대기
Stationary [전투? → (기술 > 평타 > 제자리)] > 대기
```
맨 위가 우선 선택(ReactiveSelector) — 앞 가지가 이기면 달리던 뒤 가지를 끊는다(시전 중이면 예고 끊김 알림).
- **깨우기 · 잠**: 행동(트리 상태 192바이트 + 위협 · 기술 일정)은 깨어 있는 동안만 풀에서 꺼내 붙인다(몬스터 20만 중 깨어 있는 것만 메모리). 선공 · 보스 · 제자리 종류는 대상이 없으면 곧바로 잠들고, 비선공은 보는 사람이 있는 동안 배회한다.
- **대상**: 도발(맹세 함성 — Taunt 상태를 건 사람) > 위협(준 피해 합 — 지금 대상보다 **10% 넘게** 앞서야 바꾼다) > 감지(선공 — `aggro_range` 안 가장 가까운 사람). 마을 · 은신(밤 장막 — 쫓던 몬스터가 잊는다) · Zone 밖 · 시야(9칸) 밖은 놓친다.
- **비선공**: 맞으면 반격한다(위협 = 피해). 예전처럼 곁을 지나가는 사람을 먼저 치지 않는다.
- **돕기**: 전투를 시작한 몬스터가 `assist_range` 안의 쉬고 있는 동료를 부른다(지금 데이터는 잎새 정령만 4칸).
- **리쉬 · 귀환**: 스폰에서 `Monster.leash`(보통 7 · 보스 12) 넘게 끌려가거나 Zone 경계에 닿으면 집으로 걸어가고(그동안 맞아도 반격하지 않는다 — 끌고 가서 깎는 악용 막기), 닿으면 체력 · 보스 단계 · 기술 대기를 처음으로.
- **보스**: 체력 문턱마다 단계가 오르고 단계 기술을 곧바로 쓴다. 예고 중 무력화 게이지가 비면(Broken) 끊긴다(`counterable` 기술).

## 6. 지금 데이터(시드 — 시트에서 자유롭게 고친다)
| 종족 | 트리 | 기술(칸 우선순위 순) |
|---|---|---|
| 수액덩이 · 그락 약탈꾼 | Melee | 없음(처음엔 쉽게) |
| 들개 · 회색늑대 · 서리늑대 · 칼둔 사냥개 | Melee(선공) | 돌진(65 · 60 · 60 · 55ms/칸, 둔화 · 화상) |
| 불곰 | Melee | 곰 돌진(넉백) · 앞발 후려치기(부채꼴 넉백) |
| 붉은갓버섯 · 걷는 망자 | Melee | 포자 터뜨리기(중독) · 썩은 숨(부채꼴 중독) |
| 이슬덩이 · 굴개미 · 모래전갈 · 붉은전갈 · 그늘거미 | Melee(선공) | 덮치기(도약) · 독침(중독 2) · 거미줄(둔화) |
| 그락 창잡이 · 떠도는 망령 · 어린 비룡 · 비룡 | Ranged | 창 던지기 · 원혼의 손길 + 망령 점멸 · 화염구(화상) + 화염 숨결 |
| 잎새 정령 | Support(돕기 4) | 치유의 이슬 > 회복의 숲 > 잎날 던지기 |
| 그락 전사 · 칼둔 전사 | Melee(선공) | 광폭(체력 40% 이하) > 전쟁 깃발(광폭 장판) · 전쟁의 북(질풍 장판) > 내려찍기 |
| 백골 수비병 | Melee + 뼈 결계 오라(굳건 20%) | 방패 밀치기(넉백 2) |
| 갑주게 · 파약 기사 | Melee | 껍질 웅크리기 · 굳건(체력 50% 이하) · 방패 돌진(넉백 2) |
| 얼음 석인 · 고대 석인 · 용암 석인 · 진흙덩이 | Melee | 서리 내려찍기(서리 지대) · 땅 가르기(직선 4) · 용암 내려찍기(용암 웅덩이) + 용암 분출(3연속 추적) · 진흙 튀기기 |
| 모로즈 | Boss | 서리 강타 · 얼음 가시 / 2단계 혹한의 포효(혹한 오라) / 3단계 빙하의 분노(광폭) + 빙하 낙하 |
| 우르간 | Boss | 대지 강타 · 돌격 / 2단계 전투 함성(광전사의 기운) + 회전 베기 / 3단계 도약 강타 |
| 세베린 | Boss | 망자의 일격 · 망자의 손아귀(끌어당겨 내려찍기) / 2단계 사령 강타 + 죽음의 고리 / 3단계 사령의 장막(굳건 + 망자의 땅) |
| 굴개미 여왕 | Boss | 여왕의 짓밟기 · 산성 분사 · 독 안개 투척 / 2단계 여왕의 비명(질풍) / 3단계 여왕의 분노(광폭) + 땅굴 습격 |
| 즈마이 | Boss | 화염 숨결 · 날개 강타 / 2단계 비상(도약 + 용암) / 3단계 용의 분노(광폭) + 유성 낙하 |
| 그라목 | Boss | 지옥불 강타 · 지옥불 돌진(50ms/칸) / 2단계 지옥문(지옥불 오라) + 종말의 불길(고리 → 원) / 3단계 마왕의 분노(광폭 40) |

## 7. 개발자 메모
- **생각 예약**: 몬스터마다 하나(`m_ThinkEpoch` — 더 이른 예약으로 바뀌면 옛 예약은 번호가 맞지 않아 버린다). 잎이 남긴 가장 이른 시각, 없으면 0.1초 뒤, 바닥 20ms.
  트리를 도는 중에 몬스터가 죽으면(철벽 태세 반격) 행동을 돌려주지 않고 생각이 끝난 뒤 재운다(`m_bThinking` · `m_bSleepAfterThink`).
- **시각**: `GetNowTime()`은 49.7일마다 한 바퀴 — 모두 차이로 비교(`TimeReached`). 깨어날 때 모든 시각 칸을 "지금"으로 시작하고, 풀린 쿨다운은 지운다(오래된 시각이 미래로 보이지 않게).
- **길 찾기**: 추격 · 귀환은 먼저 목표 쪽 한 칸(막히지 않으면 A* 없이), 막히면 A*(`FindPath`, 넓히기 4096칸 상한 — 넘으면 1초 동안 같은 목표로 다시 찾지 않는다).
  A*의 칸 기록(g · 어디서 · 닫힘)은 스레드마다 하나인 표를 세대 번호로 비워 다시 쓴다(할당 없음) — 예전 unordered_map · set과 같은 길(무작위 지도 42,000건 대조 차이 0).
- **트리**: `BT_Builder<CMonsterAgent>`로 만든 불변 트리(노드 배열) + 몬스터마다 노드 상태 8바이트 × 24. 핫 경로에 `std::function` · 잠금 없음. 데코레이터: Condition(틱마다 다시 보고 거짓이면 끊는다) · Cooldown · TimeLimit · Inverter · Succeeder · Failurer, 합성: Sequence · Selector · ReactiveSelector · SelectorRandom · Parallel.
- **REIW와 다른 점**: REIW는 종마다 트리를 만들고(공격 기술을 트리 잎에 박는다) 주기마다 OnUpdate를 돌린다. 하르벤은 종류마다 트리 하나를 함께 쓰고, 종마다 다른 것(어떤 기술 · 조건 · 우선순위)은 시트에서 잎이 읽으며, 사건 · 시각에만 돈다.
- 새 상태 · 장판 · 기술 열을 더하면: 스키마 yaml · `MonsterAITypes.h` · `Canon_MonsterAI.h` · `Bind_MonsterAI.cpp`(+ 규칙 · d2 사례) — [DATA_PIPELINE.md](DATA_PIPELINE.md) 3장.
