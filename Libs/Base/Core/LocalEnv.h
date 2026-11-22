#pragma once

#include <filesystem>

// ─────────────────────────────────────────────────────────────────────────────
// CLocalEnv — 개발 PC의 로컬 설정 파일(harven.env)을 프로세스 환경 변수로 올린다.
// 형식은 한 줄에 KEY=VALUE(# 주석 · 빈 줄 · 앞의 export · 감싼 따옴표 · CRLF · BOM 허용) —
// systemd EnvironmentFile · docker --env-file과 같은 형식이라 운영에서도 그대로 쓸 수 있다.
// 줄 중간의 #는 값으로 본다(비밀번호에 #가 들어갈 수 있다).
//
// 찾는 곳: HARVEN_ENV_FILE(있으면 그 파일만) → 실행 파일 폴더 · 작업 폴더에서 위로 올라가며 처음 만나는 harven.env
//   (VS + WSL이면 ~/HARVEN-SERVER/build/<프리셋>/GameServer → ~/HARVEN-SERVER/harven.env)
// 이미 있는 환경 변수는 덮어쓰지 않는다(운영 · CI가 주는 값이 먼저). 빈 값은 채우지 않은 것으로 보고 건너뛴다.
// HARVEN_DB_CA가 상대 경로면 harven.env가 있는 폴더 기준 절대 경로로 바꾼다.
// 로그에는 파일 경로와 변수 이름만 찍는다(값은 찍지 않는다).
// 스레드를 띄우기 전 main 맨 앞에서 한 번 부른다(setenv는 스레드 안전하지 않다).
// ─────────────────────────────────────────────────────────────────────────────
class CLocalEnv
{
public:
    static constexpr const char* kFileName = "harven.env";

    // 읽은 파일 경로(없으면 빈 경로)를 돌려준다
    static std::filesystem::path Load(const char* argv0);
};
