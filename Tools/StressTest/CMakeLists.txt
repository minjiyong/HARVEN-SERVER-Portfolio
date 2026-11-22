# STRESS_TEST — 봇 부하 도구(리눅스, 화면 없이). 서버와 같은 Shared/Protocol/Protocol.h를 쓴다.
#   cmake --build <폴더> --target StressTest
#   HARVEN_STRESS_MAX_BOTS: 최대 봇 수(정적 배열 크기). 작게 주면 그 수까지 올리고 머문다(고정 부하 전/후 비교).
set(HARVEN_STRESS_MAX_BOTS 20000 CACHE STRING "StressTest 최대 봇 수")

add_executable(StressTest EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/NetworkModule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_include_directories(StressTest PRIVATE
    "${HARVEN_SHARED_DIR}"
    "${HARVEN_SERVER_DIR}"
    "${HARVEN_LIBS_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(StressTest PRIVATE HARVEN_STRESS_MAX_BOTS=${HARVEN_STRESS_MAX_BOTS})
target_link_libraries(StressTest PRIVATE Threads::Threads)
