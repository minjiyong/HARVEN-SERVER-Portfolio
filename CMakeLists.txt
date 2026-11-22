cmake_minimum_required(VERSION 3.20)
project(HarvenServer LANGUAGES CXX)

# 리눅스 전용 — 서버는 Boost.Asio · unixODBC. 빌드 설정은 CMakePresets.json(linux-release · linux-debug · linux-debug-memory · linux-memory · linux-asan · linux-tsan).
# 폴더 배치(README "폴더 구조"): Server/HARVEN_GameServer(게임 서버) · Libs(엔진) · Shared(프로토콜) · Tools(도구).
# include는 이 세 뿌리 기준 경로("Content/Field/Sector.h" · "Network/GameSession.h" · "Protocol/Protocol.h").

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "HARVEN 서버는 리눅스 전용이다. 지금 시스템: ${CMAKE_SYSTEM_NAME}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Boost CONFIG REQUIRED)
find_package(ODBC REQUIRED)
find_package(Threads REQUIRED)
option(HARVEN_IN_MEMORY_DB "Use the in-memory database (Tools/Sandbox/MemDBConnection.cpp) for local tests" OFF)
set(HARVEN_SANITIZER "" CACHE STRING "검사기 빌드: address(ASan+UBSan) · thread(TSan) · 빈칸(없음)")
set_property(CACHE HARVEN_SANITIZER PROPERTY STRINGS "" address thread)

set(HARVEN_SERVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Server/HARVEN_GameServer")
set(HARVEN_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libs")
set(HARVEN_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Shared")

# Debug는 _DEBUG를 켠다(예전 MSVC Debug와 같다 — #ifdef _DEBUG 검사 · ASSERT가 살아난다)
add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)

if(HARVEN_SANITIZER STREQUAL "address")
    # 정렬 검사는 뺀다 — 패킷은 일부러 1바이트로 채운 구조체(#pragma pack(1), 안의 ObjID가 홀수 위치)이고
    # x86-64 · AArch64(Graviton)는 보통 읽기 · 쓰기의 비정렬 접근을 그대로 처리한다(원자 연산만 정렬이 필요 — 패킷에는 없다)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(HARVEN_SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
elseif(NOT HARVEN_SANITIZER STREQUAL "")
    message(FATAL_ERROR "HARVEN_SANITIZER는 address · thread · 빈칸 중 하나: ${HARVEN_SANITIZER}")
endif()

# x86-64: 16바이트 CAS(cmpxchg16b)를 바로 쓴다 — Base/Core/LockFreeStack.h(SList와 같은 꼬리표 붙은 머리)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_options(-mcx16)
endif()

file(GLOB_RECURSE GAME_SERVER_SOURCES CONFIGURE_DEPENDS
    "${HARVEN_SERVER_DIR}/*.cpp"
    "${HARVEN_LIBS_DIR}/*.cpp")
# 시험 코드(tests/ — main이 따로 있다)와 IDE 중간 폴더는 서버 실행 파일에 넣지 않는다
list(FILTER GAME_SERVER_SOURCES EXCLUDE REGEX "/HARVEN_GameServer/tests/")
list(FILTER GAME_SERVER_SOURCES EXCLUDE REGEX "/(x64|Debug|Release|build)/")

if(HARVEN_IN_MEMORY_DB)
    list(FILTER GAME_SERVER_SOURCES EXCLUDE REGEX "/Libs/Database/DBConnection\\.cpp$")
    list(APPEND GAME_SERVER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Sandbox/MemDBConnection.cpp")
endif()

add_executable(GameServer ${GAME_SERVER_SOURCES})
target_include_directories(GameServer PRIVATE
    "${HARVEN_SERVER_DIR}"
    "${HARVEN_LIBS_DIR}"
    "${HARVEN_SHARED_DIR}"
    ${Boost_INCLUDE_DIRS}
    ${ODBC_INCLUDE_DIRS})
target_compile_definitions(GameServer PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
# 모든 .cpp가 맨 앞에 include하는 pch.h를 미리 컴파일한다(Boost.Asio · 표준 헤더 — REIW와 같은 방식)
target_precompile_headers(GameServer PRIVATE "${HARVEN_SERVER_DIR}/pch.h")
# -rdynamic: 크래시 때 남기는 호출 스택(Base/Core/CrashHandler)에 함수 이름이 찍히게
target_link_options(GameServer PRIVATE -rdynamic)
# atomic: CLockFreeStack의 16바이트 원자 연산(x86-64 밖에서) · moodycamel
target_link_libraries(GameServer PRIVATE Threads::Threads ${ODBC_LIBRARIES} atomic)

# 부하 봇 · 동시성 시험은 따로 빌드한다: cmake --build <폴더> --target StressTest ConcurrencyTests
add_subdirectory(Tools/StressTest)
add_subdirectory(Server/HARVEN_GameServer/tests)
