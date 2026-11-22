#include "pch.h"
#include "Content/Reward/RewardHelper.h"

#include <string_view>

#include "Base/Core/CoreTLS.h"
#include "Base/GameObjectManager.h"
#include "Content/Chat/ChatHelper.h"
#include "Content/Guild/GuildService.h"
#include "Content/Field/ZoneManager.h"
#include "Content/Inventory/Inventory.h"
#include "Content/Field/Sector.h"
#include "Content/Field/ZoneLayout.h"
#include "Content/Quest/QuestHelper.h"
#include "Content/Reward/LootHelper.h"
#include "Content/Achieve/AchieveHelper.h"
#include "Content/Reward/LootRoll.h"
#include "Content/ServiceCommon.h"
#include "Database/GameData/EconRules.h"
#include "Database/GameData/Item/ItemTable.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/PartyRules.h"
#include "Database/GameData/RewardRules.h"
#include "Database/GameData/WorldData.h"
#include "Entity/Subject/Monster/Monster.h"
#include "User/User.h"
#include "User/UserHelper.h"

namespace CRewardHelper
{
    namespace
    {
        // 피해를 준 사람(이 Zone 스레드가 가진 사람)
        struct Contributor
        {
            UserSharedPtr user;
            UInt32        damage = 0;
            UInt32        partyId = 0;
            bool          sharing = false;   // 살아 있고 죽은 자리에서 exp_share_range 안 — 그로시 · 경험치를 나눠 받는다
        };
        using ContributorList = std::array<Contributor, RewardRule::kMaxShares>;

        void SendExpShare(const UserSharedPtr& user, UInt32 exp, UInt8 percent, bool guaranteed, const ObjID& monster)
        {
            auto s = user->GetGameSession();
            if (s == nullptr || s->m_eState != SOCKET_STATE::ST_INGAME)
                return;
            PARTY_NFY_PACKET n;
            Service::Init(n, PacketType::PARTY_NFY);
            n.event = static_cast<UInt8>(PartyEvent::ExpShare);
            n.arg32 = exp;
            n.arg8 = percent;
            n.arg16 = guaranteed ? 1 : 0;
            n.obj = ObjID::npos;
            n.target = monster;
            s->PostSend(n);
        }

        // 목적격 조사 — 마지막 글자가 한글이면 받침으로 "을" · "를", 아니면(숫자 · 괄호 · 영문) "을(를)"
        [[nodiscard]] const char* ObjectParticle(std::string_view s) noexcept
        {
            size_t i = s.size();
            while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80)
                --i;   // 마지막 글자의 첫 바이트
            if (i == 0 || s.size() - (i - 1) != 3)
                return "을(를)";
            const auto b0 = static_cast<unsigned char>(s[i - 1]);
            const auto b1 = static_cast<unsigned char>(s[i]);
            const auto b2 = static_cast<unsigned char>(s[i + 1]);
            if ((b0 & 0xF0) != 0xE0)
                return "을(를)";
            const UInt32 cp = (static_cast<UInt32>(b0 & 0x0F) << 12) | (static_cast<UInt32>(b1 & 0x3F) << 6) | (b2 & 0x3F);
            if (cp < 0xAC00 || cp > 0xD7A3)
                return "을(를)";
            return (cp - 0xAC00) % 28 != 0 ? "을" : "를";
        }

        // 살아 있고 죽은 자리에서 exp_share_range 안 — 그로시 · 경험치 · 정수를 나눠 받고, 정예 보상 · 보스 파티원 굴림의 거리 조건
        [[nodiscard]] bool IsSharing(CUser& user, short deathX, short deathY)
        {
            return !user.GetStat()->IsDead()
                && PartyRule::ChebyshevDistance(user.GetX(), user.GetY(), deathX, deathY) <= PartyRule::ExpShareRange();
        }

        // 1) 피해 기록 → 이 Zone 스레드가 가진 사람(list) · 다른 Zone에 있는 사람(elsewhere — 퀘스트만). 기록은 비운다.
        //    ledgerTotal = 기록 전체(플레이어)의 피해 합 — 정예 · 보스 자격(피해 몫 %)의 분모(다른 Zone으로 간 사람의 몫도 센다)
        size_t CollectContributors(const UserSharedPtr& killer, const MonsterSharedPtr& monster, short deathX, short deathY,
            ContributorList& list, std::array<ObjID, RewardRule::kMaxShares>& elsewhere, size_t& nElsewhere, UInt64& ledgerTotal)
        {
            size_t n = 0;
            nElsewhere = 0;
            ledgerTotal = 0;
            const ObjID killerId = killer->GetObjID();
            const bool killerHere = !killer->IsTransferring() && GZoneManager->IsCurrentThreadOwner(killerId);
            bool killerListed = false;
            auto push = [&](UserSharedPtr user, UInt32 damage)
            {
                Contributor c;
                c.partyId = user->m_PartyId.load(std::memory_order_relaxed);
                c.damage = damage;
                c.sharing = IsSharing(*user, deathX, deathY);
                c.user = std::move(user);
                list[n++] = std::move(c);
            };
            for (const auto& e : monster->m_DamageLedger.Entries())
            {
                if (e.id.GetCategory<EnumCategory>() != EnumCategory::eUser)
                    continue;
                ledgerTotal += e.damage;
                if (n >= list.size())
                    continue;
                if (e.id == killerId)
                {
                    killerListed = true;
                    if (killerHere)
                        push(killer, e.damage);
                    else
                        elsewhere[nElsewhere++] = e.id;
                    continue;
                }
                if (!GZoneManager->IsCurrentThreadOwner(e.id))
                {
                    elsewhere[nElsewhere++] = e.id;   // 그 사이 Zone을 옮겼다
                    continue;
                }
                auto user = ::GetGameObject<CUser>(e.id);
                if (user == nullptr)
                    continue;                         // 나갔다
                if (user->IsTransferring())
                {
                    elsewhere[nElsewhere++] = e.id;
                    continue;
                }
                push(std::move(user), e.damage);
            }
            // 기록이 비었어도(검증을 건너뛴 경로) 막타는 넣는다
            if (!killerListed && n < list.size())
            {
                if (killerHere)
                    push(killer, 1);
                else if (nElsewhere < elsewhere.size())
                    elsewhere[nElsewhere++] = killerId;
            }
            monster->m_DamageLedger.Clear();
            return n;
        }

        // 2) 그로시 · 경험치를 나눠 받는 사람 — sharing인 사람, 아무도 없으면 막타(없으면 가장 많이 때린 사람) 하나
        size_t PickSharers(const UserSharedPtr& killer, const ContributorList& list, size_t n, std::array<size_t, RewardRule::kMaxShares>& who)
        {
            size_t k = 0;
            for (size_t i = 0; i < n; ++i)
                if (list[i].sharing)
                    who[k++] = i;
            if (k > 0 || n == 0)
                return k;
            size_t fallback = 0;
            for (size_t i = 0; i < n; ++i)
            {
                if (list[i].user == killer) { fallback = i; break; }
                if (list[i].damage > list[fallback].damage) fallback = i;
            }
            who[0] = fallback;   // 경험치 · 그로시가 사라지지 않게
            return 1;
        }

        void ShareExp(UInt32 exp, const ObjID& monsterId, const ContributorList& list, const std::array<size_t, RewardRule::kMaxShares>& who, size_t k)
        {
            if (exp == 0 || k == 0)
                return;
            std::array<PartyRule::KillShareIn, RewardRule::kMaxShares> in{};
            UInt64 sumDamage = 0;
            for (size_t t = 0; t < k; ++t)
            {
                const Contributor& c = list[who[t]];
                in[t] = { c.damage, c.user->GetStat()->GetLevel(), c.partyId };
                sumDamage += c.damage > 0 ? c.damage : 1;
            }
            std::array<PartyRule::ShareOut, RewardRule::kMaxShares> out{};
            PartyRule::SplitKillExp(exp, std::span<const PartyRule::KillShareIn>(in.data(), k), PartyRule::CarryLevelRange(), PartyRule::CarryMinExpPct(),
                std::span<PartyRule::ShareOut>(out.data(), k));
            for (size_t t = 0; t < k; ++t)
            {
                const UserSharedPtr& u = list[who[t]].user;
                if (out[t].exp > 0)
                    CUserHelper::GainExp(u, out[t].exp);
                if (k >= 2)
                {
                    const UInt64 d = in[t].damage > 0 ? in[t].damage : 1;
                    SendExpShare(u, out[t].exp, static_cast<UInt8>(d * 100 / sumDamage), out[t].guaranteed, monsterId);
                }
            }
        }

        // percent = 정예 그로시 배율(elite_adena_percent — 일반 100)
        void ShareAdena(const MonsterTableRow* species, UInt32 percent, const ContributorList& list, const std::array<size_t, RewardRule::kMaxShares>& who, size_t k)
        {
            if (species == nullptr || species->adenaMax == 0 || k == 0 || (LRng() % 100) >= species->adenaChance)
                return;
            std::uniform_int_distribution<UInt32> amountRoll(species->adenaMin, species->adenaMax);
            const UInt32 rolled = LootRoll::ScalePercent(amountRoll(LRng), percent, 0, 0xFFFFFFFFu);
            std::array<UInt32, RewardRule::kMaxShares> weight{};
            std::array<UInt32, RewardRule::kMaxShares> share{};
            for (size_t t = 0; t < k; ++t)
                weight[t] = list[who[t]].damage;
            RewardRule::SplitByWeight(rolled, std::span<const UInt32>(weight.data(), k), std::span<UInt32>(share.data(), k));
            for (size_t t = 0; t < k; ++t)
            {
                const UserSharedPtr& u = list[who[t]].user;
                UInt64 amount = share[t];
                if (u->m_GearExtras.adenaFind > 0)   // 장비 옵션 "그로시 획득" — 자기 몫에만
                    amount = amount * (100u + u->m_GearExtras.adenaFind) / 100u;
                if (amount > 0)
                    u->GetInventory()->AddAdena(static_cast<UInt32>(std::min<UInt64>(amount, 0xFFFFFFFFull)), AdenaReason::Pickup);
            }
        }

        auto Rng() { return [] { return static_cast<UInt32>(LRng()); }; }

        // 3) 일반 몬스터 — 정수 · 결정: 나눠 받는 사람(who) 중 피해 비례 추첨 한 명. 몬스터가 그 사람보다 level_gap 이상 낮으면 확률 절반.
        //    정수(essence_permille) · 결정(crystal_permille)을 따로 굴리고 같은 사람이 받는다.
        void GiveEssence(const ContributorList& list, const std::array<size_t, RewardRule::kMaxShares>& who, size_t k,
            const EconRule::TierLootRow& t, int monsterLevel, CLootHelper::DropSpot& spot)
        {
            const bool essence = t.essenceItem != ITEM_TABLE_ID_NONE && t.essencePermille > 0;
            const bool crystal = t.crystalItem != ITEM_TABLE_ID_NONE && t.crystalPermille > 0;
            if (k == 0 || (!essence && !crystal))
                return;
            std::array<UInt32, RewardRule::kMaxShares> damage{};
            for (size_t i = 0; i < k; ++i)
                damage[i] = list[who[i]].damage;
            const size_t w = LootRoll::PickByDamage(std::span<const UInt32>(damage.data(), k), Rng());
            if (w == LootRoll::kNone)
                return;
            const UserSharedPtr& user = list[who[w]].user;
            const int level = user->GetStat()->GetLevel();
            const auto chance = [&](UInt32 permille)
            {
                const UInt32 p = LootRoll::GapPermille(permille, level, monsterLevel, EconRule::LevelGap());
                return p > 0 && LootRoll::Below(Rng(), LootRoll::kPermille) < p;
            };
            if (essence && chance(t.essencePermille))
                CLootHelper::Grant(user, t.essenceItem, 1, 0, CLootHelper::Source::Field, spot);
            if (crystal && chance(t.crystalPermille))
                CLootHelper::Grant(user, t.crystalItem, 1, 0, CLootHelper::Source::Field, spot);
        }

        // 4) 정예 — 자격자(피해 ≥ elite_share_percent% · 공유 범위 안 · 살아 있음, 계정당 한 번)마다 결정 · 정수 · 정예 장비 굴림
        void GiveEliteRewards(const ContributorList& list, size_t n, UInt64 ledgerTotal, int tier, CLootHelper::DropSpot& spot)
        {
            std::array<LootRoll::Claimant, RewardRule::kMaxShares> c{};
            for (size_t i = 0; i < n; ++i)
                c[i] = LootRoll::Claimant{ list[i].user->GetPlayerId(), list[i].damage, list[i].partyId, list[i].sharing, true };
            std::array<size_t, RewardRule::kMaxShares> pick{};
            const size_t m = LootRoll::PickClaimants(std::span<const LootRoll::Claimant>(c.data(), n), ledgerTotal, EconRule::EliteSharePercent(),
                true, false, std::span<size_t>(pick.data(), pick.size()));
            const EconRule::TierLootRow& t = EconRule::Tier(tier);
            for (size_t j = 0; j < m; ++j)
            {
                const UserSharedPtr& user = list[pick[j]].user;
                if (t.eliteCrystalItem != ITEM_TABLE_ID_NONE && t.eliteCrystalCount > 0)
                    CLootHelper::Grant(user, t.eliteCrystalItem, t.eliteCrystalCount, 0, CLootHelper::Source::Elite, spot);
                if (t.essenceItem != ITEM_TABLE_ID_NONE && t.eliteEssenceCount > 0)
                    CLootHelper::Grant(user, t.essenceItem, t.eliteEssenceCount, 0, CLootHelper::Source::Elite, spot);
                if (t.eliteGearPercent > 0 && LootRoll::Below(Rng(), 100u) < t.eliteGearPercent)
                {
                    const CLootHelper::Rolled r = CLootHelper::RollEliteGear(user, tier);
                    if (r.id != ITEM_TABLE_ID_NONE)
                        CLootHelper::Grant(user, r.id, 1, r.extra, CLootHelper::Source::Elite, spot);
                }
            }
        }

        // 죽은 자리에서 radius(체비셰프) 안의 이 Zone 플레이어 — 섹터 칸을 반경만큼 훑는다(3×3 이웃보다 넓을 수 있다 — exp_share_range ≤ 30)
        template <class Fn>
        void ForEachUserNear(short x, short y, int radius, Fn&& fn)
        {
            if (GSector == nullptr)
                return;
            const auto cellOf = [](int v) { return (v >= 0 ? v : v - (SECTOR_RANGE - 1)) / SECTOR_RANGE; };
            for (int cy = cellOf(y - radius); cy <= cellOf(y + radius); ++cy)
                for (int cx = cellOf(x - radius); cx <= cellOf(x + radius); ++cx)
                    GSector->ForEachObjectInCell(static_cast<short>(cx), static_cast<short>(cy), EnumCategory::eUser, [&](const SubjectSharedPtr& o)
                    {
                        if (PartyRule::ChebyshevDistance(o->GetX(), o->GetY(), x, y) <= radius)
                            fn(static_pointer_cast<CUser>(o));
                    });
        }

        // 5) 보스 개인 굴림 — 자격자(피해 ≥ boss_share_percent% · 또는 그런 사람과 같은 파티이면서 공유 범위 안 · 이 Zone 스레드)마다
        //    BossLoot 한 번(계정당 한 번). 파티원은 때리지 않았어도 된다 — 죽은 자리 곁을 훑어 찾는다.
        void GiveBossRewards(const ContributorList& list, size_t n, UInt64 ledgerTotal, const MonsterTableRow& species,
            short deathX, short deathY, CLootHelper::DropSpot& spot)
        {
            std::vector<LootRoll::Claimant> c;
            std::vector<UserSharedPtr> users;
            c.reserve(n + 8);
            users.reserve(n + 8);
            bool partyCarry = false;
            for (size_t i = 0; i < n; ++i)
            {
                c.push_back(LootRoll::Claimant{ list[i].user->GetPlayerId(), list[i].damage, list[i].partyId, list[i].sharing, true });
                users.push_back(list[i].user);
                partyCarry |= list[i].partyId != 0 && LootRoll::HasShare(list[i].damage, ledgerTotal, EconRule::BossSharePercent());
            }
            if (partyCarry)
                ForEachUserNear(deathX, deathY, PartyRule::ExpShareRange(), [&](const UserSharedPtr& user)
                {
                    const UInt32 partyId = user->m_PartyId.load(std::memory_order_relaxed);
                    if (partyId == 0 || user->IsTransferring() || !GZoneManager->IsCurrentThreadOwner(user->GetObjID()))
                        return;
                    for (const UserSharedPtr& u : users)
                        if (u == user)
                            return;   // 이미 피해 기록에 있다
                    c.push_back(LootRoll::Claimant{ user->GetPlayerId(), 0, partyId, IsSharing(*user, deathX, deathY), false });
                    users.push_back(user);
                });
            std::vector<size_t> pick(c.size());
            const size_t m = LootRoll::PickClaimants(std::span<const LootRoll::Claimant>(c), ledgerTotal, EconRule::BossSharePercent(),
                false, true, std::span<size_t>(pick));
            for (size_t j = 0; j < m; ++j)
            {
                const UserSharedPtr& user = users[pick[j]];
                const CLootHelper::Rolled r = CLootHelper::RollBossLoot(user, species);
                if (r.id != ITEM_TABLE_ID_NONE)
                    CLootHelper::Grant(user, r.id, 1, r.extra, CLootHelper::Source::Boss, spot);
            }
        }
    }

    void HandleKill(const UserSharedPtr& killer, const MonsterSharedPtr& monster, short deathX, short deathY)
    {
        if (killer == nullptr || monster == nullptr)
            return;
        const MonsterTableRow* species = monster->GetSpecies();
        const bool elite = monster->IsElite();
        const bool boss = species != nullptr && species->boss;
        const UInt32 baseExp = species != nullptr ? species->exp : (monster->GetType() == MONSTER_TYPE::PASSIVE ? 3 : 5);
        const UInt32 exp = elite ? LootRoll::ScalePercent(baseExp, EconRule::EliteExpPercent(), 0, 0xFFFFFFFFu) : baseExp;
        // 지역 단계 = 죽은 자리의 Region.tier(Monster.drop_table은 소모품 표 번호일 뿐)
        const WorldData::RegionRow* region = WorldData::FindRegion(CZoneLayout::GetZoneIdByWorld(deathX, deathY));
        const int tier = region != nullptr ? region->tier : 1;

        ContributorList list{};
        std::array<ObjID, RewardRule::kMaxShares> elsewhere{};
        size_t nElsewhere = 0;
        UInt64 ledgerTotal = 0;
        const size_t n = CollectContributors(killer, monster, deathX, deathY, list, elsewhere, nElsewhere, ledgerTotal);

        // 그로시 · 경험치 — 피해 비례(파티가 아니어도). 정예는 배율.
        std::array<size_t, RewardRule::kMaxShares> who{};
        const size_t k = PickSharers(killer, list, n, who);
        ShareExp(exp, monster->GetObjID(), list, who, k);
        ShareAdena(species, elite ? EconRule::EliteAdenaPercent() : 100u, list, who, k);

        // 전리품 — 소모품 드롭 표 · 필드 장비는 막타(자동 획득). 막타가 이 Zone에 없으면(지속 피해가 Zone을 옮긴 뒤 들어감) 가장 많이 때린 사람.
        size_t looter = n;
        for (size_t i = 0; i < n; ++i)
        {
            if (list[i].user == killer)
            {
                looter = i;
                break;
            }
            if (looter == n || list[i].damage > list[looter].damage)
                looter = i;
        }
        if (!monster->IsDummy())
        {
            CLootHelper::DropSpot spot{ deathX, deathY, 0 };
            if (looter < n)
                CLootHelper::GiveDropTable(list[looter].user, species, spot);
            if (boss)
                GiveBossRewards(list, n, ledgerTotal, *species, deathX, deathY, spot);   // 「보스는 장비 3개」는 없앴다 — 자격자마다 개인 굴림
            else if (elite)
                GiveEliteRewards(list, n, ledgerTotal, tier, spot);                      // 필드 장비 · 일반 정수는 굴리지 않는다
            else
            {
                if (looter < n)
                {
                    const CLootHelper::Rolled gear = CLootHelper::RollFieldGear(list[looter].user, tier);
                    if (gear.id != ITEM_TABLE_ID_NONE)
                        CLootHelper::Grant(list[looter].user, gear.id, 1, gear.extra, CLootHelper::Source::Field, spot);
                }
                GiveEssence(list, who, k, EconRule::Tier(tier), species != nullptr ? species->level : 1, spot);
            }
        }

        // 결사 성장 — 이 Zone에서 함께 때린 사람을 결사마다 묶어 서비스로(경험치 · 기여도 · 임무 · 출석 · 협력 처치)
        {
            std::array<UserSharedPtr, RewardRule::kMaxShares> damagers{};
            size_t nd = 0;
            for (size_t i = 0; i < n && nd < damagers.size(); ++i)
                if (list[i].user != nullptr && list[i].user->m_GuildId.load(std::memory_order_relaxed) != 0)
                    damagers[nd++] = list[i].user;
            if (nd > 0)
                CGuildHelper::OnKill(std::span<const UserSharedPtr>(damagers.data(), nd), exp,
                    species != nullptr ? species->level : 1, boss);
        }

        // 퀘스트(처치 · 지역 처치 · 수집) — 피해를 준 사람 모두. 다른 Zone에 있으면 그 Zone에서(종족은 번호로 넘겨 그쪽에서 다시 찾는다)
        if (species != nullptr)
        {
            for (size_t i = 0; i < n; ++i)
                CQuestHelper::OnKill(list[i].user, species, deathX, deathY);
            const UInt16 speciesId = species->id;
            for (size_t i = 0; i < nElsewhere; ++i)
                Service::PostToOwner(elsewhere[i], [speciesId, deathX, deathY](const UserSharedPtr& user)
                {
                    if (user != nullptr)
                        CQuestHelper::OnKill(user, MonsterTable::Find(speciesId), deathX, deathY);
                });
        }

        // 개인 업적(처치 · 정예 · 보스 · 파티 처치) — 퀘스트와 같은 자격(피해를 준 모두, 허수아비 밖). 다른 Zone에 있으면 그 사람의 Zone에서
        // (진척을 쓰는 곳은 소유 Zone 하나 — 파티 여부도 그쪽의 m_PartyId로 본다)
        if (!monster->IsDummy())
        {
            const UInt16 speciesId = species != nullptr ? species->id : 0;
            for (size_t i = 0; i < n; ++i)
                CAchieveHelper::OnKill(list[i].user, speciesId, elite, boss);
            for (size_t i = 0; i < nElsewhere; ++i)
                Service::PostToOwner(elsewhere[i], [speciesId, elite, boss](const UserSharedPtr& user)
                {
                    if (user != nullptr)
                        CAchieveHelper::OnKill(user, speciesId, elite, boss);
                });
        }
    }

    void AnnounceLoot(const UserSharedPtr& user, ItemTableId itemId, UInt64 extra)
    {
        if (user == nullptr)
            return;
        const ItemTableRow* row = ItemTable::Find(itemId);
        if (row == nullptr)
            return;
        const Rarity rarity = ItemRules::EffectiveRarity(*row, extra);
        if (ItemRules::RarityLootNotice(rarity))
        {
            // 예: "성준 님이 [희귀] 서리 단검을 획득했습니다." — 이름 · 희귀도 · 아이템 이름은 기획 데이터(Rarity.name · Item.display_name)
            const std::string_view itemName = (row->displayName != nullptr && row->displayName[0] != '\0') ? row->displayName : row->name;
            std::string text;
            text.reserve(CHAT_SIZE);
            text.append(user->GetName()).append(" 님이 [").append(ItemRules::RarityName(rarity)).append("] ")
                .append(itemName).append(ObjectParticle(itemName)).append(" 획득했습니다.");
            CChatHelper::BroadcastSystem(user->GetObjID(), user->GetName(), text.c_str());
        }
        // 신화는 화면 가운데 공지도(보상의 순간을 모두가 본다 — 예전 그대로)
        if (rarity == Rarity::Mythic)
            CUserHelper::BroadcastWorldNotice(WorldNoticeCode::RareLoot, itemId, static_cast<Int32>(Rarity::Mythic), user->GetName());
    }
}
