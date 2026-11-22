# 동시성 시험 — 서버 코드 없이 Libs 헤더만(CLockFreeStack · CObjectPool · CSafeMessageQueue). --bench면 성능 비교
# 서버와 따로 빌드한다(EXCLUDE_FROM_ALL): cmake --build <폴더> --target ConcurrencyTests
add_executable(ConcurrencyTests EXCLUDE_FROM_ALL concurrency_tests.cpp)
add_executable(SocialStoreTests EXCLUDE_FROM_ALL social_store_tests.cpp "${HARVEN_SERVER_DIR}/Content/Social/SocialStore.cpp")
target_include_directories(SocialStoreTests PRIVATE "${HARVEN_SERVER_DIR}" "${HARVEN_LIBS_DIR}" "${HARVEN_SHARED_DIR}" ${Boost_INCLUDE_DIRS})
target_link_libraries(SocialStoreTests PRIVATE Threads::Threads)
target_include_directories(ConcurrencyTests PRIVATE "${HARVEN_LIBS_DIR}")
target_link_libraries(ConcurrencyTests PRIVATE Threads::Threads atomic)

# 행동 트리 시험 — Libs/Base/BehaviorTree만(묶음 · 장식 · 끊기 · 깨우기 요청 · 정의 공유)
#   cmake --build <폴더> --target BehaviorTreeTests
add_executable(BehaviorTreeTests EXCLUDE_FROM_ALL behavior_tree_tests.cpp)
target_include_directories(BehaviorTreeTests PRIVATE "${HARVEN_LIBS_DIR}")

# 파티 규칙 시험 — PartyRule::SplitExp(경험치 나누기 · 쩔 보장)의 성질을 무작위로(합 = total · 보장 · 비례 · 순서)
#   cmake --build <폴더> --target PartyRuleTests
add_executable(PartyRuleTests EXCLUDE_FROM_ALL party_rule_tests.cpp)
target_include_directories(PartyRuleTests PRIVATE "${HARVEN_SERVER_DIR}" "${HARVEN_LIBS_DIR}" "${HARVEN_SHARED_DIR}")
target_compile_options(PartyRuleTests PRIVATE -finput-charset=UTF-8)

# 결사 성장 규칙 시험 — 진행 저장 왕복(무작위 · 깨진 덩어리) · 임무 고르기 · 처치 규칙(임무 · 덧보상 · 출석 · 한도 · 협력 · 업적 · 칭호 · 주 넘김) · 권한 · 글
#   cmake --build <폴더> --target GuildRuleTests
add_executable(GuildRuleTests EXCLUDE_FROM_ALL guild_rule_tests.cpp "${HARVEN_SERVER_DIR}/Content/Guild/GuildProgress.cpp")
target_include_directories(GuildRuleTests PRIVATE "${HARVEN_SERVER_DIR}" "${HARVEN_LIBS_DIR}" "${HARVEN_SHARED_DIR}")
target_compile_options(GuildRuleTests PRIVATE -finput-charset=UTF-8)

# 처치 보상 굴림 시험 — Content/Reward/LootRoll.h(필드 장비 등급 경계 · 희귀 천장 · 직업 무기 바꾸기 · 피해 가중 추첨 · 레벨 차 절반 ·
# 정예 · 보스 자격 · 정예 장비 칸 · 보스 굴림(직업 거르기 · 영웅 천장) · 정예 확률)
#   cmake --build <폴더> --target LootRuleTests
add_executable(LootRuleTests EXCLUDE_FROM_ALL loot_rule_tests.cpp)
target_include_directories(LootRuleTests PRIVATE "${HARVEN_SERVER_DIR}" "${HARVEN_LIBS_DIR}" "${HARVEN_SHARED_DIR}")
target_compile_options(LootRuleTests PRIVATE -finput-charset=UTF-8 -Wall -Wextra)

# 개인 업적 규칙 시험 — AchieveRules.h(색인) · Content/Achieve/AchieveBook(통계 → 달성 · 받음 · 저장 절) · User/ProgressCodec(진행 기록 판 4:
# 판 3 기록 → 업적 빈 채 · 판 4 왕복 · 깨진 덩어리 · 가장 큰 기록 ≤ VARBINARY(4000)의 절반)
#   cmake --build <폴더> --target AchieveRuleTests
add_executable(AchieveRuleTests EXCLUDE_FROM_ALL achieve_rule_tests.cpp
    "${HARVEN_SERVER_DIR}/Content/Achieve/AchieveBook.cpp" "${HARVEN_SERVER_DIR}/User/ProgressCodec.cpp")
target_include_directories(AchieveRuleTests PRIVATE "${HARVEN_SERVER_DIR}" "${HARVEN_LIBS_DIR}" "${HARVEN_SHARED_DIR}")
target_compile_options(AchieveRuleTests PRIVATE -finput-charset=UTF-8 -Wall -Wextra)

# PostToOwner 한 걸음 판정 시험 — Content/ServicePost.h(나감 · 가짐 · 옮기는 중 · 남의 Zone · 넘긴 횟수 다 씀 → nullptr)
#   cmake --build <폴더> --target ServicePostTests
add_executable(ServicePostTests EXCLUDE_FROM_ALL service_post_tests.cpp)
target_include_directories(ServicePostTests PRIVATE "${HARVEN_SERVER_DIR}")
target_compile_options(ServicePostTests PRIVATE -finput-charset=UTF-8 -Wall -Wextra)
