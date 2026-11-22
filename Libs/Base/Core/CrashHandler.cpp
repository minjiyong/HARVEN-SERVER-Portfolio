#include "pch.h"
#include "Base/Core/CrashHandler.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <fcntl.h>
#include <new>
#include <unistd.h>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define HARVEN_CRASH_HANDLER_DISABLED 1
#endif

namespace
{
    constexpr int kSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    constexpr size_t kAltStackSize = 64 * 1024;
    constexpr int kMaxFrames = 64;

    std::atomic<pid_t> GReportingTid{ 0 };    // 기록을 맡은 스레드(0 = 아직 없음)
    std::atomic<bool>  GReportDone{ false };

    // 신호 처리기 안에서 쓸 수 있는 것(write)만으로 숫자 · 글을 남긴다
    void WriteText(int fd, const char* text)
    {
        (void)!::write(fd, text, std::strlen(text));
    }

    void WriteHex(int fd, std::uintptr_t value)
    {
        char buf[2 + 16 + 1];
        buf[0] = '0';
        buf[1] = 'x';
        for (int i = 0; i < 16; ++i)
        {
            const unsigned nibble = static_cast<unsigned>((value >> ((15 - i) * 4)) & 0xF);
            buf[2 + i] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
        }
        buf[18] = '\0';
        WriteText(fd, buf);
    }

    void WriteDec(int fd, long value)
    {
        char buf[24];
        int i = 23;
        buf[i] = '\0';
        const bool negative = value < 0;
        unsigned long v = negative ? static_cast<unsigned long>(-value) : static_cast<unsigned long>(value);
        do { buf[--i] = static_cast<char>('0' + v % 10); v /= 10; } while (v != 0 && i > 1);
        if (negative) buf[--i] = '-';
        WriteText(fd, buf + i);
    }

    const char* SignalName(int sig)
    {
        switch (sig)
        {
        case SIGSEGV: return "SIGSEGV(잘못된 메모리 접근 — ASSERT_CRASH 포함)";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE(0으로 나누기 등)";
        case SIGILL:  return "SIGILL";
        case SIGABRT: return "SIGABRT(abort · std::terminate)";
        default:      return "signal";
        }
    }

    void WriteReport(int fd, int sig, const siginfo_t* info)
    {
        WriteText(fd, "\n[CRASH] ");
        WriteText(fd, SignalName(sig));
        WriteText(fd, " pid=");
        WriteDec(fd, static_cast<long>(::getpid()));
        WriteText(fd, " tid=");
        WriteDec(fd, static_cast<long>(::gettid()));
        if (info != nullptr && info->si_code <= 0)
        {
            // kill · raise · abort가 보낸 신호 — 주소 대신 보낸 프로세스
            WriteText(fd, " sent-by-pid=");
            WriteDec(fd, static_cast<long>(info->si_pid));
        }
        else if (info != nullptr && (sig == SIGSEGV || sig == SIGBUS))
        {
            WriteText(fd, " fault-addr=");
            WriteHex(fd, reinterpret_cast<std::uintptr_t>(info->si_addr));
            if (info->si_addr == nullptr)
                WriteText(fd, " (널 — ASSERT_CRASH이거나 널 포인터)");
        }
        WriteText(fd, "\n  thread: ");
        char name[32]{};
        if (::pthread_getname_np(::pthread_self(), name, sizeof(name)) == 0)
            WriteText(fd, name);
        WriteText(fd, "\n  call stack (addr2line -e GameServer <주소>):\n");
        void* frames[kMaxFrames];
        const int count = ::backtrace(frames, kMaxFrames);
        ::backtrace_symbols_fd(frames, count, fd);

        // 메모리(VmRSS · VmSize) — /proc/self/status에서 그대로 옮긴다
        const int status = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
        if (status >= 0)
        {
            char buf[4096];
            const ssize_t n = ::read(status, buf, sizeof(buf) - 1);
            ::close(status);
            if (n > 0)
            {
                buf[n] = '\0';
                for (const char* key : { "VmRSS:", "VmSize:", "Threads:" })
                    if (const char* line = std::strstr(buf, key))
                    {
                        const char* end = std::strchr(line, '\n');
                        WriteText(fd, "  ");
                        (void)!::write(fd, line, end != nullptr ? static_cast<size_t>(end - line + 1) : std::strlen(line));
                    }
            }
        }
    }

    void OnCrash(int sig, siginfo_t* info, void*)
    {
        // 기록은 처음 죽은 스레드 하나만 남긴다. 같은 상태로 다른 스레드가 잇달아 죽으면 그 기록이 끝날 때까지(최대 5초) 기다렸다가
        // 넘긴다 — 바로 넘기면 프로세스가 기록 도중에 끝난다. 기록하던 스레드가 처리기 안에서 또 죽으면 바로 넘긴다.
        const pid_t self = static_cast<pid_t>(::gettid());
        pid_t expected = 0;
        if (GReportingTid.compare_exchange_strong(expected, self))
        {
            const int fd = ::open("crash.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
            if (fd >= 0)
            {
                WriteReport(fd, sig, info);
                ::close(fd);
            }
            WriteReport(STDERR_FILENO, sig, info);
            GReportDone.store(true);
        }
        else if (expected != self)
        {
            const struct timespec step{ 0, 10 * 1000 * 1000 };   // 10ms
            for (int i = 0; i < 500 && !GReportDone.load(); ++i)
                ::nanosleep(&step, nullptr);
        }
        // 원래 처리로 다시 보낸다(코어 덤프 · 신호 종료 코드)
        ::signal(sig, SIG_DFL);
        ::raise(sig);
    }

    void OnOutOfMemory()
    {
        const int fd = ::open("crash.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0)
        {
            WriteText(fd, "\n[OOM] operator new 실패 — 메모리 부족\n");
            ::close(fd);
        }
        std::set_new_handler(nullptr);   // 다음 실패는 std::bad_alloc으로
    }
}

void CCrashHandler::Install()
{
#ifndef HARVEN_CRASH_HANDLER_DISABLED
    // backtrace는 처음 부를 때 libgcc를 읽는다 — 처리기 안이 아니라 여기서 미리 한 번
    void* warmup[1];
    (void)::backtrace(warmup, 1);

    InitThread();
    for (const int sig : kSignals)
    {
        struct sigaction action{};
        action.sa_sigaction = &OnCrash;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        ::sigemptyset(&action.sa_mask);
        ::sigaction(sig, &action, nullptr);
    }
    std::set_new_handler(&OnOutOfMemory);
#endif
}

void CCrashHandler::InitThread()
{
#ifndef HARVEN_CRASH_HANDLER_DISABLED
    thread_local bool LInstalled = false;
    if (LInstalled)
        return;
    LInstalled = true;
    // 스레드가 끝날 때까지 쓰는 신호용 스택(서버 스레드는 프로세스와 함께 끝난다 — 돌려주지 않는다)
    stack_t stack{};
    stack.ss_sp = ::operator new(kAltStackSize);
    stack.ss_size = kAltStackSize;
    stack.ss_flags = 0;
    ::sigaltstack(&stack, nullptr);
#endif
}
