#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// 기획 데이터 로더 — 서버 · 클라 공유 모듈(Shared/Document/DATA_PIPELINE.md).
//
//   GameData/*.json(시트 HARVEN_DATA → datatool export, mapgen → Town*.json)
//     → GameData::LoadFromDir(스레드를 시작하기 전에 한 번)
//     → 불변 스냅샷(GameDataSnapshot) → 기존 파사드(MonsterTable::Find …)가 읽는다.
//
//  · 실패하면 서버는 뜨지 않는다 — 오류를 모두 모아 원본 위치("Monster!14 name: 21바이트 > 19")와 함께 돌려준다.
//  · 스냅샷은 부팅 뒤 바뀌지 않는다. 핫 리로드(--dev, Phase 4)는 새 세대를 만들어 바꿔 끼우고 옛 세대는 해제하지 않는다
//    (EquippedPiece.row 같은 포인터가 옛 세대를 들고 있을 수 있다).
//  · 이 헤더는 가볍다(JSON 파서를 include하지 않는다) — 파사드 .cpp · 서버 main이 include한다.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct GameDataSnapshot;   // Data/Load/Snapshot.h

namespace GameData
{
    struct Issue
    {
        std::string where;   // "Monster!14 name" · "Monster.json#3 name"
        std::string what;    // "21바이트 > 19"
        std::string rule;    // 옮겨 온 규칙 번호(RULES.md R031 …) — 형 · 열 검사는 비어 있다
    };

    struct LoadResult
    {
        bool ok = false;
        std::vector<Issue> errors;
        std::string version;        // manifest.json의 판 해시(없으면 "unversioned")
        double ms = 0.0;            // 읽기 + 묶기 + 검증 시간
        double readMs = 0.0;        //   그중 파일 읽기 + manifest 확인(파일마다 한 번 읽는다)
        double bindMs = 0.0;        //   JSON 파싱 + 묶기(Bind)
        double validateMs = 0.0;    //   검증(Validate — 옮겨 온 규칙)
        size_t tables = 0;          // 읽은 표 수
        size_t rows = 0;            // 읽은 행 수
        size_t stringBytes = 0;     // 문자열 영역 크기
    };

    // dir의 GameData를 읽어 검증하고, 통과하면 현재 스냅샷으로 올린다(이전 스냅샷은 해제하지 않는다).
    [[nodiscard]] LoadResult LoadFromDir(const std::filesystem::path& dir);

    // 읽고 검증만 한다(올리지 않는다) — datatool check · --check-data.
    [[nodiscard]] LoadResult CheckDir(const std::filesystem::path& dir);

    // 현재 스냅샷을 정본 JSON으로 쓴다(--dump-data) — Canon 코드(datatool export와 같은 바이트 — datatool verify가 확인).
    [[nodiscard]] bool DumpToDir(const std::filesystem::path& dir, std::string& error);

    // --data-dir가 비었으면: 실행 파일 옆 GameData/ → 위로 올라가며 GameData/manifest.json 또는 Bin/GameData/manifest.json이 있는 곳
    // (빌드 출력 Bin/<구성>/에서는 바로 위 Bin/GameData, 저장소 뿌리 · 도구 폴더에서는 Bin/GameData).
    [[nodiscard]] std::filesystem::path ResolveDir(const std::string& overrideDir, const char* argv0);

    [[nodiscard]] bool Loaded() noexcept;
    [[nodiscard]] const std::string& Version() noexcept;

    // 결과를 사람이 읽을 줄로(부팅 로그 · check 출력)
    [[nodiscard]] std::string Describe(const LoadResult& r, size_t maxErrors = 200);

    // ── 개발용 핫 리로드(--dev — 운영 기본 끔, Phase 4) ─────────────────────────────
    //  · dir을 다시 읽어 검증 → 모든 표의 구조 지문(행 수 · 키)이 지금 판과 같으면(값만 바뀐 판) 새 세대로 올린다.
    //    id를 더하거나 빼거나 구조 상수를 넘으면 거부하고 재시작을 안내한다. 옛 세대는 해제하지 않는다(포인터 안전).
    //  · 로직 스레드(GameLogicThread — Zone · 기본)는 묶음(틱)을 시작할 때 PinThread()로 세대를 고정한다 →
    //    한 틱 안에서는 한 판만 보이고, 새 세대는 다음 틱부터(Zone 틱 경계에서 교체).
    struct ReloadResult
    {
        LoadResult load;
        bool published = false;
        std::string from;           // 바꾸기 전 판
        std::string reason;         // 올리지 않은 까닭(검증 실패 · 같은 판 · 구조 변경)
        size_t generation = 0;      // 올린 뒤 세대 수
        size_t filesOpened = 0;     // 이번에 실제로 연 파일 수(나머지는 크기 · 시각이 같아 기억한 원문)
    };
    [[nodiscard]] ReloadResult Reload(const std::filesystem::path& dir);
    [[nodiscard]] std::string DescribeReload(const ReloadResult& r);
    [[nodiscard]] size_t Generations() noexcept;
    void PinThread() noexcept;

    // --dev: dir/manifest.json이 바뀌면(datatool sync · watch) 다시 읽는 감시 스레드. RequestReload()는 다음 확인 때 바로 다시 읽게 한다.
    // done(결과)은 감시 스레드에서 불린다. 한 번만 시작한다.
    void StartDevWatch(const std::filesystem::path& dir);
    void RequestReload(std::function<void(const ReloadResult&)> done = {});
    [[nodiscard]] bool DevMode() noexcept;
}

// 현재 스냅샷(부팅 뒤 불변). 로드 전에 부르면 오류를 찍고 멈춘다.
[[nodiscard]] const GameDataSnapshot& GD() noexcept;
