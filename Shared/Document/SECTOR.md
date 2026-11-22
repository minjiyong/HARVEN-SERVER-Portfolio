# SECTOR — 섹터 격자 훑기(시야 · 범위)

> 코드: `Server/HARVEN_GameServer/Content/Field/Sector.*`(격자 · 훑기) · `SectorHelper.cpp`(시야 목록 · 걸음 · Zone 넘기) ·
> 시험 `Tools/Test/regress_view.py`(시야 회귀 · `--base` A/B).

## 1. 격자

- Zone(스레드)마다 격자가 따로다(`thread_local GSector`, 50×50칸). 칸 하나 = 10×10타일(`SECTOR_RANGE`). 다른 Zone 칸은 이 배열에 없다 —
  Zone 경계 = 시야 경계.
- 칸 항목 `Entry { id, 약한 참조, x, y }`. **x · y = 객체의 지금 좌표**다. 격자에 든 객체의 좌표는 `UpdateObjectSectorAndPosition`만 바꾸고
  (칸이 바뀌면 새 항목, 같은 칸이면 항목 좌표만), 격자 밖 객체(Zone을 넘는 중 · 죽어 빠진 플레이어)만 `SetPosition`을 쓴다.
  Debug 빌드는 훑을 때마다 항목 좌표 = 객체 좌표 · 항목이 그 칸 안인지 본다(`AppendWithin`).
- 칸마다 플레이어 항목 수(`m_CellUsers`)를 같이 센다 — 넣고 뺄 때 함께 맞춘다(Debug는 훑을 때마다 다시 세어 대조).

## 2. 훑기 고르기

| 함수 | 훑는 칸 | 돌려주는 것 | 쓰는 곳 |
|---|---|---|---|
| `AppendNeighbors` · `ForEachNeighborObject` | 3×3 | 칸에 든 모두(category) | 3×3 전체를 보는 곳(PvP 모양 판정 · 줍기) · 드물게 불리는 알림(사망 · 접속 종료 · 혈맹 …) |
| `AppendInRange` · `ForEachObjectInRange` | 3×3 ∩ 반경이 닿는 칸 | 그 칸에 든 모두 | 칸 단위 뜻이 필요한 곳(몬스터 돌진 막힘 · 모양 타격 — 콜백이 직접 거른다) |
| `AppendWithin` · `ForEachObjectWithin` | 3×3 ∩ 반경이 닿는 칸 | **체비셰프 반경 안만**(항목 좌표로 거른다) | 시야(`CollectVisible` · `CollectUsers`) · 알림(피해 · 효과 · 상태 · 기술) · 몬스터 AI(대상 · 동료 · 장판) |

- `AppendWithin`은 반경 밖 항목의 약한 참조를 풀지 않는다(원자 연산 · 공유 포인터 두 번이 빠진다). 결과(객체와 차례)는 `AppendInRange` + 콜백의
  "체비셰프 반경 안" 검사와 같다 — 콜백이 그 반경(또는 더 좁게)으로 다시 거르던 곳에만 쓴다. 시야 = `CSubjectHelper::CanSee`(체비셰프 `GViewRange`).
- 플레이어만 찾는 훑기(eUser)는 플레이어 항목이 없는 칸을 건너뛴다(`MayHold`) — 몬스터 항목을 하나씩 넘기지 않는다. 결과는 그대로다.
- 훑은 결과를 담는 목록은 `CSubjectScratch`(스레드마다 깊이별 목록을 다시 쓴다 — 콜백 안에서 또 훑어도 서로 덮지 않는다).

## 3. 플레이어 걸음(`HandlePlayerMove`)

- 걸음 전 · 후 시야 목록을 `CollectVisible`로 두 번 모은 뒤, 새 목록 차례대로 ADD · MOVE(+ 몬스터 깨우기), 옛 목록 차례대로 REMOVE를 보낸다.
- "다른 목록에 있었나"는 `CVisibilityMarks`(옛 · 새 ObjID를 한 번씩 적는 표, 세대 번호로 비운다)가 바로 답한다 — 예전 선형 탐색은
  보이는 수의 제곱이라 붐비는 마을(한 사람이 130~150명을 본다)에서 서버 명령의 40%를 먹었다. 알림 종류 · 차례는 같다(Debug는 선형 탐색과 대조).

## 4. 시험

- `Tools/Test/regress_view.py`: 훈련장 · Zone 0 | 1 경계에서 플레이어 여섯이 칸 경계 · 시야 끝(9 ↔ 10칸) · Zone 경계를 오가며, 걸음마다
  각자 아는 이웃(ADD · REMOVE)과 좌표(ADD · MOVE)를 서버 규칙(체비셰프 시야 · 같은 Zone)으로 다시 계산한 것과 맞대어 본다.
  `--base <옛 서버>`면 걸음마다 받은 알림(종류 · 차례 · 좌표)을 옛 서버와 맞대어 본다.
- 격자를 고친 뒤에는 Debug(ASan) 빌드로 회귀 · StressTest를 돌린다 — 항목 좌표 · 플레이어 수 대조 assert가 켜진다.
