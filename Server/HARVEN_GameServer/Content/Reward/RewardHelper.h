#pragma once

class CUser;
class CMonster;

// ─────────────────────────────────────────────────────────────────────────────
// 몬스터 처치 보상 — 몬스터를 가진 Zone 스레드에서(CUserHelper::AttackMonster). 설계: Shared/Document/PARTY.md 3.
//
//  · 나눠 받는 사람 = 몬스터의 피해 기록(CDamageLedger)에 있는 사람 + 막타 — 이 Zone 스레드가 가진 사람만
//    (그 사이 다른 Zone으로 옮긴 사람은 그로시 · 경험치에서 빠지고 퀘스트만 그 Zone에서 인정받는다).
//  · 전리품(경제 1차 — Shared/Document/ECONOMY.md 「처치 보상」, 굴림 · 지급은 CLootHelper):
//    - 소모품 드롭 표(Monster.drop_table × drop_rolls)는 막타(이 Zone에 없으면 가장 많이 때린 사람) — 일반 · 정예 · 보스 모두.
//    - 일반 몬스터: 필드 장비(TierLoot — 죽은 자리 Region.tier, 희귀 천장)는 막타, 정수 · 결정은 나눠 받는 사람 중 피해 비례 추첨 한 명.
//    - 정예: 그로시 · 경험치 배율 + 자격자(피해 ≥ elite_share_percent% · 공유 범위)마다 결정 · 정수 · 정예 장비 굴림(영웅 천장).
//    - 보스: 자격자(피해 ≥ boss_share_percent% · 또는 그 파티원 중 공유 범위 안)마다 BossLoot 개인 굴림 한 번.
//    받는 사람마다 가방에 바로(SystemMessageCode::LootAcquired), 가방이 차면 죽은 자리 주변에 그 사람의 줍기 우선권으로(LootOverflow —
//    그 사람 > 그 사람의 파티 > 누구나, 시트 PartyConst loot_owner_ms · loot_party_ms). 계정 = playerId(한 사람 한 번).
//  · 그로시 · 경험치: 피해를 준 사람 모두(파티가 아니어도, 모르는 사람끼리도)가 준 피해에 비례(RewardRule::SplitByWeight).
//    살아 있고 죽은 자리에서 시트 exp_share_range 안에 있는 사람만 나눠 받는다(빠진 사람의 몫은 남은 사람에게 — 아무도 없으면 막타).
//    그로시는 한 번 굴린 양을 나눠 지갑에 바로 넣고(자동 획득), 저마다 자기 "그로시 획득" 옵션을 자기 몫에 더한다.
//    경험치는 같은 파티의 몫을 모아 파티 안에서 다시 나눈다(쩔 보장 — PartyRule::SplitKillExp). 받는 사람이 둘 이상이면 저마다 PARTY_NFY ExpShare.
//  · 퀘스트(처치 · 지역 처치 · 수집): 피해를 준 사람 모두 — 거리 · 생사와 상관없이. 보스 목표는 예전대로 가까이 있던 모두(OnBossKilled).
//  · 희귀도 알림: 시트 Rarity.loot_notice인 희귀도의 몬스터 전리품이 가방에 들어가면 시스템 채팅(ChatChannel::System)으로 서버 전체에
//    "누가 무엇을". 신화는 예전처럼 월드 공지(WORLD_NOTICE_INF RareLoot)도 함께.
// ─────────────────────────────────────────────────────────────────────────────
namespace CRewardHelper
{
    // 몬스터가 막 쓰러졌다(섹터에서 뺀 뒤). 피해 기록을 비운다. (deathX, deathY) = 쓰러진 칸.
    void HandleKill(const UserSharedPtr& killer, const MonsterSharedPtr& monster, short deathX, short deathY);

    // 몬스터 전리품이 user의 가방에 들어왔다(자동 획득 · 넘쳐 떨어진 것을 주움) — 희귀도 알림 · 신화 월드 공지.
    // 플레이어가 버린 물건을 줍는 것은 알리지 않는다(버렸다 주워 알림을 되풀이하지 못하게 — CItemHelper::TryPickupAt이 가린다).
    void AnnounceLoot(const UserSharedPtr& user, ItemTableId itemId, UInt64 extra);
}
