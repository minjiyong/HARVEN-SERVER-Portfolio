#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// CCrashHandler — 서버가 죽을 때(SIGSEGV · SIGBUS · SIGFPE · SIGILL · SIGABRT) crash.log에 신호 · 주소 · 호출 스택 · 메모리를 남긴다.
// ASSERT_CRASH(널 쓰기)도 여기로 온다. 남긴 뒤 원래 처리(코어 덤프 · 종료 코드)로 넘긴다.
// 호출 스택의 함수 이름은 -rdynamic(CMakeLists.txt)으로, 줄 번호는 addr2line -e GameServer <주소>로 찾는다.
// 스택이 넘친 경우에도 남기도록 스레드마다 신호용 스택을 따로 둔다(InitThread — CThreadManager가 스레드마다 부른다).
// ASan · TSan 빌드에서는 걸지 않는다(검사기 자신의 보고가 먼저다).
// ─────────────────────────────────────────────────────────────────────────────
class CCrashHandler
{
public:
    static void Install();     // main에서 한 번(신호 처리 · 메모리 부족 처리)
    static void InitThread();  // 스레드마다(신호용 스택)
};
