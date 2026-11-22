// ─────────────────────────────────────────────────────────────────────────────
// STRESS_TEST — 봇 부하 도구(리눅스, 화면 없이). 1초마다 수치를 찍는다.
//
//   StressTest <초> [출력 csv]
//     csv: 초, 접속 수, 활성 봇, 지연 EMA(ms), 지연 평균, 단계(0 증가 · 1 안정 · 2 감소), 보이는 몬스터 · 플레이어 평균
//   서버 포트: 기본 PORT_NUM(4000). 환경 변수 HARVEN_STRESS_PORT로 바꾼다(예: HARVEN_STRESS_PORT=4300 ./StressTest 60).
//   서버는 --load-test로 띄운다(새 캐릭터를 마을 대신 16개 Zone에 흩어 세운다).
//   최대 봇 수는 빌드 때 정한다(CMake HARVEN_STRESS_MAX_BOTS, 기본 20000) — 작게 주면 "그 수까지 올리고 머문다"(고정 부하).
//
// 늘리기 규칙: 지연 100ms에서 멈추고 150ms에서 줄인다(NetworkModule.cpp Adjust_Number_Of_Client).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "NetworkModule.h"

int main(int argc, char** argv)
{
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 60;
    FILE* csv = argc > 2 ? std::fopen(argv[2], "w") : nullptr;
    if (csv) std::fprintf(csv, "sec,connections,active,delay_ema,delay_avg,phase,vis_monsters,vis_players\n");
    InitializeNetwork();
    int peakStable = 0;
    long long delaySum = 0;
    int delayN = 0;
    for (int s = 1; s <= seconds; ++s)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const int conn = num_connections.load(), act = active_clients.load();
        const int ema = global_delay.load(), avg = delay_avg_val.load();
        const int ph = static_cast<int>(current_phase.load());
        if (ema <= 100 && act > peakStable) peakStable = act;
        if (s > seconds / 2) { delaySum += ema; ++delayN; }
        std::printf("[stress] %4ds conn=%d active=%d delay=%dms avg=%dms phase=%d monsters=%.1f players=%.1f\n",
            s, conn, act, ema, avg, ph, g_avg_visible_monsters.load(), g_avg_visible_players.load());
        std::fflush(stdout);
        if (csv) { std::fprintf(csv, "%d,%d,%d,%d,%d,%d,%.2f,%.2f\n", s, conn, act, ema, avg, ph, g_avg_visible_monsters.load(), g_avg_visible_players.load()); std::fflush(csv); }
    }
    std::printf("[stress] server rejections move_too_fast=%d skill_rejected=%d\n", g_move_too_fast.load(), g_skill_rejected.load());
    std::printf("[stress] summary peak_active_at_delay<=100ms=%d mean_delay_2nd_half=%.1fms\n", peakStable, delayN ? static_cast<double>(delaySum) / delayN : 0.0);
    if (csv) std::fclose(csv);
    std::fflush(stdout);
    std::_Exit(0);
}
