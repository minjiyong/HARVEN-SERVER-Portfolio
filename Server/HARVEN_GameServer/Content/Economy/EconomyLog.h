#pragma once

#include <cstdint>
#include <string>

#include "Database/GameData/Item/ItemTableRow.h"   // ItemGrade · ItemTableId

enum class AdenaReason : uint8_t;   // Protocol.h

// ─────────────────────────────────────────────────────────────────────────────
// 경제 로그(경제 1차 명세 §8) — 그로시 · 장비 · 재료가 어디서 생기고 어디로 사라지는지 하루 단위로 센다.
//
//  · 카운터는 모두 원자 변수(아무 스레드에서나 On*를 부른다 — Zone · DB 완료 콜백 · 서비스). 락 없음.
//  · Tick(지금)은 svc-tick 스레드가 1초마다 부른다. 하루 경계(06:00 KST — GuildGrowth::PeriodId(지금, 86400, 75600))를
//    넘으면 지난 하루의 카운터를 한 줄 JSON으로 $HARVEN_ECONOMY_LOG(기본: 작업 폴더의 economy.log)에 덧붙이고
//    stdout에 [Economy] 요약을 찍은 뒤 0으로 되돌린다. 서버를 켠 뒤 첫 경계 전까지는 「켠 때부터」를 센다(끌 때는 쓰지 않는다).
//    HARVEN_ECONOMY_PERIOD_SEC=N이면 하루 대신 N초(시험용 — regress_craft.py).
//  · SnapshotJson()은 지금 값(되돌리지 않음) — 개발 채팅 /economy(--dev)가 보낸 사람에게 답한다.
//  · 그로시는 CInventory의 그로시 함수(AddAdena · TrySpendAdena · Reserve/Return · 정산 받기 · 상점)가 사유와 함께 한 곳에서 부른다.
//    제작 수수료는 DB 커밋 확인 뒤(CCraftHelper)에 센다(사람이 나갔어도 커밋은 됐다).
// 형식 · 명령: Shared/Document/ECONOMY.md 「경제 로그」.
// ─────────────────────────────────────────────────────────────────────────────

// 장비가 생기는 길(GearIn) — 필드 · 정예 · 보스 굴림은 처치 보상(CRewardHelper)이, 제작은 CCraftHelper가, 퀘스트 · 상점은 그 처리가 부른다
enum class GearSource : uint8_t { Field, Elite, Boss, Craft, Quest, Shop, Achievement, Count };   // Achievement: 업적 보상(받기)
// 장비가 사라지는 길(GearOut)
enum class GearSink : uint8_t { Dismantle, ShopSell, EnchantBreak, Count };
// 재료가 생기는 길 · 쓰이는 길
enum class MaterialSource : uint8_t { Kill, Elite, Boss, Dismantle, Achievement, Count };   // Achievement: 업적 보상(받기)
enum class MaterialSink : uint8_t { Craft, ShopSell, Count };

class CEconomyLog
{
public:
    // 그로시: delta > 0 들어옴 · < 0 나감(사유별로 따로 센다)
    static void OnAdena(AdenaReason reason, int64_t delta) noexcept;
    static void OnGearIn(GearSource source, ItemGrade grade, uint32_t count = 1) noexcept;
    static void OnGearOut(GearSink sink, ItemGrade grade, uint32_t count = 1) noexcept;
    static void OnMaterialIn(MaterialSource source, ItemTableId item, uint32_t count) noexcept;
    static void OnMaterialOut(MaterialSink sink, ItemTableId item, uint32_t count) noexcept;
    static void OnCraftFee(uint32_t fee) noexcept;

    // 지금 값(한 줄 JSON — 0인 칸은 뺀다). 되돌리지 않는다.
    [[nodiscard]] static std::string SnapshotJson();
    // svc-tick(1초) — 하루 경계를 넘었으면 지난 하루를 파일 · stdout에 쓰고 0으로
    static void Tick(int64_t unixNow);
};
