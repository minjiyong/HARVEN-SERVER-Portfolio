# ECONOMY — 경제 1차: 장비 라인업 · 장비 굴림 · 제작 · 분해(데이터 · 로직)

> 계약(서버 · 클라 공통): 경제 1차 구현 명세(ECON_SPEC) §0 · §1 · §3 · §4 · §9 · §10 — 1~10절은 그중 **데이터 쪽**(표 · 열 · 생성기 · 검증)이다.
> 11~17절은 **로직 1부**(명세 §2 가방 50칸 · §7 제작 · 분해 · §8 경제 로그 — 패킷 · 트랜잭션 · DB · 시험 · 클라가 할 일)다.
> 18~24절은 **로직 2부**(명세 §1 귀속 집행 · §5 처치 보상 · §6 정예 — 필드 · 정예 · 보스 장비 굴림 · 정수 · 결정 · 거래 이력 · 지운 표 · 시험 · 클라가 할 일)다.
> 코드: 열거형 `Database/GameData/Item/ItemTableRow.h`(ItemGrade · ItemRoute · ItemBind · CraftPart) · 인스턴스 비트 `Database/GameData/ItemRules.h` ·
> 파사드 `Database/GameData/EconRules.h`(구현 `Database/GameData/Load/Bind_Econ.cpp`) · 스키마 `Tools/DataTool/schema/econ.yaml` · `item.yaml` ·
> 생성기 `Tools/DataTool/dt/lineup.py` + `Tools/DataTool/lineup/phase1.yaml` · 검증 R124~R135([DATA_RULES.md](DATA_RULES.md)) · D2 `Tools/DataTool/d2/econ.py`.
> 굴림 코드: 순수 함수 `Content/Reward/LootRoll.h`(시험 `LootRuleTests`) · 굴림 · 지급 `Content/Reward/LootHelper.cpp` · 누가 무엇을 `Content/Reward/RewardHelper.cpp` ·
> 정예 `CMonster::SetElite` · `CMonsterHelper::RollElite`. 회귀 시험 `Tools/Test/regress_economy.py`(23절).

## 1. 한눈에

| 무엇 | 어디 |
|---|---|
| 등급 6 · 단 3 · 얻는 길 6 · 귀속 2 | `ItemGrade`(끝에 `eMythic`) · Item `rank` · `route`(flags:ItemRoute) · `bind`(enum:ItemBind) |
| 장비 라인업 113종(일반~희귀 91 + 기존 영웅 · 전설 22) · 재료 6종 | `Bin/GameData/Item.json` ← `datatool lineup`(phase1.yaml) |
| 필드 장비 · 정수 · 결정 확률(지역 단계 1~5) | `TierLoot` |
| 정예 상수 · 천장 · 직업 무기 바꾸기 · 파편 · 거래 분해 | `LootConst`(한 행) |
| 정예 장비의 등급 · 단 | `EliteGear`(단계마다) |
| 보스 개인 굴림 | `BossLoot`(보스마다, item 0 = 없음) |
| 제작 레시피(등급 × 단 × 부위 묶음) | `CraftRecipe`(21행 — 생성기가 무기 레시피에서 만든다) |
| 분해(등급마다) | `Dismantle`(6행) |
| 귀속 · 거래 이력 | 인스턴스 bit 8 · 9(`ItemInstance::bound` · `traded`), `ItemRules::IsBound` · `OnAcquire` |
| 드롭 표 | `DropEntry`에 장비 없음(R126) — 소모품만. 장비는 위 표의 등급 굴림으로만(옛 `GearDrop` · `GearChance` · `ClassWeapon`은 지웠다 — 22절) |
| 희귀도 굴림 | 없음 — 장비 희귀도 = 등급 바닥(`FloorRarity`). `RarityStep`은 모든 행 `w_plus0 = 1000`으로 남겨 두었고 **1차에는 쓰지 않는다**(22절) |
| 처치 보상 · 정예 · 보스 개인 굴림 | 18~20절(`CRewardHelper::HandleKill` → `CLootHelper`) |
| 귀속 · 거래 이력 집행 | 21절(경매 등록 `AuctionResult::Bound` · 버리기 `ItemBound` · 카오 떨굼 제외 · 경매 구매 bit 9) |

## 2. 등급 · 단 · 얻는 길 · 귀속

`ItemGrade` 열거자 이름은 옛 「바닥」 이름이라 뜻을 아래로 고정한다(값은 바꾸지 않는다 — 끝에만 추가).

| ItemGrade | 등급 | 바닥 희귀도(FloorRarity) | 착용 레벨 | tier 열 | 1차 행 |
|---|---|---|---|---|---|
| eNone | 일반 | Common | 0 | 1 | 30 |
| eRare | 고급 | Uncommon | 5 | 2 | 31 |
| eEpic | 희귀 | Rare | 12 | 3 | 30 |
| eUnique | 영웅 | Heroic | 18 | 4 | 18 |
| eLegendary | 전설 | Legendary | 26 | 5 | 4 |
| eMythic | 신화 | Mythic | 35 | 5 | 0 |

- **단**(`rank`): 0 없음(장비 아님) · 1 하 · 2 중 · 3 상. 같은 등급의 「상」 성능 = 다음 등급 「하」(아래 [공식](#6-라인업-생성기)).
- **얻는 길**(`route`, `ItemRoute` 비트): `Shop=1 · Field=2 · Elite=4 · Boss=8 · Craft=16 · Quest=32`. 시트에서는 이름을 `|`로(`Field|Craft`). 장비만.

  | 칸 | 하 | 중 | 상 |
  |---|---|---|---|
  | 일반 | Field(+Shop 기존 상점품) | Field · Craft | Craft |
  | 고급 | Field · Craft(+Shop) | Craft(+Shop: 도브란의 단검) | Elite |
  | 희귀 | Field · Elite · Craft | Craft | Elite |
  | 영웅 | Elite · Craft | 없음(2차 불씨 레시피) | Boss(세트) |
  | 전설 · 신화 | Boss | Boss | Boss |

  퀘스트 보상 장비는 `+Quest`, Shop이 파는 장비는 `+Shop`(생성기가 Quest.json · Shop.json을 보고 더한다 — R128 · R129가 양쪽을 맞춘다).
- **귀속**(`bind`): `eAcquire` = 얻는 순간 귀속 — 영웅-상(보스 세트) · 전설 · 신화 장비, 재료 중 장비 파편(66). 겹치는 아이템의 귀속은 재료만(R135).
- **직업**: 모든 무기는 한 직업(`class_mask` — 서약기사 `Knight` = Sword · Greatsword · Saber, 각인술사 `Engraver` = Staff, 밤사냥꾼 `NightHunter` = Dagger). 방어구 · 장신구는 비운다(모두).

## 3. Item 새 열 · 인스턴스 비트

Item 열은 **끝에** 붙였다(`…, req_level, rank, route, bind`) — `ItemTableRow`도 `reqLevel` 뒤에 같은 순서(집합 초기화가 그대로 컴파일된다).

| 열 | 형 | 기본 | 뜻 |
|---|---|---|---|
| `rank` | u8(0~3) | 없음(값을 적는다) | 단. 장비 1~3, 장비가 아니면 0(R124) |
| `route` | flags:ItemRoute | `""` | 얻는 길. 장비가 아니면 비운다(R124) |
| `bind` | enum:ItemBind | `eNone` | `eAcquire` = 획득 귀속 |
| `grade`(기존) | enum:ItemGrade | — | 장비: 등급. **재료는 표시 색 전용**(EffectiveRarity는 비장비를 Common으로 본다 — 옵션 · 알림 없음) |
| `tier`(기존) | u8(1~5) | — | 장비는 등급 단계(일반 1 … 전설 · 신화 5) — 옵션 수치 범위(`OptionRange`)가 이 값을 쓴다 |

인스턴스(64비트 `extra`, DB `Inventory.extra`):

| 비트 | 뜻 |
|---|---|
| 0-2 · 3-6 · 7 | 희귀도 · 특수 효과 · 강화된 특수 효과(그대로) |
| **8** | `bound` — 귀속(`ItemRules::kBoundBit`) |
| **9** | `traded` — 경매로 산 이력(`kTradedBit`) — 분해량 `traded_dismantle_percent` |
| 10-61 · 62-63 | 옵션 4줄 · 예약 0(그대로) |

- `Pack` · `Unpack`이 두 비트를 보존한다(컴파일 시험 `static_assert`).
- `ItemRules::IsBound(row, extra)` = `row.bind == eAcquire || extra & kBoundBit` — 겹치는 재료는 extra가 없어 행으로만.
- `ItemRules::OnAcquire(row, extra)` = 장비이고 `row.bind == eAcquire`면 `extra | kBoundBit`(새로 만드는 모든 장비에 — 필드 · 정예 · 보스 · 제작 · 퀘스트 · 시작 장비 · 상점 구매).
- `ItemRules::IsTraded(extra)` · `FloorRarity`의 `eMythic → Mythic`.

## 4. 재료 6종(Item 행, `eMaterial` · 겹치기 999 · rank 0 · route 없음)

| id | code_name | name | 표시 이름 | grade(색) | bind | sell | icon |
|---|---|---|---|---|---|---|---|
| 61 | HuntEssenceLow | Low Hunt Essence | 하급 사냥 정수 | eNone | eNone | 2 | 80 |
| 62 | HuntEssenceMid | Mid Hunt Essence | 중급 사냥 정수 | eRare | eNone | 4 | 81 |
| 63 | HuntEssenceHigh | High Hunt Essence | 상급 사냥 정수 | eEpic | eNone | 8 | 82 |
| 64 | AshCrystal | Ash Crystal | 잿빛 결정 | eEpic | eNone | 20 | 83 |
| 65 | DeepAshCrystal | Deep Ash Crystal | 짙은 잿빛 결정 | eUnique | eNone | 50 | 84 |
| 66 | GearShard | Gear Shard | 장비 파편 | eNone | eAcquire | 0 | 85 |

아이콘 80~85는 임시 번호(클라는 없는 번호에 기본 그림). 코드는 `ItemTableData::Id::GearShard` 등(Gen/ItemIds.h).

## 5. 경제 표(묶음 `econ` — item 다음에 묶는다)

모든 표: 1행 열 키 · 2행 설명 · 3행부터 데이터(시트), GameData 행 순서 = 키 순. 서버는 배열 · 시작 위치 색인으로 바로 찾는다(해시맵 없음).

### LootConst(한 행)
| 열 | 값 | 뜻 |
|---|---|---|
| rare_pity | 20 | 필드 장비 N개 동안 희귀가 없으면 다음 장비는 희귀(그 단계 rare_bp > 0일 때) |
| heroic_pity | 20 | 정예 · 보스 장비 N개 동안 영웅이 없으면 다음은 영웅(그 표에 영웅 칸이 있을 때) |
| class_swap_percent | 60 | 남의 직업 무기가 뽑히면 같은 등급 · 단 · 경로의 자기 직업 무기로 바꿀 확률(%) |
| level_gap | 10 | 몬스터가 받는 사람보다 N레벨 이상 낮으면 정수 · 결정 확률 절반 |
| elite_permille | 20 | 스폰 · 재등장마다 정예 확률(‰, ≤ 1000) |
| elite_min_tier | 2 | 이 단계(Region.tier) 이상 지역에서만 정예(1~5) |
| elite_hp_percent · elite_attack_percent | 400 · 150 | 정예 최대 HP · 공격력(%, ≥ 100) |
| elite_level_bonus | 3 | 정예 방어 · 회피 · 경직 계산 레벨 가산 |
| elite_exp_percent · elite_adena_percent | 300 · 300 | 정예 경험치 · 그로시(%) |
| elite_share_percent | 10 | 정예 보상 자격 — 피해 N% 이상 + 공유 범위 |
| boss_share_percent | 3 | 보스 개인 굴림 자격 — 피해 N% 이상(또는 그런 사람의 파티원 · 공유 범위) |
| shard_item | 66 | 장비 파편(재료 — R133) |
| traded_dismantle_percent | 50 | 거래 이력 장비 분해량(%, 올림) |

### TierLoot(5행 — 행 = 지역 단계 1~5)
| 열 | T1 | T2 | T3 | T4 | T5 | 뜻 |
|---|---|---|---|---|---|---|
| common_bp · uncommon_bp · rare_bp | 45 · 42 · 0 | 45 · 30 · 12 | 45 · 30 · 12 | 45 · 30 · 12 | 45 · 30 · 12 | 일반 몬스터 처치당 일반 · 고급 · 희귀 장비(만분율, 합 ≤ 10000 — R130). T1은 희귀 몫을 고급으로 |
| essence_item · essence_permille | 61 · 300 | 61 · 300 | 62 · 250 | 63 · 200 | 63 · 200 | 사냥 정수(‰) |
| crystal_item · crystal_permille | 0 · 0 | 64 · 3 | 64 · 3 | 65 · 2 | 65 · 2 | 일반 몬스터 결정(0 = 없음) |
| elite_crystal_item · elite_crystal_count | 0 · 0 | 64 · 1 | 64 · 1 | 65 · 1 | 65 · 1 | 정예 결정(자격자마다) |
| elite_essence_count | 5 | 5 | 5 | 5 | 5 | 정예 정수(그 단계 essence_item) |
| elite_gear_percent | 0 | 10 | 6 | 5 | 5 | 정예 장비 확률(자격자마다, %) |

### EliteGear(`tier, seq, grade, rank, weight` — child_of TierLoot)
T2: 고급-상 60 · 희귀-하 40 / T3: 희귀-상 50 · 희귀-하 30 · 영웅-하 20 / T4 · T5: 영웅-하 80 · 희귀-상 20. 고른 (등급, 단)의 **route에 Elite가 있는 장비** 중에서 고른다(R131).

### BossLoot(`species, seq, item_id, weight` — child_of Monster)
보스마다 자격자 한 번 굴림. `item_id 0` = 없음. 무기 행은 그 사람 직업이 못 쓰면 빼고 가중치를 다시 합한다(로직). 1차 값(임시 — 2차 보스 전용 무기 · 용비늘 · 균열 계열이 오면 바꾼다):
모로즈(29) 이레린 51~54 각 62 · 없음 752 / 우르간(30) 서약 47~50 각 62 · 없음 752 / 굴개미 여왕(32) 은늑대 55~58 각 62 · 없음 752 /
세베린(31) 새벽지기(6) 40 · 세트 12종 각 5 · 없음 900 / 즈마이(33) 세트 12종 각 10 · 없음 880 / 그라목(34) 18 · 24 · 28 각 30 · 없음 910.

### CraftRecipe(`id, grade, rank, part, fee, in1_item, in1_count … in4_item, in4_count`)
`part` = `CraftPart`(eWeapon 무기 · eArmor 투구~망토 · eAccessory 목걸이 · 반지 — `EconRule::PartOf`). 빈 재료 칸은 0/0(R132). 제작 가능 = route에 Craft(R125).
생성기가 무기 레시피에서 셋을 만든다: **방어구 = 재료 개수 · 수수료 × 0.5(올림), 장신구 = 방어구 + (희귀 이상) 같은 단계 결정 6(희귀 64 · 영웅 65)**.

| 등급-단 | 무기 재료 | 수수료(무기 / 방어구 · 장신구) |
|---|---|---|
| 일반-중 | 61×15 + 66×2 | 100 / 50 |
| 일반-상 | 61×40 + 66×3 | 300 / 150 |
| 고급-하 | 61×60 + 66×8 | 500 / 250 |
| 고급-중 | 61×150 + 64×5 + 66×15 | 1,200 / 600 |
| 희귀-하 | 62×80 + 64×10 + 66×30 | 2,000 / 1,000 |
| 희귀-중 | 62×200 + 64×30 + 66×60 | 5,000 / 2,500 |
| 영웅-하 | 63×150 + 65×20 + 66×80 | 8,000 / 4,000 |

id = 칸 순서 × 3 + 부위(1~21). **영웅-하 장신구 레시피(21)는 1차에 만들 장비가 없다** — 다음 차수에 영웅-하 장신구가 들어올 자리라 R125는 「레시피의 (등급, 단) 칸에 Craft 장비가 하나 이상」으로 본다(부위까지는 보지 않는다).

### Dismantle(등급마다 한 행 — 행 순서 = 등급, R133)
| grade | shard_count | item_id | item_count |
|---|---|---|---|
| eNone | 1 | 61 | 2 |
| eRare | 3 | 61 | 2 |
| eEpic | 8 | 62 | 3 |
| eUnique | 20 | 63 | 4 |
| eLegendary | 60 | 63 | 4 |
| eMythic | 0 | 0 | 0(분해 불가 — `DismantleFor`가 nullptr) |

거래 이력(bit 9)이면 둘 다 `(n × traded_dismantle_percent + 99) / 100`(`EconRule::TradedAmount`). 강화 수치는 반영하지 않는다.

## 6. 파사드 `EconRule`(Database/GameData/EconRules.h)

| 함수 | 돌려주는 것 |
|---|---|
| `RarePity()` … `TradedDismantlePercent()` | LootConst 열 그대로 |
| `Tier(t)` | TierLoot 행(1..5로 자른다) |
| `EliteGear(t)` · `BossLoot(species)` | 그 단계 · 그 보스의 행(span — 없으면 빈 것) |
| `PartOf(row)` | 부위 묶음(장비가 아니면 `CraftPart::Count`) |
| `RecipeFor(row)` · `RecipeAt(grade, rank, part)` | 레시피(RecipeFor는 route에 Craft가 있을 때만) |
| `DismantleFor(grade)` · `TradedAmount(n)` | 분해 행(분해 불가면 nullptr) · 거래 이력 분해량 |
| `Items(grade, rank, route)` | 장비 중 그 등급 · 단(0 = 모든 단) · route에 그 비트(비트 하나)가 있는 것 — id 순 |
| `ClassWeapons(cls, grade, rank, route)` | 위 중 그 직업이 쓸 수 있는 무기 |
| `HasRoute(row, route)` · `RouteIndex(route)` | route 비트 검사 · 비트 → 0..5 |

`Items` · `ClassWeapons`는 로드 때 미리 만든 색인(등급 6 × 단 4 × 경로 6, 직업마다 한 벌 더)을 span으로 준다 — 필드 장비 굴림 · 직업 무기 바꾸기가 그대로 쓴다.

## 7. 라인업 생성기

```
python3 Tools/DataTool/datatool.py lineup [--dry-run] [--seed-econ] [--yaml <판>.yaml]
```

- 입력 `Tools/DataTool/lineup/phase1.yaml`(한글 주석 — 등급 · 곡선 · 경로 · 귀속 · 레시피 · 매입가 · 아이콘 · 재료 · 무기 · 방어구 계열 · 기존 영웅 · 전설).
- 쓰는 것: `Item.json`(장비 · 재료 행 — 소비품 · 화폐는 그대로, 새 열은 기본값) · `CraftRecipe.json`. `--seed-econ`이면 `seed:` 절로 LootConst · TierLoot · EliteGear · BossLoot · Dismantle도.
  모두 `dt.export.write`로 — `_src.json`(시트 행 = 키 순 + 3) · `ids.lock` · `Gen/ItemIds.h` · `manifest.json`이 같이 바뀐다(오프라인 — 시트를 읽지 않는다).
- **id**: 기존 장비는 `code_name`으로 찾아 id · 이름 · 표시 이름 · 아이콘 · 판매가 · 세트 · 부위 · 무기 종류를 그대로 둔다. 재료는 YAML의 id(61~66). 새 장비는 가장 큰 id + 1부터(1차 67~130) — 같은 YAML로 다시 돌리면 같은 결과.
  Item.json의 장비가 YAML에 없으면 멈춘다(id는 영구).
- **다시 계산하는 열**(매번): grade · rank · tier · req_level · class_mask · attack_bonus · armor_bonus · magic_armor_bonus · route · bind · sell_price.
- **공식**
  - 무기 공격력: 직업별 「하」 기준값 — 서약기사 3 · 6 · 12 · 22 · 36 · (55) / 각인술사 3 · 8 · 15 · 26 · 34 · (44) / 밤사냥꾼 3 · 8 · 15 · 25 · 33 · (42).
    같은 등급 안은 `하 × r^(단-1)`, `r = sqrt(다음 등급 하 / 이 등급 하)`(→ 상 = 다음 등급 하), 반올림(0.5 올림).
  - 방어구 · 장신구: 몸통 방어 지수(등급 하) 3 · 6 · 10 · 15 · 22 · (32)를 같은 방식으로 단 보간 × 부위 배수(투구 0.55 · 갑옷 1 · 장갑 0.4 · 장화 0.4 · 망토 0.45 · 목걸이 0.45 · 반지 0.35)
    → 합을 먼저 반올림(최소 1)하고 계열 성향(방어 90/10 · 마법 35/65 · 균형 65/35 %)으로 방어 · 마법 방어에 나눈다(작은 쪽은 0일 수 있다 —
    칸마다 최소 1로 올리면 작은 부위가 1/1에 묶여 단이 올라도 그대로였다. 리뷰 반영 8283d5b).
  - 매입가: 제작 가능품 = `floor(그 부위 수수료 × 50 ÷ 바닥 희귀도 sell_percent)`(R134), 그 밖 = 등급 · 단 곡선 × 부위 배수(무기 1 · 방어구 0.5 · 장신구 0.5).
    상점품의 판매가는 그대로 — 생성기가 되팔기(R090)를 먼저 본다.
  - 새 장비 아이콘 = 같은 종류 · 등급의 기존 아이콘(그림이 생길 때까지).
- 이름: 영문 `name` ≤ 19 ASCII, `code_name` PascalCase · 겹침 없음. 새 방어구는 「<계열> <부위>」(투구 · 갑옷 · 장갑 · 장화 · 망토 · 목걸이 · 반지).

### 1차 라인업(id 순 — 무기의 직업: 기 서약기사 · 각 각인술사 · 사 밤사냥꾼)

| 칸 | 무기 | 방어구 · 장신구(계열 · 성향) | route |
|---|---|---|---|
| 일반-하 | 2 연습용 목검(기) · 19 견습생의 지팡이(각) · 20 사냥용 단검(사) | 여행자(균형): 29 가죽 두건 · 32 누빔 갑옷 · 37 가죽 장갑 · 39 가죽 장화 · 41 여행자의 망토 · 43 뼈 부적 · 45 구리 반지 | Field(+Shop · Quest) |
| 일반-중 | 67 구리 장검 · 69 물푸레 지팡이 · 71 뼈 단검 | 경비대(방어): 73~79 | Field · Craft |
| 일반-상 | 68 경비대 장검 · 70 필경사의 지팡이 · 72 숲지기 단검 | 개척민(마법): 80~86 | Craft |
| 고급-하 | 3 철검 · 21 참나무 지팡이 · 25 흑철 단검 | 사슬(방어): 30 철 투구 · 33 사슬 갑옷 · 92~96 / 각인사(마법): 35 각인사의 법복 | Field · Craft(+Shop · Quest) |
| 고급-중 | 87 단련 철검 · 89 옹이 지팡이 · 15 도브란의 단검 | 단련(마법): 97~103 | Craft(+Shop) |
| 고급-상 | 88 그락 약탈도(곡도) · 90 잎새 지팡이 · 91 약탈자 단검 | 그락 약탈자(균형): 104~110 | Elite |
| 희귀-하 | 4 강철 장검 · 22 각인 지팡이 · 26 월광 단검 | 사냥꾼(균형): 116 사냥꾼 두건 · 36 사냥꾼 가죽 갑옷 · 38 철 장갑 · 40 철 장화 · 42 야행 망토 · 44 은 목걸이 · 46 은 반지 | Field · Elite · Craft(+Quest) |
| 희귀-중 | 16 기병 곡도 · 112 은각인 지팡이 · 114 거미독 단검 | 기사단(방어): 117 기사단 사슬모 · 118~123 | Craft |
| 희귀-상 | 111 기사단 장검 · 113 폐허 각인 지팡이 · 115 전갈꼬리 단검 | 폐허 수비대(마법): 124~130 | Elite |
| 영웅-하 | 5 잉걸 장검 · 23 잿불 지팡이 · 27 독니 단검 | 31 기사단 투구 · 34 판금 갑옷(방어) | Elite · Craft(+Quest) |
| 영웅-중 | 17 세베린의 장검 | — | Quest만(퀘스트 12 보상) |
| 영웅-상 | — | 세트 47~50 서약(방어) · 51~54 이레린(마법) · 55~58 은늑대(균형) | Boss(+Quest 48 · 52 · 57), 귀속 |
| 전설-중 | 6 새벽지기(대검) | — | Boss · Quest, 귀속 |
| 전설-상 | 18 칼둔의 처형검(대검) · 24 하르벤 가지 · 28 밤송곳니 | — | Boss(+Quest 24 · 28), 귀속 |

## 8. 검증(R124~R135 — `Bind_Econ.cpp` ValidateEcon)

R124 장비 단 1~3 · 비장비 단 0 · 경로 없음 / R125 Craft ⇒ 레시피, 레시피 칸 ⇒ Craft 장비 / R126 드롭 표에 장비 없음 / R127 BossLoot ⇔ Boss(보스 몬스터 · 가중치) /
R128 상점 ⇔ Shop / R129 퀘스트 보상 ⇔ Quest / R130 TierLoot 확률 합 · 굴린 등급의 Field 장비 · 정수 · 결정은 재료 / R131 EliteGear 칸의 Elite 장비 · 단계 가중치 /
R132 레시피 재료 · 칸 유일 / R133 분해 행 수 · 재료 · 파편 / R134 제작 장비 매입가 ≤ 수수료 50% / R135 겹치는 아이템 귀속은 재료만.
구조 검사(번호 없음): LootConst 한 행 · TierLoot 단계 1부터 연속 · EliteGear · BossLoot seq 연속 · 레시피 단 1~3 · Dismantle 행 순서 = 등급. 시험: `datatool d2`(d2/econ.py).

## 9. 시트 반영 — 했다(2026-10-12)

- 올리기 전 `datatool pull`로 시트 = 이전 GameData(1577972)인지 표 47개를 대조했다 — Shop 행 순서만 다르고 값은 같았다(기획자 수정 없음).
- 드라이브 `HARVEN_DATA`에 `LootConst_Table` · `TierLoot_Table` · `EliteGear_Table` · `BossLoot_Table` · `CraftRecipe_Table` · `Dismantle_Table`을 Drive 커넥터로 만들고
  `Shared/ToolsConfig/DataTool.json` `tables`에 id를 더했다(Shop 뒤) → `push --meta`(_Enums: ItemRoute · ItemBind · CraftPart · eMythic, _README 표 목록) →
  `push --tabs LootConst,TierLoot,EliteGear,BossLoot,CraftRecipe,Dismantle`.
- 열 · 행이 바뀐 표 `Item`(rank · route · bind 열, 행 130) · `DropEntry`(장비 행 제거) · `RarityStep`(+0)은 탭 값을 비우고 `push --tabs Item,DropEntry,RarityStep --force-seed`.
- 왕복: `datatool sync` → 표 53 · 오류 0 · **바뀐 파일 0**. 지운 표 `GearDrop` · `GearChance` · `ClassWeapon` 파일은 `retired`(보관용 — 지워도 된다).
- 서비스 계정 키는 PC `C:\Repository\_secrets`에서 장치 연결로 옮겨 `~/.config/harven/sheets-sa.json`으로 쓰고 바로 지웠다.
- **앞으로** 라인업을 다시 만들면(`datatool lineup`) Item · CraftRecipe의 장비 행은 생성물이다 — `push --tabs Item,CraftRecipe --force-seed`로 올린다
  (시트에서 장비 행을 손으로 고치면 다음 lineup이 덮는다. 튜닝은 `phase1.yaml`에서).

### DataTool.json에 없는 표를 sync · export는 어떻게 다루나(조사 — `dt/pipeline.py` · `dt/export.py`)
- `pull` · `sync` · `watch` · `check`는 `DataTool.json` `tables`의 표만 읽고 검사한다(`sheet_tabs`). 목록에 없는 econ 표는 **시트에서 읽지 않는다.**
- `export.write`는 넘겨받은 표만 쓴다 — **목록에 없는 `Bin/GameData/<표>.json`은 지우지도 바꾸지도 않는다.** `_src.json` · `ids.lock`의 그 표 항목도 그대로 둔다.
  `manifest.json`은 폴더의 `*.json`을 모두 세어 다시 쓰므로 그 표는 **manifest에 남는다**(서버가 계속 읽는다 — 없는 파일이면 로드 실패).
- 묶음 규칙 검사(C++ datacheck)는 GameData 전체를 임시 폴더에 복사하고 시트에서 온 표만 덮어 검사한다 — 목록에 없는 econ 표도 **새 시트 값(Item 등)과 함께 R124~R135로 검사된다**
  (시트에서 Item의 Craft 경로를 지우면 R125로 export가 막힌다).
- 다만 파이썬 스키마 검사(min · max · 열거형 이름 · ref · rows_exact · child_of seq)는 목록의 표에만 돈다 — 목록에 없는 표는 C++ 로더의 형 · 구조 검사와 R번호 규칙만 받는다
  (`verify`는 schema의 모든 표 — 메모리 덤프 바이트 · 행 수를 본다).
- 요약: **목록에 없는 표 = 손으로(또는 생성기로) 관리하는 GameData 파일** — sync해도 남고, 지워지지 않으며, 서버 · 검사기는 계속 읽는다. 시트 파일을 만들어 목록에 넣는 순간부터 시트가 원본이 된다.

## 10. 로직이 할 일 — 2부에서 했다

데이터 1부가 남긴 할 일은 로직 2부(18~24절)로 모두 들어갔다 — 경제 로그 호출(18절) · 정예 표시 `SUBJECT_FLAG_ELITE`(19절) · `ItemBound`(21절) ·
옛 장비 드롭 표 `GearDrop` · `GearChance` · `ClassWeapon`과 로더 · 천장 상수 `ItemRules::kPityRare` · `kPityHeroic`를 지움(22절 — 천장은 `EconRule::RarePity` · `HeroicPity`) ·
`ItemRules::OnAcquire` · `IsBound` · `IsTraded`를 장비 생성 · 경매 등록 · 버리기 · 카오 떨굼 · 경매 구매(bit 9)에(21절, 분해는 1부부터 귀속 장비도 받는다).
- 시작 장비(`Class.starter_weapon` 2 · 19 · 20)는 그 직업 무기라 그대로 장착된다. 상점 무기(2 · 3 · 15 · 19 · 20 · 21 · 25)는 이제 직업 제한 · 착용 레벨(고급 5)이 있다.
- 클라: Item.json 끝 열 `rank` · `route` · `bind`, `ItemGrade` 끝 `eMythic`(표시 이름 「신화」), 재료 grade 색, 아이콘 80~85(재료) · 새 장비는 기존 아이콘 번호. 2부가 더한 것은 24절.

## 11. 가방 50칸(명세 §2)

| 무엇 | 값 · 자리 |
|---|---|
| 칸 수 | `MAX_INVENTORY_SLOTS = 50`(Protocol.h) — 재료 탭 없이 한 가방(클라는 보기 필터 탭) |
| 소지품 목록 | `ITEM_LIST_ACK_PACKET.items[kItemListPageSlots]`, `kItemListPageSlots = 30` — **바이트 모양은 30칸 시절 그대로**(244B) |
| 보내는 법 | `CUserHelper::SendITEM_LIST_ACK`가 **두 쪽**: 칸 0~29(slotCount 30) → 30~49(slotCount 20). 쪽마다 그 범위의 모든 칸을 칸 번호 순으로, 빈 칸은 `itemId 0` |
| 받는 법 | 항목마다 그 칸을 덮어쓴다(쪽 범위 밖은 건드리지 않는다). 예전처럼 「목록이 오면 전부 비우기」를 하면 둘째 쪽이 첫 쪽을 지운다 |
| 칸 「없음」 | `kNoInventorySlot = 0xFFFF`. `ITEM_ENCHANT_REQ.protectionSlot ≥ MAX_INVENTORY_SLOTS`면 보호권 없음 — **예전 「30 = 없음」은 이제 칸 30**(빈 칸이면 Invalid) |
| DB | 표는 그대로(`UserInventory.SlotIndex SMALLINT`) — 칸 30~49는 새 행일 뿐 |
| 판 | `PROTOCOL_VERSION` 2 → 3 |

부하 봇(StressTest)은 첫 쪽(칸 0부터)에서만 추적을 비운다. 샌드박스 `_FI`(가방 가득)은 칸 1~49를 철검으로 채운다.

## 12. 제작 · 분해 패킷(112 · 113 — 명세 §7)

```cpp
// PacketType: CRAFT_REQ = 112 (C→S), CRAFT_ACK = 113 (S→C)
enum class CraftOp : uint8_t { Craft = 0, Dismantle = 1 };
enum class CraftResult : uint8_t { Ok, Invalid, NotAtBlacksmith, Busy, Duplicate, NoRecipe, LackMaterial, LackAdena, BagFull, Equipped, StorageError };
struct CRAFT_REQ_PACKET { unsigned short size; char type; uint8_t op; uint32_t seq; uint16_t npcId; uint16_t itemId; uint16_t slotIndex; };   // 14B
struct CRAFT_ACK_PACKET { unsigned short size; char type; uint8_t op; uint8_t result; uint32_t seq; uint16_t itemId; uint16_t slotIndex;
                          uint32_t fee; uint16_t outItem[2]; uint16_t outCount[2]; };                                                         // 25B
```

| 칸 | 바이트 | Craft | Dismantle |
|---|---|---|---|
| REQ `op` · `seq` · `npcId` | 3 · 4~7 · 8~9 | 0 · 멱등 순번(0이면 Duplicate) · 대장장이(NPC 2) | 1 · 〃 · 〃 |
| REQ `itemId` · `slotIndex` | 10~11 · 12~13 | 만들 장비 id · 0 | 0 · 분해할 칸 |
| ACK `itemId` · `slotIndex` | 9~10 · 11~12 | 만든 장비 · **결과가 들어간 칸** | 분해한 장비 · 비운 칸 |
| ACK `fee` | 13~16 | 낸 수수료 | 0 |
| ACK `outItem[2]` · `outCount[2]` | 17~20 · 21~24 | 0 | [장비 파편, 정수](0 = 없음) |

실패 ACK는 요청의 `itemId` · `slotIndex`를 그대로 돌려준다(fee · out 0). 칸 · 그로시는 성공했을 때만 바뀌고 **바뀐 칸마다 `ITEM_SLOT_INF` + `ITEM_DETAIL_INF`**,
수수료는 `ADENA_INF(delta -fee, reason AdenaReason::Craft = 12)`로 따로 온다(ACK보다 먼저).

| 결과 | 언제 |
|---|---|
| `Ok` | 커밋됐다 |
| `Invalid` | 죽음 · 이동 중 · 모르는 op · 없는 아이템 id · 분해: 빈 칸 · 범위 밖 · 장비가 아님 |
| `Invalid`(분해) | 요청의 `itemId`(0이 아니면)와 그 칸의 물건이 다르다 — 정리 · 이동 사이에 다른 장비가 들어온 칸을 부수지 않는다(2026-10-13) |
| `NotAtBlacksmith` | (2026-10-13부터 보내지 않는다 — 제작 · 분해는 어디서나, `npcId`는 보지 않는다. 열거형 자리만) |
| `Busy` | 강화 · 제작 · 분해가 진행 중(같은 사람) · 경매 요청 처리 중 |
| `Duplicate` | `seq == 0` 또는 `seq ≤ m_LastCraftSeq`(강화와 같은 방식 — 요청마다 올린다) |
| `NoRecipe` | 제작: route에 Craft가 없거나 (등급, 단, 부위 묶음) 레시피가 없음(소비품 포함) · 분해: 분해 행이 없는 등급(신화) |
| `LackMaterial` · `LackAdena` | 재료(모든 묶음 합) · 수수료가 모자람 — 검사 순서: 레시피 → 재료 → 그로시 → 자리 |
| `BagFull` | 결과가 들어갈 자리가 없다(제작: 재료를 덜고도 빈 칸 없음 · 분해: 합칠 묶음 + 빈 칸이 모자람) |
| `Equipped` | 착용 중인 장비는 분해할 수 없다 |
| `StorageError` | DB 반영 실패 — ACK를 보낸 뒤 서버가 연결을 닫는다(보낸 뒤 닫기 `CloseAfterFlush`). 다시 들어오면 DB가 원본 |

**규칙**(코드 `Content/Craft/CraftHelper.cpp` — `CraftPlan::Craft` · `Dismantle`)
- 제작: 재료를 그 id의 **모든 묶음에서 작은 묶음부터**(같으면 칸 번호 순) 덜어 낸다 → 수수료(그로시) → 결과는 **비는 칸 중 가장 앞**(재료로 비워진 칸 포함).
  인스턴스 = `ItemRules::OnAcquire(row, RollInstance(row, FloorRarity(row), 그 사람 직업).Pack())` — 희귀도는 등급 바닥 고정, 아이템 찾기 · 천장 없음.
- 분해: 칸을 비우고 `EconRule::DismantleFor(등급)`의 [파편 `shard_count`, 재료 `item_count`] — 강화 수치는 안 본다, 거래 이력(bit 9)이면 둘 다 `TradedAmount`(절반 올림).
  나온 것은 같은 id 묶음(999 미만, 칸 번호 순)에 먼저 합치고 남으면 빈 칸(방금 비운 칸 포함)부터.

## 13. 트랜잭션 · 칸 잠금

강화와 같은 「준비 → 한 DB 트랜잭션 → 소유 Zone에서 완료」다.

1. 소유 Zone(Route `CRAFT_REQ` → `CCraftHelper::HandleCraft`): seq · 상태 · Busy 확인(대장장이 거리 제한 없음 — 2026-10-13).
2. **계획**(`CraftPlan`) — 가방 사본(`CraftPlan::SnapshotOf`: 칸 50 + 잠긴 칸 + 그로시)만 보고 칸마다 before/after(`DB_ITEM_SLOT_SAVE`)와 그로시 before/after를 만든다. 메모리를 바꾸지 않는다.
3. `CInventory::BeginBatch(save)` — 계획의 **모든 칸을 잠그고**(`PendingTx::Craft`) 수수료를 메모리에서 **예약**한다(저장 없음 — 그 사이 다른 지출은 예약 뒤 값을 본다).
4. `GDBThread->RequestTx(playerId, "CraftTransaction"|"DismantleTransaction")` → `CDBConnection::SaveItemBatch` → `P_ITEM_BATCH_TRADE` 한 트랜잭션(그 사람 샤드 — 앞서 넣은 그로시 · 칸 저장 뒤에 커밋).
5. 완료(DB 스레드): 커밋됐으면 경제 로그 → `Service::PostToOwner` → `CompleteBatch(save, ok, AdenaReason::Craft)` → `CRAFT_ACK`.
   - 성공: 칸마다 after를 메모리에 넣고 잠금을 풀고 칸 알림 · `ADENA_INF`.
   - 실패: 예약한 수수료를 되돌려 저장하고 **칸은 잠근 채** `StorageError` → 연결 닫기(커밋 응답을 잃었을 수 있다 — 강화와 같은 판단).

**잠금(강화와 공용)** — `CInventory`의 `PendingTx { None, Enchant, Craft }` + `std::bitset<MAX_INVENTORY_SLOTS>`(예전 `m_bEnchantPending` · `m_EnchantSlots[3]`를 일반화).
`IsBusy()` = 무엇이든 진행 중, `IsSlotLocked(칸)` = 그 칸이 잠김.

| 동작 | 진행 중일 때 |
|---|---|
| 강화 요청 · 제작 · 분해 요청 · 경매 거래(올리기 · 사기 · 내리기 · 받기) | `Busy`(서로를 Busy로 본다 — 강화 쪽 동작은 그대로) |
| 장착 · 정리 | 거부(같은 부위의 다른 칸이 벗겨질 수 있다) |
| 자리 바꿈 · 버림 · 팔기 · 사용 · 해제 · 경매 등록 · 정산 받기 | 잠긴 칸이면 거부 |
| 물건 넣기(전리품 · 퀘스트 · 줍기 · 상점 구매 `TryAddItem` · `FindSingleSlotFor`) · 카오 떨굼 | 잠긴 칸을 건너뛴다(결과 칸 · 재료 묶음에 섞이지 않는다) |

## 14. DB — `P_ITEM_BATCH_TRADE`(MySQL)

`P_ITEM_BATCH_TRADE(IN in_UserID INT, IN in_Slots LONGTEXT, IN in_AdenaBefore INT, IN in_AdenaAfter INT)` — `setup.sql`(새로 깔 때) · `migrate_economy_phase1.sql`(돌고 있는 DB — 여러 번 돌려도 된다).

- `in_Slots` = JSON 배열(1~50개), 칸마다
  `{"slot":S,"has":0|1,"item":I,"count":N,"equipped":0|1,"enchant":E,"extra":X, "keep":0|1,"newItem":I,"newCount":N,"newEquipped":0|1,"newEnchant":E,"newExtra":X}`.
- 칸마다 **낙관 검사**(`SELECT … FOR UPDATE`): `has = 1`이면 그 행이 그대로 있어야(아이템 · 수량 · 착용 · 강화 · extra), `has = 0`이면 행이 없어야. 그다음 `keep = 1`이면 upsert, 0이면 지운다.
- 그로시는 `in_AdenaBefore ≠ in_AdenaAfter`일 때만: 잠근 `User.Adena = in_AdenaBefore`여야 하고 `in_AdenaAfter`로 바꾼다(분해는 같은 값 — 그로시를 보지 않는다).
- 하나라도 어긋나면 `SIGNAL 45000`(`Stale batch inventory` · `Stale batch adena` · `Invalid batch slots` — 음수 그로시 · 칸 0~49 밖 · 깨진 JSON) → 롤백. 커밋하면 `Ok = 1` 한 줄.
- `schema_verify.sql`은 프로시저 50개(+ `P_ITEM_BATCH_TRADE`) · DB README에 절. `schema_precheck.sql`(user_id 이관 판정 — 38개 목록)은 결사 성장 때처럼 그대로.
- 인메모리 DB 쌍둥이 `Tools/Sandbox/MemDBConnection.cpp` `SaveItemBatch`(같은 검사) · 일지 op `IB|pid|before|after|n|칸…`.

## 15. 시험

| 무엇 | 명령 |
|---|---|
| 제작 · 분해 · 경제 로그 | `python3 Tools/Test/regress_craft.py [--server build/linux-memory/GameServer] [--port 4300] [--only …] [--keep]` — 서버를 직접 띄운다(`--dev` · Bin/GameData 사본) |
| 가방 50칸 · 착용 칸 | `python3 Tools/Test/regress_inventory.py [host] [port]`(띄운 서버에) — 두 쪽 목록으로 칸 50개를 받았는지도 본다 |

`regress_craft.py` 시나리오: pages(두 쪽 · 칸 0~49) · craft(작은 묶음부터 · 수수료 · 가장 앞 빈 칸 · 재료로 비운 칸 · Duplicate · 먼 곳 · NPC 0에서도 Ok ·
LackMaterial · NoRecipe · Invalid) · dismantle(묶음에 합침 · 거래 이력 절반 · Equipped · Invalid) · busy(제작이 DB에 머무는 동안 강화 · 제작 Busy, 장착 거부) ·
enchant(보호권 없음 0xFFFF · 칸 30은 이제 칸) · adena(LackAdena) · bagfull(제작 · 분해) · storage(StorageError · 연결 닫힘 · 다시 들어오면 그대로) ·
economy(`/economy` · economy.log를 모두 더하면 이 시험이 한 일과 같다 · `[Economy]` 요약).

샌드박스(인메모리 DB) 이름 꼬리표 — `lvNN_` 캐릭터에만, 대문자(다른 시험의 소문자 무작위 꼬리표와 겹치지 않게):

| 꼬리표 | 꾸러미 |
|---|---|
| `_CR` | 0: 하급 사냥 정수 61×100 · 1: 61×20 · 2: 장비 파편 66×50 · 3: 잿빛 결정 64×30 · 4: 중급 사냥 정수 62×100 · 5: 철검 3 · 6: 사슬 갑옷 33(거래 이력 bit 9) · 7: 강철 장검 4(착용) · 8: 각인 두루마리 13×5 · 9: 구리 장검 67, 그로시 50000 |
| `_CP` | `_CR` + 그로시 0 |
| `_CF` | `_CR` + 칸 10~49 철검(가방 가득) |
| `_SF` | `_CR` + `SaveItemBatch` · `SaveEnchant`가 늘 실패(StorageError 길 — 이름에 `_SF`가 든 캐릭터 모두) |

대장장이 도브란(NPC 2)의 집은 **(765, 757)**(TownNpcHome — 배회 1칸) — 시험은 `lv20_xy766_758_…`로 곁에 세운다(걸을 수 없으면 (764, 758) · (765, 759) …).
환경 변수(시험용): `HARVEN_DB_BATCH_DELAY_MS=N`(인메모리 `SaveItemBatch`가 N ms 멈춘다 — Busy 시험) · `HARVEN_ECONOMY_PERIOD_SEC=N`(경제 로그 「하루」를 N초로).

## 16. 클라가 할 일

- **가방 50칸**: 칸 번호 0~49. `ITEM_LIST_ACK`는 **두 쪽**(0~29 · 30~49)으로 온다 — 항목마다 그 칸을 덮어쓰고, 목록을 받을 때 가방 전체를 비우지 않는다(빈 칸은 itemId 0으로 온다).
  재료(eMaterial)는 같은 가방에 — 보기 필터 탭(전체 · 장비 · 소비 · 재료)으로 나눠 보인다.
- **보호권 없음 = `kNoInventorySlot`(0xFFFF)** — 예전처럼 30을 보내면 칸 30을 보호권으로 본다(빈 칸이면 강화가 Invalid).
- **`PROTOCOL_VERSION` = 3**(SERVER_CONFIG_INF로도 온다).
- **제작 · 분해**: 어디서나 `CRAFT_REQ`(12절 — 2026-10-13부터 대장장이 곁 제한 없음, 클라는 소지품 「분해하기」 · 메뉴 「제작」) — 요청마다 seq를 올린다. 결과는 `CRAFT_ACK` + 칸마다 `ITEM_SLOT_INF`/`ITEM_DETAIL_INF` + `ADENA_INF(reason Craft)`.
  제작 목록은 Item.json의 `route`에 Craft가 있는 장비 + `CraftRecipe`(등급 · 단 · 부위 묶음 — `EconRule::PartOf`와 같은 묶음)로 클라가 그린다. 분해 미리보기는 `Dismantle` 표 · 거래 이력 bit 9(절반 올림).
  `StorageError` 뒤에는 연결이 끊긴다(다시 접속).
- **`SUBJECT_FLAG_ELITE`(16)**: `SUBJECT_INFO_NFY.flags` — 정예 몬스터 표시(로직 2부가 보낸다 — 19 · 24절).
- **`SystemMessageCode::ItemBound`**(26 — LootOverflow 다음 값): 귀속된 물건이라 경매 등록 · 버리기를 할 수 없다(param1 = 아이템 id, param2 = 칸 — 로직 2부가 보낸다 — 21 · 24절).
- **`AdenaReason::Craft`(12)**: 제작 수수료 — 그로시 변화 문구.

## 17. 경제 로그(명세 §8)

`Content/Economy/EconomyLog.{h,cpp}` — `CEconomyLog`의 정적 함수 · 원자 카운터(아무 스레드에서나 부른다).

| 함수 | 세는 것 | 부르는 곳 |
|---|---|---|
| `OnAdena(AdenaReason, int64 delta)` | 그로시 들어옴(delta > 0) · 나감 — 사유별 | `CInventory::AddAdena` · `TrySpendAdena` · `ReserveAdena` · `ReturnAdena` · `CreditAdenaNoSave` · `TryBuy` · `TrySell` · 제작 커밋(Craft) |
| `OnGearIn(GearSource, ItemGrade[, n])` | 장비 생성 `Field · Elite · Boss · Craft · Quest · Shop · Achievement` × 등급 | 제작 커밋 · 퀘스트 보상 · 상점 구매 · 처치 보상 지급(필드 · 정예 · 보스 — `CLootHelper::Grant`, 18절) · 업적 받기(ACHIEVEMENT.md 7장) |
| `OnGearOut(GearSink, ItemGrade[, n])` | 장비 소멸 `Dismantle · ShopSell · EnchantBreak` × 등급 | 분해 커밋 · 상점 매입 · 강화 증발(커밋 뒤) |
| `OnMaterialIn(MaterialSource, id, n)` | 재료 생성 `Kill · Elite · Boss · Dismantle · Achievement` × 재료 id | 분해 커밋 · 처치 보상 지급(Kill · Elite · Boss — `CLootHelper::Grant`, 18절) · 업적 받기 |
| `OnMaterialOut(MaterialSink, id, n)` | 재료 소비 `Craft · ShopSell` × 재료 id | 제작 커밋 · 상점 매입(eMaterial) |
| `OnCraftFee(n)` | 제작 수수료 합 | 제작 커밋 |

- **하루 경계**: svc-tick 스레드가 1초마다 `Tick(UnixNow())` — `GuildGrowth::PeriodId(now, 86400, 75600)`(06:00 KST)가 바뀌면 지난 하루를
  `$HARVEN_ECONOMY_LOG`(기본: 작업 폴더의 `economy.log`)에 **한 줄 JSON**으로 덧붙이고 stdout에 `[Economy] adena in=… out=… net=… | gear in=… out=… | material in=… out=… | craftFee=…`를 찍은 뒤 0으로.
  서버를 켠 뒤 첫 경계 전까지는 켠 때부터 센다(끌 때는 쓰지 않는다 — 그날 몫은 잃는다).
- **한 줄 형식**(0인 칸은 뺀다 · 재료는 아이템 id 문자열, 1024 이상은 `"other"`):
  ```json
  {"period":20738,"start":1791838800,"day":"2026-10-13","adena":{"in":{"Pickup":1200},"out":{"Craft":300}},
   "gear":{"in":{"Craft":{"eNone":3}},"out":{"Dismantle":{"eRare":2}}},
   "material":{"in":{"Dismantle":{"61":3,"66":5}},"out":{"Craft":{"61":45,"66":6}}},"craftFee":300}
  ```
  (실제로는 한 줄.) `start` = 그 하루가 시작한 유닉스 초, `day` = 그 시각의 KST 날짜.
- **`/economy`**(개발 서버 `--dev`만 — `/dataver`와 같은 문): 지금 값(같은 JSON, 머리 없음 · 되돌리지 않음)을 보낸 사람에게만 일반 대화 줄(보낸 사람 「경제」)로 답한다.
  대화 한 줄은 127바이트라 잘라서 여러 줄 — 이어 붙이면 원래 JSON.

## 18. 처치 보상(명세 §5 — 로직 2부)

`CRewardHelper::HandleKill`(몬스터를 가진 Zone 스레드 — `CUserHelper::AttackMonster`가 부른다). 그로시 · 경험치 · 결사 · 퀘스트는 예전 그대로(PARTY.md 3절)이고 **전리품만 바뀌었다**.
지역 단계 `tier` = **죽은 자리의 `Region.tier`**(`WorldData::FindRegion(GetZoneIdByWorld(죽은 칸))`) — `Monster.drop_table`은 이제 소모품 표 번호일 뿐이다.

| 몬스터 | 소모품 드롭 표 | 장비 | 재료 | 그로시 · 경험치 |
|---|---|---|---|---|
| 일반 | `Monster.drop_table` × `drop_rolls` → **looter** | 필드 장비 한 번 굴림 → looter | 정수 · 결정 — 나눠 받는 사람 중 **피해 비례 추첨 한 명** | 피해 비례(예전 그대로) |
| 정예(19절) | 〃 → looter | 자격자마다 `elite_gear_percent`로 정예 장비 | 자격자마다 결정 × `elite_crystal_count` · 정수 × `elite_essence_count` | × `elite_exp_percent` · `elite_adena_percent` |
| 보스(20절) | 〃(보스 표 — 장비 없음) → looter | 자격자마다 `BossLoot` 한 번 | — | 예전 그대로 |
| 허수아비 | 없음 | 없음 | 없음 | (죽지 않는다) |

looter = 막타(이 Zone에 없으면 가장 많이 때린 사람). 「보스는 장비 3개」 · 확장 세션 장비 굴림(`PickGearForTier` · `GearChance` · `ClassWeaponFor` · `MaybeClassSwap`) · 옛 희귀도 굴림(`RollRarity`)은 지웠다.

**필드 장비**(`CLootHelper::RollFieldGear` — 순수 함수 `LootRoll::PickFieldGrade` · `ApplyRarePity` · `ClassSwap`)
1. `roll ∈ [0, 10000)` — `TierLoot[tier]`의 `common_bp` → `uncommon_bp` → `rare_bp` 순서로 빼 간다. bp마다 × (100 + 아이템 찾기)/100(장비 옵션 `ItemFind` — 음수는 0). 합이 10000을 넘으면 뒤 등급이 준다.
2. 장비가 나왔으면 **희귀 천장**: `m_PityRare + 1 ≥ rare_pity`이고 그 단계가 희귀를 굴리면(`rare_bp > 0`) 희귀로 올린다. 카운터는 희귀면 0, 아니면 +1(1000에서 멈춘다 —
   희귀가 없는 1단계에서도 센다: 2단계 첫 장비가 희귀일 수 있다). 장비가 안 나온 처치는 세지 않는다.
3. 그 등급의 `EconRule::Items(등급, 모든 단, Field)`에서 **균등** 하나 → 남의 직업 무기(무기 · `ClassCanUse` 아님)면 `class_swap_percent`로
   `EconRule::ClassWeapons(내 직업, 등급, 그 단, Field)` 중 균등 하나(없으면 그대로).
4. 인스턴스 = `ItemRules::RollInstance(행, FloorRarity(행), 내 직업)` + `OnAcquire`(획득 귀속 장비면 bit 8). 희귀도는 등급 바닥 고정 — 아이템 찾기는 등급 확률에만.

**정수 · 결정**(`GiveEssence` — 일반 몬스터만): 나눠 받는 사람(살아 있고 죽은 자리에서 `exp_share_range` 안 — 아무도 없으면 막타 · 가장 많이 때린 사람 하나) 중
**피해 비례로 한 명**(`LootRoll::PickByDamage` — 피해 0은 1로)을 뽑고 그 사람에게 `essence_permille`로 정수 1개, `crystal_permille`로 결정 1개(따로 굴림, `crystal_item 0` = 없음).
몬스터가 그 사람보다 `level_gap` 레벨 이상 낮으면(종족 레벨 기준) 두 확률 모두 절반(`LootRoll::GapPermille` — 내림, **`level_gap 0` = 끔**).

**지급**(`CLootHelper::Grant` — 옛 `GiveItems`를 받는 사람마다로 일반화): 가방(`TryAddItem` → `SYSTEM_MESSAGE LootAcquired` · 희귀도 알림 `AnnounceLoot` · 결사 `OnLoot`) →
가방이 차면 죽은 자리 둘레(`PickDropTile` — 처치 한 번 안에서 칸 순번을 이어 간다)에 **받는 사람**의 줍기 우선권(그 사람 > 그 사람의 파티 > 누구나)으로(`LootOverflow`).

**경제 로그**(17절 — 넘쳐 떨어진 것도 생긴 것으로 센다): 장비 `OnGearIn(Field · Elite · Boss, 등급)`, 재료 `OnMaterialIn(Kill · Elite · Boss, id, 수)`
(필드의 정수 · 결정 · 드롭 표의 재료 = Kill). 소비품은 세지 않는다.

**천장 저장**: `m_PityRare`(필드 장비) · `m_PityHeroic`(정예 · 보스 장비)는 예전처럼 진행 기록(UserProgress)에 — 카운터가 0으로 돌아간 순간 `SaveProgress`, 그 사이 값은 로그아웃 때.
계정 = playerId(User 행 하나 = 계정 하나).

## 19. 정예(명세 §6)

| 무엇 | 어디 · 값 |
|---|---|
| 굴림 | `CMonsterHelper::RollElite` — 첫 스폰(`InitForZone` — 자리를 정한 뒤) · 재등장(`HandleRespawn`)마다. 보스 · 허수아비가 아니고 지역 단계 ≥ `elite_min_tier`면 `elite_permille`(‰) |
| 능력치 | `CMonster::SetElite` — 정예면 최대 HP × `elite_hp_percent` · 공격력(`CStat::Offensive` — 평타 · 기술 피해) × `elite_attack_percent`(1 ~ 65535로 자른다), 아니면 종 기본값(`InitInstance`가 적어 둔 값)으로. HP는 가득 |
| 레벨 가산 | `CMonster::CombatLevel()` = 종족 레벨 + `elite_level_bonus`(정예만) — 받는 피해의 방어(`MonsterDefense`) · 플레이어 회피(`EvadeChance`) · 무력화 게이지(보스만이라 지금은 정예와 겹치지 않는다) |
| 표시 | `SUBJECT_INFO_NFY.flags = SUBJECT_FLAG_ELITE`(16) — 정예는 `SUBJECT_ADD_NFY` 뒤에 **늘** INFO(level · hp · maxhp · flags). 재등장 때 정예이거나 **직전 생애가 정예**였으면 `SUBJECT_RESPAWN_NFY` 뒤에 INFO(되돌림 — flags 0 · 종 최대 HP). 이름(19바이트)은 그대로 |
| 보상 | 소모품 드롭 표(looter) + 그로시 · 경험치 × 배율(결사 경험치도 배율을 건 값) + **자격자**마다 결정 × `elite_crystal_count`(그 단계 `elite_crystal_item`) · 정수 × `elite_essence_count`(그 단계 `essence_item`) · `elite_gear_percent`로 정예 장비. 필드 장비 · 일반 정수 굴림은 없다 |
| 자격자 | 피해 ≥ 전체 피해의 `elite_share_percent`% · 살아 있고 공유 범위 안 · 이 Zone 스레드 · 계정당 한 번(`LootRoll::PickClaimants` — 분모 = 피해 기록 전체(플레이어), 다른 Zone으로 간 사람의 몫도 센다) |
| 정예 장비 | `CLootHelper::RollEliteGear` — `EliteGear[tier]` 가중치로 (등급, 단) 칸 → **영웅 천장**: `m_PityHeroic + 1 ≥ heroic_pity`이고 그 단계에 영웅 이상 칸이 있으면 그 칸들 중에서만(`LootRoll::PickEliteCell`). 카운터: 영웅 이상 0 · 아니면 +1 → `Items(등급, 단, Elite)` 균등 → 직업 무기 바꾸기(경로 Elite) → 인스턴스(바닥 희귀도 + 귀속 비트) |

부팅 로그 `[Zone N] Monster Init Success (12500 monsters, E elite + boss)`에 첫 스폰 정예 수가 나온다.

## 20. 보스 개인 굴림(명세 §5)

- **자격자**(`GiveBossRewards` → `LootRoll::PickClaimants(…, boss_share_percent, 공유 조건 없음, 파티 끼기)`)
  - 피해 기록에 있고 이 Zone 스레드에 있는 사람 중 피해 ≥ 전체의 `boss_share_percent`%(살아 있지 않아도 · 멀리 있어도 — 싸우다 쓰러진 사람도 받는다).
  - 위 자격자와 **같은 파티**(`m_PartyId ≠ 0`)이면서 **공유 범위 안**(살아 있음 · 죽은 자리에서 `exp_share_range` 안 · 이 Zone 스레드)인 사람 — 때리지 않았어도.
    죽은 자리 둘레의 섹터 칸을 반경만큼 훑어 찾는다(`ForEachUserNear` — 3×3 이웃보다 넓을 수 있는 반경 ≤ 30).
  - 계정(playerId)마다 한 번 — 피해 기록과 파티원 양쪽에 걸려도 한 번.
- **굴림**(`CLootHelper::RollBossLoot` → `LootRoll::RollBoss`): `BossLoot(종족)`의 행 중 **그 사람 직업이 못 쓰는 무기 행을 빼고** 가중치를 다시 합해 하나. `item_id 0` = 없음.
  장비가 나오면 영웅 천장 카운터(`m_PityHeroic`): 영웅 이상 → 0, 그 밖 장비 → +1(1차 보스 장비는 모두 영웅 이상). 영웅 미만 장비가 천장에 걸리면 영웅 이상 장비 행 중에서 다시 고른다(1차 데이터에는 없는 길).
- 보스의 소모품 드롭 표(`drop_table` 6~9 × `drop_rolls`)는 예전처럼 looter 한 사람.
- 보스 장비는 획득 귀속(bind eAcquire — bit 8)이라 경매 · 버리기가 안 된다(21절).

## 21. 귀속 · 거래 이력 집행(명세 §1)

| 자리 | 귀속(`ItemRules::IsBound(행, extra)` = 행 `bind eAcquire` 또는 bit 8)일 때 |
|---|---|
| 경매 등록 — 소유 Zone 검사(`CAuctionHelper::HandleRequest`) | 거부 — `AUCTION_ACK result = AuctionResult::Bound`(14, 끝에 더함) + `SYSTEM_MESSAGE ItemBound`(param1 아이템 · param2 칸). 경매인 곁 검사(NotAtNpc) 뒤 |
| 경매 등록 — 칸 꺼내기(`CInventory::TakeForListing`) | `TakeResult::Bound` → 같은 알림(그 사이 칸이 바뀐 경우를 막는 두 번째 검사) |
| 땅에 버리기(`ITEM_DISCARD_REQ`) | `ITEM_DISCARD_ACK` 실패 + `ItemBound` — 칸은 그대로 |
| 카오 사망 떨굼(`CInventory::TryTakeRandomItem`) | 후보에서 뺀다(잠긴 칸처럼) |
| 가방이 차서 땅에 넘친 처치 보상(`CItemHelper::CanLoot`) | **주인만** 줍는다(파티 · 누구나 단계 없음), 30초 대신 **2분** 뒤 사라진다 — 가방을 일부러 채워 보스 · 전설 장비를 넘기던 길을 막는다(리뷰 반영). 주인은 ObjID와 **계정(playerId)** 으로 알아본다 — 다시 접속하면 ObjID가 바뀌어 예전엔 주인도 못 주웠다(감사 2026-10-13) |
| 경매 구매(`CAuctionService::Buy`) | 경제 1차 전에 올라온 목록이 지금은 귀속인 물건(보스 세트 · 전설)이면 `Bound`로 거부 — 기간이 끝나면 판 사람에게 돌아간다 |
| 상점 매입 · 분해 · 제작 재료 | 허용(명세 §1) |

재료는 겹치는 아이템이라 extra가 없다 — 장비 파편(66)처럼 **행의 bind**로 판단한다.

**획득 귀속 비트(bit 8)를 켜는 곳**(`ItemRules::OnAcquire(행, extra)` — 장비이고 행이 eAcquire면): 필드 · 정예 · 보스 장비(18~20절) · 제작(1부) · 퀘스트 보상(`QuestHelper::Finish`) ·
시작 장비(`GiveStarterKit` · 직업이 바뀐 옛 캐릭터의 초보자 무기) · 상점 구매(`CInventory::TryBuy`) · 결사 상점(`GuildGrowthService` 구매). 1차 데이터에서 귀속 장비는 보스 · 전설 장비뿐이라 나머지 길은 지금 0이다(데이터가 바뀌어도 맞게).
상점 거래 프로시저 `P_SHOP_TRADE`는 칸의 Extra를 0으로 쓴다(인자에 없다) — 서버는 extra가 있는 칸을 같은 샤드에서 바로 이어 `P_INVENTORY_UPSERT`로 다시 쓴다
(인메모리 DB도 P_SHOP_TRADE처럼 0으로 쓴다 — 감사 2026-10-13, regress_items_edge.py shopbound).

**거래 이력(bit 9)**: 경매로 **산 장비**의 구매자 정산 줄 extra에 `ItemRules::TradedBitFor(행)`(장비면 512, 아니면 0)을 더한다 — 메모리(`CAuctionService::FinishBuy`)와 DB가 같게.
- MySQL `P_AUCTION_BUY(…, in_Now, in_SettleExtraOr BIGINT)` — 9번째 인자(구매자 정산 줄 `Extra = 목록 Extra | in_SettleExtraOr`). `setup.sql` · `migrate_economy_phase1.sql`(여러 번 돌려도 된다 —
  **서버보다 먼저** 돌린다: 서버는 인자 9개로 부른다). `CDBConnection::AuctionBuy(…, settleExtraOr)`.
- 인메모리 DB `ApplyBuy(…, extraOr)` · 일지 op `AB|…|extraOr`(옛 일지는 0으로 읽는다).
- 받기(`PlaceIncoming`)는 정산 줄 extra를 그대로 칸에 — 그 장비를 분해하면 파편 · 정수가 `traded_dismantle_percent`(올림, 12절). 다시 팔아도 비트는 남는다.

## 22. 지운 표 · 쓰지 않는 표

| 표 | 무엇을 | 대신 |
|---|---|---|
| `GearDrop` · `GearChance` · `ClassWeapon` | **지웠다** — 스키마(item.yaml) · `Canon_Item.h` · `Snap_Item.h` · `Bind_Item.cpp` · `Bin/GameData/*.json` · `_src.json` · manifest · `rowcounts.json` · D2 사례 · `DropTable::GearPool` · `GearChance` · `ClassWeaponFor` · `PickGearForTier` | TierLoot · EliteGear · BossLoot · `EconRule::Items` · `ClassWeapons` |
| 그 규칙 R100 · R101 | **폐지**(DATA_RULES.md에 줄을 남김 — 번호는 다시 쓰지 않는다) | R126~R131 |
| `RarityStep` | 그대로(모든 행 +0) — **1차에는 쓰지 않는다**(옛 `ItemRules::RollRarity` · `kPityRare` · `kPityHeroic` · `kMythicGate`는 지웠다. 파사드 `StepWeights`만 남김) | 다음 차수의 「같은 등급 안 희귀도 오름」 자리 |

시트: `Shared/ToolsConfig/DataTool.json`의 `tables`에서 세 표를 빼고 파일 id는 `retired`에 옮겼다(datatool은 읽지 않는다 — 드라이브의 `ClassWeapon_Table` · `GearDrop_Table` ·
`GearChance_Table` 파일은 보관용, 사람이 지워도 된다). 표를 지우는 순서는 DATA_PIPELINE.md 4절.

## 23. 시험(로직 2부)

| 무엇 | 명령 |
|---|---|
| 굴림 순수 함수 | `cmake --build --preset linux-memory --target LootRuleTests && build/linux-memory/Server/HARVEN_GameServer/tests/LootRuleTests` — 등급 bp 경계 · 아이템 찾기 · 희귀 천장 · 직업 무기 바꾸기 · 피해 가중 추첨 · 레벨 차 · 자격자(정예 · 보스 파티원 · 계정당 한 번) · 정예 장비 칸 · 보스 굴림(직업 거르기 · 영웅 천장) · 정예 확률 |
| 처치 보상 · 정예 · 보스 · 귀속 · 거래 이력 · 경제 로그 | `python3 Tools/Test/regress_economy.py [--server …] [--port 4350] [--only …] [--keep]` — 서버를 직접 띄운다(`--dev`) |

`regress_economy.py` 시험용 GameData(값만 — 표 모양 그대로): 몬스터 HP 600 · 레벨 25 · 공격력 1 · 비선공, 모로즈 HP 3000 · 단계 · 기술 없음, LootConst 희귀 천장 4 · 직업 무기 바꾸기 100% ·
정예 1000‰(3단계 이상) · 보스 자격 3%, TierLoot 1단계 일반 9999 · 희귀 1bp / 2단계 일반 4000 · 고급 3000 · 희귀 3000 · 정수 · 결정 1000‰ / 3단계 정예 장비 100%, BossLoot 모로즈 「없음」 0.

| 시나리오 | 보는 것 |
|---|---|
| field | 올벤 숲(2단계): 막타만 필드 장비 처치마다 하나 · route Field · 등급 일반~희귀 · 희귀도 = 바닥 · 무기는 내 직업 · 정수 · 결정은 처치마다 정확히 한 사람(둘 다 같은 사람) · 피해를 더 준 사람이 더 자주 |
| gap | Lv40이 Lv25 몬스터(레벨 차 15 ≥ 10) — 정수 · 결정 1000‰ → 500‰(0도 전부도 아니다) |
| pity | 야스나 초원(1단계): 장비 등급 일반 · 일반 · 일반 · **희귀** · … (천장 4) |
| elite | 캄파나 폐허(3단계): 보이는 몬스터 모두 INFO flags 16 · 최대 HP 2400(600 × 400%) · 경험치 · 그로시 합 × 300% · 피해 10% 이상만 결정 1 · 정수 5 · 정예 장비 하나, 10% 아래는 없음 |
| boss | 모로즈: 피해 3% 이상 둘 + 그 파티원(때리지 않음) — 저마다 보스 장비 정확히 하나(bit 8 · 영웅), 3% 아래 모르는 사람은 없음 |
| boundloot | 야스나 초원 — 시험 사본에서 일반 필드 장비를 획득 귀속으로 · 줍기 단계 2 · 4초: 가방이 찬 주인의 귀속 장비가 발밑에 넘치면 파티원 · 남은 단계가 지나도 못 줍고, 30초가 지나도 남는다(2분) |
| bound | 장비 파편 경매 등록 → `Bound`(14) + `ItemBound` · 버리기 실패 + `ItemBound` · 철검을 다른 사람이 사서 받으면 extra bit 9 |
| economy | `/economy`(+ economy.log) — 필드 · 정예 · 보스 장비 수 · Kill · Elite 재료 수가 이 시험이 받은 것과 같다 |

각인탄은 길목의 다른 몬스터를 맞힐 수 있어 시험은 「조준한 몬스터」가 아니라 **쓰러진 몬스터마다**(SUBJECT_DIE_NFY · 피해 기록) 기대값을 센다.
다른 회귀 시험의 시험용 GameData: `regress_party.py` — TierLoot 희귀 10000bp(처치마다 희귀 필드 장비 → 알림) · 정수 · 결정 0‰ · 정예 0‰, `regress_guild.py` — TierLoot 일반 10000bp ·
정수 · 결정 · 정예 0, `regress_monster_ai.py` — 정예 0‰(HP × 4 정예가 시간을 흔들지 않게). 예전 `GearChance 100%` · `RarityStep +2` 수정은 지웠다.

## 24. 클라가 할 일(로직 2부)

- **정예 표시**: `SUBJECT_INFO_NFY.flags & SUBJECT_FLAG_ELITE`(16) — 몬스터는 ADD 뒤 INFO가 오면 그 `maxhp`로 HP 바를 다시 그린다(종족 최대 HP가 아니다).
  재등장(`SUBJECT_RESPAWN_NFY`) 뒤 INFO가 오면 정예 여부 · 최대 HP를 그것으로 바꾼다(flags 0이면 정예 표시를 끈다). 이름은 그대로 — 정예 표지는 클라가 그린다.
- **`AuctionResult::Bound`(14)**: 「귀속된 물건은 경매에 올릴 수 없습니다」 — `SYSTEM_MESSAGE ItemBound`(26)도 함께 온다(param1 아이템 · param2 칸).
- **버리기 거부**: `ITEM_DISCARD_ACK` 실패 + `ItemBound`. 귀속 표시는 행 `bind eAcquire` 또는 인스턴스 bit 8(`ITEM_DETAIL_INF.extra & 256`), 거래 이력은 bit 9(512 — 분해 미리보기 절반).
- **전리품 알림**: 정수 · 결정 · 정예 · 보스 보상도 `LootAcquired`(가방) · `LootOverflow`(발밑)로 온다 — 막타가 아니어도 받을 수 있다.
- 프로토콜 판은 3 그대로(1부와 같은 판 — 끝에 더한 값뿐이다).
