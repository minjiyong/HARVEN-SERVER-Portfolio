#pragma once
// 생성물 — `python3 Tools/DataTool/datatool.py gen` (dt/enums.py). 손으로 고치지 않는다.
// C++ 열거형 ↔ 이름 글자. 시트 · GameData JSON은 이름으로 적는다(SHEET_DATA_PROMPT [5]).
// 열거자마다 static_assert로 값이 헤더와 같은지 확인한다 — 헤더가 바뀌면 다시 생성해야 빌드된다.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Database/GameData/CombatData.h"
#include "Database/GameData/SkillData.h"
#include "Database/GameData/SkillGrowth.h"
#include "Database/GameData/MonsterTable.h"
#include "Database/GameData/MonsterAITypes.h"
#include "Database/GameData/ItemRules.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/QuestData.h"
#include "Database/GameData/TownTypes.h"
#include "Database/GameData/Item/ItemTableRow.h"
#include "Database/GameData/GuildGrowth.h"
#include "Database/GameData/AchieveRules.h"

namespace GameDataEnums
{
	struct Item { std::string_view name; int value; };

	template <class E> struct Info;   // 정의는 아래 특수화만

	template <> struct Info<ClassId>
	{
		static constexpr std::string_view kName = "ClassId";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Knight", 0 },
			{ "Engraver", 1 },
			{ "NightHunter", 2 },
		};
		static constexpr size_t kCount = 3;
	};
	static_assert(static_cast<int>(ClassId::Knight) == 0);
	static_assert(static_cast<int>(ClassId::Engraver) == 1);
	static_assert(static_cast<int>(ClassId::NightHunter) == 2);
	static_assert(static_cast<int>(ClassId::Count) == 3, "ClassId: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<StatusId>
	{
		static constexpr std::string_view kName = "StatusId";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Stun", 1 },
			{ "Freeze", 2 },
			{ "Burn", 3 },
			{ "Poison", 4 },
			{ "Slow", 5 },
			{ "Guard", 6 },
			{ "Ambush", 7 },
			{ "Evade", 8 },
			{ "Marked", 9 },
			{ "Oath", 10 },
			{ "Rally", 11 },
			{ "Veil", 12 },
			{ "Taunt", 13 },
			{ "Broken", 14 },
			{ "Clone", 15 },
			{ "Empower", 16 },
			{ "Fortify", 17 },
			{ "Swift", 18 },
		};
		static constexpr size_t kCount = 19;
	};
	static_assert(static_cast<int>(StatusId::None) == 0);
	static_assert(static_cast<int>(StatusId::Stun) == 1);
	static_assert(static_cast<int>(StatusId::Freeze) == 2);
	static_assert(static_cast<int>(StatusId::Burn) == 3);
	static_assert(static_cast<int>(StatusId::Poison) == 4);
	static_assert(static_cast<int>(StatusId::Slow) == 5);
	static_assert(static_cast<int>(StatusId::Guard) == 6);
	static_assert(static_cast<int>(StatusId::Ambush) == 7);
	static_assert(static_cast<int>(StatusId::Evade) == 8);
	static_assert(static_cast<int>(StatusId::Marked) == 9);
	static_assert(static_cast<int>(StatusId::Oath) == 10);
	static_assert(static_cast<int>(StatusId::Rally) == 11);
	static_assert(static_cast<int>(StatusId::Veil) == 12);
	static_assert(static_cast<int>(StatusId::Taunt) == 13);
	static_assert(static_cast<int>(StatusId::Broken) == 14);
	static_assert(static_cast<int>(StatusId::Clone) == 15);
	static_assert(static_cast<int>(StatusId::Empower) == 16);
	static_assert(static_cast<int>(StatusId::Fortify) == 17);
	static_assert(static_cast<int>(StatusId::Swift) == 18);
	static_assert(static_cast<int>(StatusId::Count) == 19, "StatusId: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<SkillTarget>
	{
		static constexpr std::string_view kName = "SkillTarget";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Self", 0 },
			{ "Enemy", 1 },
			{ "Ground", 2 },
			{ "Direction", 3 },
		};
		static constexpr size_t kCount = 4;
	};
	static_assert(static_cast<int>(SkillTarget::Self) == 0);
	static_assert(static_cast<int>(SkillTarget::Enemy) == 1);
	static_assert(static_cast<int>(SkillTarget::Ground) == 2);
	static_assert(static_cast<int>(SkillTarget::Direction) == 3);

	template <> struct Info<SkillShape>
	{
		static constexpr std::string_view kName = "SkillShape";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Single", 0 },
			{ "Around", 1 },
			{ "Circle", 2 },
			{ "Line", 3 },
			{ "Chain", 4 },
		};
		static constexpr size_t kCount = 5;
	};
	static_assert(static_cast<int>(SkillShape::Single) == 0);
	static_assert(static_cast<int>(SkillShape::Around) == 1);
	static_assert(static_cast<int>(SkillShape::Circle) == 2);
	static_assert(static_cast<int>(SkillShape::Line) == 3);
	static_assert(static_cast<int>(SkillShape::Chain) == 4);

	template <> struct Info<DamageKind>
	{
		static constexpr std::string_view kName = "DamageKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Physical", 0 },
			{ "Magic", 1 },
		};
		static constexpr size_t kCount = 2;
	};
	static_assert(static_cast<int>(DamageKind::Physical) == 0);
	static_assert(static_cast<int>(DamageKind::Magic) == 1);

	template <> struct Info<SkillKey>
	{
		static constexpr std::string_view kName = "SkillKey";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "C", 0 },
			{ "Q", 1 },
			{ "W", 2 },
			{ "E", 3 },
			{ "R", 4 },
			{ "A", 5 },
			{ "S", 6 },
			{ "D", 7 },
			{ "F", 8 },
			{ "V", 9 },
			{ "Space", 10 },
			{ "Z", 11 },
		};
		static constexpr size_t kCount = 12;
	};
	static_assert(static_cast<int>(SkillKey::C) == 0);
	static_assert(static_cast<int>(SkillKey::Q) == 1);
	static_assert(static_cast<int>(SkillKey::W) == 2);
	static_assert(static_cast<int>(SkillKey::E) == 3);
	static_assert(static_cast<int>(SkillKey::R) == 4);
	static_assert(static_cast<int>(SkillKey::A) == 5);
	static_assert(static_cast<int>(SkillKey::S) == 6);
	static_assert(static_cast<int>(SkillKey::D) == 7);
	static_assert(static_cast<int>(SkillKey::F) == 8);
	static_assert(static_cast<int>(SkillKey::V) == 9);
	static_assert(static_cast<int>(SkillKey::Space) == 10);
	static_assert(static_cast<int>(SkillKey::Z) == 11);
	static_assert(static_cast<int>(SkillKey::Count) == 12, "SkillKey: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<SkillKind>
	{
		static constexpr std::string_view kName = "SkillKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Basic", 0 },
			{ "Normal", 1 },
			{ "Charge", 2 },
			{ "Hold", 3 },
			{ "Cast", 4 },
			{ "Chain", 5 },
			{ "Toggle", 6 },
			{ "Move", 7 },
			{ "Identity", 8 },
			{ "Awaken", 9 },
		};
		static constexpr size_t kCount = 10;
	};
	static_assert(static_cast<int>(SkillKind::Basic) == 0);
	static_assert(static_cast<int>(SkillKind::Normal) == 1);
	static_assert(static_cast<int>(SkillKind::Charge) == 2);
	static_assert(static_cast<int>(SkillKind::Hold) == 3);
	static_assert(static_cast<int>(SkillKind::Cast) == 4);
	static_assert(static_cast<int>(SkillKind::Chain) == 5);
	static_assert(static_cast<int>(SkillKind::Toggle) == 6);
	static_assert(static_cast<int>(SkillKind::Move) == 7);
	static_assert(static_cast<int>(SkillKind::Identity) == 8);
	static_assert(static_cast<int>(SkillKind::Awaken) == 9);

	template <> struct Info<HitShape>
	{
		static constexpr std::string_view kName = "HitShape";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Target", 0 },
			{ "Circle", 1 },
			{ "Cone", 2 },
			{ "Line", 3 },
			{ "Ring", 4 },
		};
		static constexpr size_t kCount = 5;
	};
	static_assert(static_cast<int>(HitShape::Target) == 0);
	static_assert(static_cast<int>(HitShape::Circle) == 1);
	static_assert(static_cast<int>(HitShape::Cone) == 2);
	static_assert(static_cast<int>(HitShape::Line) == 3);
	static_assert(static_cast<int>(HitShape::Ring) == 4);

	template <> struct Info<HitAnchor>
	{
		static constexpr std::string_view kName = "HitAnchor";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Caster", 0 },
			{ "Aim", 1 },
			{ "Target", 2 },
		};
		static constexpr size_t kCount = 3;
	};
	static_assert(static_cast<int>(HitAnchor::Caster) == 0);
	static_assert(static_cast<int>(HitAnchor::Aim) == 1);
	static_assert(static_cast<int>(HitAnchor::Target) == 2);

	template <> struct Info<HitReact>
	{
		static constexpr std::string_view kName = "HitReact";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Flinch", 1 },
			{ "Knockback", 2 },
			{ "Launch", 3 },
			{ "Knockdown", 4 },
			{ "Pull", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(HitReact::None) == 0);
	static_assert(static_cast<int>(HitReact::Flinch) == 1);
	static_assert(static_cast<int>(HitReact::Knockback) == 2);
	static_assert(static_cast<int>(HitReact::Launch) == 3);
	static_assert(static_cast<int>(HitReact::Knockdown) == 4);
	static_assert(static_cast<int>(HitReact::Pull) == 5);

	template <> struct Info<HitFlag>
	{
		static constexpr std::string_view kName = "HitFlag";
		static constexpr bool kFlags = true;
		static constexpr Item kItems[] = {
			{ "HF_Status", 1 },
			{ "HF_Heavy", 2 },
			{ "HF_Counter", 4 },
			{ "HF_Execute", 8 },
			{ "HF_Tick", 16 },
			{ "HF_Release", 32 },
			{ "HF_Perfect", 64 },
			{ "HF_Final", 128 },
		};
		static constexpr size_t kCount = 8;
	};
	static_assert(static_cast<int>(HitFlag::HF_Status) == 1);
	static_assert(static_cast<int>(HitFlag::HF_Heavy) == 2);
	static_assert(static_cast<int>(HitFlag::HF_Counter) == 4);
	static_assert(static_cast<int>(HitFlag::HF_Execute) == 8);
	static_assert(static_cast<int>(HitFlag::HF_Tick) == 16);
	static_assert(static_cast<int>(HitFlag::HF_Release) == 32);
	static_assert(static_cast<int>(HitFlag::HF_Perfect) == 64);
	static_assert(static_cast<int>(HitFlag::HF_Final) == 128);

	template <> struct Info<SkillSpecial>
	{
		static constexpr std::string_view kName = "SkillSpecial";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Legacy", 1 },
			{ "ShieldBounce", 2 },
			{ "Rally", 3 },
			{ "Roll", 4 },
			{ "OathRelease", 5 },
			{ "Barrier", 6 },
			{ "Resonance", 7 },
			{ "Clones", 8 },
			{ "Veil", 9 },
			{ "PoisonMist", 10 },
			{ "MoonCleave", 11 },
			{ "DuskAwaken", 12 },
			{ "Dodge", 13 },
			{ "Blink", 14 },
			{ "Dash", 15 },
			{ "ShadowStep", 16 },
		};
		static constexpr size_t kCount = 17;
	};
	static_assert(static_cast<int>(SkillSpecial::None) == 0);
	static_assert(static_cast<int>(SkillSpecial::Legacy) == 1);
	static_assert(static_cast<int>(SkillSpecial::ShieldBounce) == 2);
	static_assert(static_cast<int>(SkillSpecial::Rally) == 3);
	static_assert(static_cast<int>(SkillSpecial::Roll) == 4);
	static_assert(static_cast<int>(SkillSpecial::OathRelease) == 5);
	static_assert(static_cast<int>(SkillSpecial::Barrier) == 6);
	static_assert(static_cast<int>(SkillSpecial::Resonance) == 7);
	static_assert(static_cast<int>(SkillSpecial::Clones) == 8);
	static_assert(static_cast<int>(SkillSpecial::Veil) == 9);
	static_assert(static_cast<int>(SkillSpecial::PoisonMist) == 10);
	static_assert(static_cast<int>(SkillSpecial::MoonCleave) == 11);
	static_assert(static_cast<int>(SkillSpecial::DuskAwaken) == 12);
	static_assert(static_cast<int>(SkillSpecial::Dodge) == 13);
	static_assert(static_cast<int>(SkillSpecial::Blink) == 14);
	static_assert(static_cast<int>(SkillSpecial::Dash) == 15);
	static_assert(static_cast<int>(SkillSpecial::ShadowStep) == 16);

	template <> struct Info<SkillGrowth::Mod>
	{
		static constexpr std::string_view kName = "GrowthMod";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Wider", 1 },
			{ "Echo", 2 },
			{ "Heavy", 3 },
			{ "Swift", 4 },
			{ "Twin", 5 },
			{ "Pull", 6 },
			{ "Element", 7 },
			{ "Lasting", 8 },
			{ "Longer", 9 },
			{ "Extra", 10 },
			{ "Steady", 11 },
		};
		static constexpr size_t kCount = 12;
	};
	static_assert(static_cast<int>(SkillGrowth::Mod::None) == 0);
	static_assert(static_cast<int>(SkillGrowth::Mod::Wider) == 1);
	static_assert(static_cast<int>(SkillGrowth::Mod::Echo) == 2);
	static_assert(static_cast<int>(SkillGrowth::Mod::Heavy) == 3);
	static_assert(static_cast<int>(SkillGrowth::Mod::Swift) == 4);
	static_assert(static_cast<int>(SkillGrowth::Mod::Twin) == 5);
	static_assert(static_cast<int>(SkillGrowth::Mod::Pull) == 6);
	static_assert(static_cast<int>(SkillGrowth::Mod::Element) == 7);
	static_assert(static_cast<int>(SkillGrowth::Mod::Lasting) == 8);
	static_assert(static_cast<int>(SkillGrowth::Mod::Longer) == 9);
	static_assert(static_cast<int>(SkillGrowth::Mod::Extra) == 10);
	static_assert(static_cast<int>(SkillGrowth::Mod::Steady) == 11);
	static_assert(static_cast<int>(SkillGrowth::Mod::Count) == 12, "SkillGrowth::Mod: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<MonsterArchetype>
	{
		static constexpr std::string_view kName = "MonsterArchetype";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eBlob", 0 },
			{ "eQuadruped", 1 },
			{ "eHumanoid", 2 },
			{ "eInsect", 3 },
			{ "eGolem", 4 },
			{ "eGhost", 5 },
			{ "eDragon", 6 },
			{ "eDemon", 7 },
			{ "eCrab", 8 },
			{ "eMushroom", 9 },
			{ "eSkeleton", 10 },
			{ "eDummy", 11 },
		};
		static constexpr size_t kCount = 12;
	};
	static_assert(static_cast<int>(MonsterArchetype::eBlob) == 0);
	static_assert(static_cast<int>(MonsterArchetype::eQuadruped) == 1);
	static_assert(static_cast<int>(MonsterArchetype::eHumanoid) == 2);
	static_assert(static_cast<int>(MonsterArchetype::eInsect) == 3);
	static_assert(static_cast<int>(MonsterArchetype::eGolem) == 4);
	static_assert(static_cast<int>(MonsterArchetype::eGhost) == 5);
	static_assert(static_cast<int>(MonsterArchetype::eDragon) == 6);
	static_assert(static_cast<int>(MonsterArchetype::eDemon) == 7);
	static_assert(static_cast<int>(MonsterArchetype::eCrab) == 8);
	static_assert(static_cast<int>(MonsterArchetype::eMushroom) == 9);
	static_assert(static_cast<int>(MonsterArchetype::eSkeleton) == 10);
	static_assert(static_cast<int>(MonsterArchetype::eDummy) == 11);

	template <> struct Info<MonsterVariant>
	{
		static constexpr std::string_view kName = "MonsterVariant";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eNormal", 0 },
			{ "eScorpion", 1 },
			{ "eSpider", 2 },
			{ "eArmored", 3 },
			{ "eZombie", 4 },
			{ "eLava", 5 },
			{ "eIce", 6 },
		};
		static constexpr size_t kCount = 7;
	};
	static_assert(static_cast<int>(MonsterVariant::eNormal) == 0);
	static_assert(static_cast<int>(MonsterVariant::eScorpion) == 1);
	static_assert(static_cast<int>(MonsterVariant::eSpider) == 2);
	static_assert(static_cast<int>(MonsterVariant::eArmored) == 3);
	static_assert(static_cast<int>(MonsterVariant::eZombie) == 4);
	static_assert(static_cast<int>(MonsterVariant::eLava) == 5);
	static_assert(static_cast<int>(MonsterVariant::eIce) == 6);

	template <> struct Info<MonsterPattern>
	{
		static constexpr std::string_view kName = "MonsterPattern";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Charge", 1 },
			{ "Slam", 2 },
			{ "Ranged", 3 },
			{ "Leap", 4 },
		};
		static constexpr size_t kCount = 5;
	};
	static_assert(static_cast<int>(MonsterPattern::None) == 0);
	static_assert(static_cast<int>(MonsterPattern::Charge) == 1);
	static_assert(static_cast<int>(MonsterPattern::Slam) == 2);
	static_assert(static_cast<int>(MonsterPattern::Ranged) == 3);
	static_assert(static_cast<int>(MonsterPattern::Leap) == 4);

	template <> struct Info<MonsterAiType>
	{
		static constexpr std::string_view kName = "MonsterAiType";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Melee", 0 },
			{ "Ranged", 1 },
			{ "Support", 2 },
			{ "Boss", 3 },
			{ "Stationary", 4 },
			{ "Dummy", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(MonsterAiType::Melee) == 0);
	static_assert(static_cast<int>(MonsterAiType::Ranged) == 1);
	static_assert(static_cast<int>(MonsterAiType::Support) == 2);
	static_assert(static_cast<int>(MonsterAiType::Boss) == 3);
	static_assert(static_cast<int>(MonsterAiType::Stationary) == 4);
	static_assert(static_cast<int>(MonsterAiType::Dummy) == 5);

	template <> struct Info<MonsterSkillTarget>
	{
		static constexpr std::string_view kName = "MonsterSkillTarget";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Enemy", 0 },
			{ "Self", 1 },
			{ "Ally", 2 },
			{ "AllyGroup", 3 },
		};
		static constexpr size_t kCount = 4;
	};
	static_assert(static_cast<int>(MonsterSkillTarget::Enemy) == 0);
	static_assert(static_cast<int>(MonsterSkillTarget::Self) == 1);
	static_assert(static_cast<int>(MonsterSkillTarget::Ally) == 2);
	static_assert(static_cast<int>(MonsterSkillTarget::AllyGroup) == 3);

	template <> struct Info<MonsterMove>
	{
		static constexpr std::string_view kName = "MonsterMove";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Dash", 1 },
			{ "Leap", 2 },
			{ "Blink", 3 },
			{ "Retreat", 4 },
		};
		static constexpr size_t kCount = 5;
	};
	static_assert(static_cast<int>(MonsterMove::None) == 0);
	static_assert(static_cast<int>(MonsterMove::Dash) == 1);
	static_assert(static_cast<int>(MonsterMove::Leap) == 2);
	static_assert(static_cast<int>(MonsterMove::Blink) == 3);
	static_assert(static_cast<int>(MonsterMove::Retreat) == 4);

	template <> struct Info<AoeAffect>
	{
		static constexpr std::string_view kName = "AoeAffect";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Enemies", 0 },
			{ "Allies", 1 },
			{ "All", 2 },
		};
		static constexpr size_t kCount = 3;
	};
	static_assert(static_cast<int>(AoeAffect::Enemies) == 0);
	static_assert(static_cast<int>(AoeAffect::Allies) == 1);
	static_assert(static_cast<int>(AoeAffect::All) == 2);

	template <> struct Info<MonsterFx>
	{
		static constexpr std::string_view kName = "MonsterFx";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "BossSlam", 1 },
			{ "Fire", 2 },
			{ "Poison", 3 },
			{ "Frost", 4 },
			{ "Thunder", 5 },
			{ "Rune", 6 },
			{ "Steam", 7 },
			{ "Heal", 8 },
			{ "Haste", 9 },
			{ "Guard", 10 },
		};
		static constexpr size_t kCount = 11;
	};
	static_assert(static_cast<int>(MonsterFx::None) == 0);
	static_assert(static_cast<int>(MonsterFx::BossSlam) == 1);
	static_assert(static_cast<int>(MonsterFx::Fire) == 2);
	static_assert(static_cast<int>(MonsterFx::Poison) == 3);
	static_assert(static_cast<int>(MonsterFx::Frost) == 4);
	static_assert(static_cast<int>(MonsterFx::Thunder) == 5);
	static_assert(static_cast<int>(MonsterFx::Rune) == 6);
	static_assert(static_cast<int>(MonsterFx::Steam) == 7);
	static_assert(static_cast<int>(MonsterFx::Heal) == 8);
	static_assert(static_cast<int>(MonsterFx::Haste) == 9);
	static_assert(static_cast<int>(MonsterFx::Guard) == 10);

	template <> struct Info<ItemType>
	{
		static constexpr std::string_view kName = "ItemType";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eConsumable", 0 },
			{ "eEquipment", 1 },
			{ "eMaterial", 2 },
			{ "eEtc", 3 },
			{ "eCurrency", 4 },
		};
		static constexpr size_t kCount = 5;
	};
	static_assert(static_cast<int>(ItemType::eConsumable) == 0);
	static_assert(static_cast<int>(ItemType::eEquipment) == 1);
	static_assert(static_cast<int>(ItemType::eMaterial) == 2);
	static_assert(static_cast<int>(ItemType::eEtc) == 3);
	static_assert(static_cast<int>(ItemType::eCurrency) == 4);

	template <> struct Info<ItemEffect>
	{
		static constexpr std::string_view kName = "ItemEffect";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eNone", 0 },
			{ "eHeal", 1 },
			{ "eReturnTown", 2 },
			{ "eTeleportRandom", 3 },
			{ "eHaste", 4 },
			{ "eEnchantWeapon", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(ItemEffect::eNone) == 0);
	static_assert(static_cast<int>(ItemEffect::eHeal) == 1);
	static_assert(static_cast<int>(ItemEffect::eReturnTown) == 2);
	static_assert(static_cast<int>(ItemEffect::eTeleportRandom) == 3);
	static_assert(static_cast<int>(ItemEffect::eHaste) == 4);
	static_assert(static_cast<int>(ItemEffect::eEnchantWeapon) == 5);

	template <> struct Info<ItemGrade>
	{
		static constexpr std::string_view kName = "ItemGrade";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eNone", 0 },
			{ "eRare", 1 },
			{ "eEpic", 2 },
			{ "eUnique", 3 },
			{ "eLegendary", 4 },
			{ "eMythic", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(ItemGrade::eNone) == 0);
	static_assert(static_cast<int>(ItemGrade::eRare) == 1);
	static_assert(static_cast<int>(ItemGrade::eEpic) == 2);
	static_assert(static_cast<int>(ItemGrade::eUnique) == 3);
	static_assert(static_cast<int>(ItemGrade::eLegendary) == 4);
	static_assert(static_cast<int>(ItemGrade::eMythic) == 5);

	template <> struct Info<EquipSlot>
	{
		static constexpr std::string_view kName = "EquipSlot";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Weapon", 1 },
			{ "Helmet", 2 },
			{ "Armor", 3 },
			{ "Gloves", 4 },
			{ "Boots", 5 },
			{ "Cloak", 6 },
			{ "Necklace", 7 },
			{ "Ring", 8 },
		};
		static constexpr size_t kCount = 9;
	};
	static_assert(static_cast<int>(EquipSlot::None) == 0);
	static_assert(static_cast<int>(EquipSlot::Weapon) == 1);
	static_assert(static_cast<int>(EquipSlot::Helmet) == 2);
	static_assert(static_cast<int>(EquipSlot::Armor) == 3);
	static_assert(static_cast<int>(EquipSlot::Gloves) == 4);
	static_assert(static_cast<int>(EquipSlot::Boots) == 5);
	static_assert(static_cast<int>(EquipSlot::Cloak) == 6);
	static_assert(static_cast<int>(EquipSlot::Necklace) == 7);
	static_assert(static_cast<int>(EquipSlot::Ring) == 8);
	static_assert(static_cast<int>(EquipSlot::Count) == 9, "EquipSlot: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<WeaponKind>
	{
		static constexpr std::string_view kName = "WeaponKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Sword", 1 },
			{ "Greatsword", 2 },
			{ "Saber", 3 },
			{ "Dagger", 4 },
			{ "Staff", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(WeaponKind::None) == 0);
	static_assert(static_cast<int>(WeaponKind::Sword) == 1);
	static_assert(static_cast<int>(WeaponKind::Greatsword) == 2);
	static_assert(static_cast<int>(WeaponKind::Saber) == 3);
	static_assert(static_cast<int>(WeaponKind::Dagger) == 4);
	static_assert(static_cast<int>(WeaponKind::Staff) == 5);

	template <> struct Info<ItemRoute>
	{
		static constexpr std::string_view kName = "ItemRoute";
		static constexpr bool kFlags = true;
		static constexpr Item kItems[] = {
			{ "Shop", 1 },
			{ "Field", 2 },
			{ "Elite", 4 },
			{ "Boss", 8 },
			{ "Craft", 16 },
			{ "Quest", 32 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(ItemRoute::Shop) == 1);
	static_assert(static_cast<int>(ItemRoute::Field) == 2);
	static_assert(static_cast<int>(ItemRoute::Elite) == 4);
	static_assert(static_cast<int>(ItemRoute::Boss) == 8);
	static_assert(static_cast<int>(ItemRoute::Craft) == 16);
	static_assert(static_cast<int>(ItemRoute::Quest) == 32);

	template <> struct Info<ItemBind>
	{
		static constexpr std::string_view kName = "ItemBind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eNone", 0 },
			{ "eAcquire", 1 },
		};
		static constexpr size_t kCount = 2;
	};
	static_assert(static_cast<int>(ItemBind::eNone) == 0);
	static_assert(static_cast<int>(ItemBind::eAcquire) == 1);

	template <> struct Info<CraftPart>
	{
		static constexpr std::string_view kName = "CraftPart";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eWeapon", 0 },
			{ "eArmor", 1 },
			{ "eAccessory", 2 },
		};
		static constexpr size_t kCount = 3;
	};
	static_assert(static_cast<int>(CraftPart::eWeapon) == 0);
	static_assert(static_cast<int>(CraftPart::eArmor) == 1);
	static_assert(static_cast<int>(CraftPart::eAccessory) == 2);
	static_assert(static_cast<int>(CraftPart::Count) == 3, "CraftPart: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<Rarity>
	{
		static constexpr std::string_view kName = "Rarity";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Common", 0 },
			{ "Uncommon", 1 },
			{ "Rare", 2 },
			{ "Heroic", 3 },
			{ "Legendary", 4 },
			{ "Mythic", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(Rarity::Common) == 0);
	static_assert(static_cast<int>(Rarity::Uncommon) == 1);
	static_assert(static_cast<int>(Rarity::Rare) == 2);
	static_assert(static_cast<int>(Rarity::Heroic) == 3);
	static_assert(static_cast<int>(Rarity::Legendary) == 4);
	static_assert(static_cast<int>(Rarity::Mythic) == 5);
	static_assert(static_cast<int>(Rarity::Count) == 6, "Rarity: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<ItemOption>
	{
		static constexpr std::string_view kName = "ItemOption";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Str", 1 },
			{ "Dex", 2 },
			{ "Int", 3 },
			{ "Vit", 4 },
			{ "Wis", 5 },
			{ "Luk", 6 },
			{ "Attack", 7 },
			{ "Magic", 8 },
			{ "Armor", 9 },
			{ "MagicArmor", 10 },
			{ "MaxHp", 11 },
			{ "MaxMp", 12 },
			{ "CritChance", 13 },
			{ "CritDamage", 14 },
			{ "AttackSpeed", 15 },
			{ "MoveSpeed", 16 },
			{ "CooldownReduction", 17 },
			{ "Penetration", 18 },
			{ "HpRegen", 19 },
			{ "MpRegen", 20 },
			{ "AttackPercent", 21 },
			{ "StatusResist", 22 },
			{ "SkillBoost", 23 },
			{ "AdenaFind", 24 },
			{ "ItemFind", 25 },
		};
		static constexpr size_t kCount = 26;
	};
	static_assert(static_cast<int>(ItemOption::None) == 0);
	static_assert(static_cast<int>(ItemOption::Str) == 1);
	static_assert(static_cast<int>(ItemOption::Dex) == 2);
	static_assert(static_cast<int>(ItemOption::Int) == 3);
	static_assert(static_cast<int>(ItemOption::Vit) == 4);
	static_assert(static_cast<int>(ItemOption::Wis) == 5);
	static_assert(static_cast<int>(ItemOption::Luk) == 6);
	static_assert(static_cast<int>(ItemOption::Attack) == 7);
	static_assert(static_cast<int>(ItemOption::Magic) == 8);
	static_assert(static_cast<int>(ItemOption::Armor) == 9);
	static_assert(static_cast<int>(ItemOption::MagicArmor) == 10);
	static_assert(static_cast<int>(ItemOption::MaxHp) == 11);
	static_assert(static_cast<int>(ItemOption::MaxMp) == 12);
	static_assert(static_cast<int>(ItemOption::CritChance) == 13);
	static_assert(static_cast<int>(ItemOption::CritDamage) == 14);
	static_assert(static_cast<int>(ItemOption::AttackSpeed) == 15);
	static_assert(static_cast<int>(ItemOption::MoveSpeed) == 16);
	static_assert(static_cast<int>(ItemOption::CooldownReduction) == 17);
	static_assert(static_cast<int>(ItemOption::Penetration) == 18);
	static_assert(static_cast<int>(ItemOption::HpRegen) == 19);
	static_assert(static_cast<int>(ItemOption::MpRegen) == 20);
	static_assert(static_cast<int>(ItemOption::AttackPercent) == 21);
	static_assert(static_cast<int>(ItemOption::StatusResist) == 22);
	static_assert(static_cast<int>(ItemOption::SkillBoost) == 23);
	static_assert(static_cast<int>(ItemOption::AdenaFind) == 24);
	static_assert(static_cast<int>(ItemOption::ItemFind) == 25);
	static_assert(static_cast<int>(ItemOption::Count) == 26, "ItemOption: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<ItemProc>
	{
		static constexpr std::string_view kName = "ItemProc";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Thunder", 1 },
			{ "Lifesteal", 2 },
			{ "FrostTouch", 3 },
			{ "Execution", 4 },
			{ "Echo", 5 },
			{ "Guardian", 6 },
			{ "Ember", 7 },
			{ "Venom", 8 },
		};
		static constexpr size_t kCount = 9;
	};
	static_assert(static_cast<int>(ItemProc::None) == 0);
	static_assert(static_cast<int>(ItemProc::Thunder) == 1);
	static_assert(static_cast<int>(ItemProc::Lifesteal) == 2);
	static_assert(static_cast<int>(ItemProc::FrostTouch) == 3);
	static_assert(static_cast<int>(ItemProc::Execution) == 4);
	static_assert(static_cast<int>(ItemProc::Echo) == 5);
	static_assert(static_cast<int>(ItemProc::Guardian) == 6);
	static_assert(static_cast<int>(ItemProc::Ember) == 7);
	static_assert(static_cast<int>(ItemProc::Venom) == 8);
	static_assert(static_cast<int>(ItemProc::Count) == 9, "ItemProc: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<WorldData::Biome>
	{
		static constexpr std::string_view kName = "Biome";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eMeadow", 0 },
			{ "eForest", 1 },
			{ "eSnow", 2 },
			{ "eCanyon", 3 },
			{ "eVolcanic", 4 },
			{ "eFairy", 5 },
			{ "eRuins", 6 },
			{ "eOrcFort", 7 },
			{ "eSwamp", 8 },
			{ "eDesert", 9 },
			{ "eWasteland", 10 },
			{ "eGraveyard", 11 },
			{ "eBeach", 12 },
			{ "eAntHill", 13 },
			{ "eHell", 14 },
		};
		static constexpr size_t kCount = 15;
	};
	static_assert(static_cast<int>(WorldData::Biome::eMeadow) == 0);
	static_assert(static_cast<int>(WorldData::Biome::eForest) == 1);
	static_assert(static_cast<int>(WorldData::Biome::eSnow) == 2);
	static_assert(static_cast<int>(WorldData::Biome::eCanyon) == 3);
	static_assert(static_cast<int>(WorldData::Biome::eVolcanic) == 4);
	static_assert(static_cast<int>(WorldData::Biome::eFairy) == 5);
	static_assert(static_cast<int>(WorldData::Biome::eRuins) == 6);
	static_assert(static_cast<int>(WorldData::Biome::eOrcFort) == 7);
	static_assert(static_cast<int>(WorldData::Biome::eSwamp) == 8);
	static_assert(static_cast<int>(WorldData::Biome::eDesert) == 9);
	static_assert(static_cast<int>(WorldData::Biome::eWasteland) == 10);
	static_assert(static_cast<int>(WorldData::Biome::eGraveyard) == 11);
	static_assert(static_cast<int>(WorldData::Biome::eBeach) == 12);
	static_assert(static_cast<int>(WorldData::Biome::eAntHill) == 13);
	static_assert(static_cast<int>(WorldData::Biome::eHell) == 14);

	template <> struct Info<WorldData::NpcKind>
	{
		static constexpr std::string_view kName = "NpcKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eMerchant", 0 },
			{ "eWeaponSmith", 1 },
			{ "eTeleporter", 2 },
			{ "eGuard", 3 },
			{ "eElder", 4 },
			{ "ePriest", 5 },
			{ "eInnkeeper", 6 },
			{ "eBard", 7 },
			{ "eChild", 8 },
			{ "eVillager", 9 },
			{ "eFarmer", 10 },
			{ "eDog", 11 },
			{ "eCat", 12 },
			{ "eChicken", 13 },
		};
		static constexpr size_t kCount = 14;
	};
	static_assert(static_cast<int>(WorldData::NpcKind::eMerchant) == 0);
	static_assert(static_cast<int>(WorldData::NpcKind::eWeaponSmith) == 1);
	static_assert(static_cast<int>(WorldData::NpcKind::eTeleporter) == 2);
	static_assert(static_cast<int>(WorldData::NpcKind::eGuard) == 3);
	static_assert(static_cast<int>(WorldData::NpcKind::eElder) == 4);
	static_assert(static_cast<int>(WorldData::NpcKind::ePriest) == 5);
	static_assert(static_cast<int>(WorldData::NpcKind::eInnkeeper) == 6);
	static_assert(static_cast<int>(WorldData::NpcKind::eBard) == 7);
	static_assert(static_cast<int>(WorldData::NpcKind::eChild) == 8);
	static_assert(static_cast<int>(WorldData::NpcKind::eVillager) == 9);
	static_assert(static_cast<int>(WorldData::NpcKind::eFarmer) == 10);
	static_assert(static_cast<int>(WorldData::NpcKind::eDog) == 11);
	static_assert(static_cast<int>(WorldData::NpcKind::eCat) == 12);
	static_assert(static_cast<int>(WorldData::NpcKind::eChicken) == 13);

	template <> struct Info<QuestData::QuestKind>
	{
		static constexpr std::string_view kName = "QuestKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Main", 0 },
			{ "Side", 1 },
			{ "Daily", 2 },
			{ "Class", 3 },
		};
		static constexpr size_t kCount = 4;
	};
	static_assert(static_cast<int>(QuestData::QuestKind::Main) == 0);
	static_assert(static_cast<int>(QuestData::QuestKind::Side) == 1);
	static_assert(static_cast<int>(QuestData::QuestKind::Daily) == 2);
	static_assert(static_cast<int>(QuestData::QuestKind::Class) == 3);

	template <> struct Info<QuestData::ObjKind>
	{
		static constexpr std::string_view kName = "ObjKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Kill", 1 },
			{ "KillIn", 2 },
			{ "Collect", 3 },
			{ "Talk", 4 },
			{ "Explore", 5 },
			{ "Boss", 6 },
			{ "Equip", 7 },
		};
		static constexpr size_t kCount = 8;
	};
	static_assert(static_cast<int>(QuestData::ObjKind::None) == 0);
	static_assert(static_cast<int>(QuestData::ObjKind::Kill) == 1);
	static_assert(static_cast<int>(QuestData::ObjKind::KillIn) == 2);
	static_assert(static_cast<int>(QuestData::ObjKind::Collect) == 3);
	static_assert(static_cast<int>(QuestData::ObjKind::Talk) == 4);
	static_assert(static_cast<int>(QuestData::ObjKind::Explore) == 5);
	static_assert(static_cast<int>(QuestData::ObjKind::Boss) == 6);
	static_assert(static_cast<int>(QuestData::ObjKind::Equip) == 7);

	template <> struct Info<QuestData::QuestIcon>
	{
		static constexpr std::string_view kName = "QuestIcon";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "None", 0 },
			{ "Moss", 1 },
			{ "Straw", 2 },
			{ "Meat", 3 },
			{ "SpiritLight", 4 },
			{ "Sap", 5 },
			{ "Crest", 6 },
			{ "Ember", 7 },
			{ "CarvedStone", 8 },
		};
		static constexpr size_t kCount = 9;
	};
	static_assert(static_cast<int>(QuestData::QuestIcon::None) == 0);
	static_assert(static_cast<int>(QuestData::QuestIcon::Moss) == 1);
	static_assert(static_cast<int>(QuestData::QuestIcon::Straw) == 2);
	static_assert(static_cast<int>(QuestData::QuestIcon::Meat) == 3);
	static_assert(static_cast<int>(QuestData::QuestIcon::SpiritLight) == 4);
	static_assert(static_cast<int>(QuestData::QuestIcon::Sap) == 5);
	static_assert(static_cast<int>(QuestData::QuestIcon::Crest) == 6);
	static_assert(static_cast<int>(QuestData::QuestIcon::Ember) == 7);
	static_assert(static_cast<int>(QuestData::QuestIcon::CarvedStone) == 8);
	static_assert(static_cast<int>(QuestData::QuestIcon::Count) == 9, "QuestData::QuestIcon: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<TownMap::BuildingKind>
	{
		static constexpr std::string_view kName = "BuildingKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eStore", 0 },
			{ "eWeaponShop", 1 },
			{ "eTemple", 2 },
			{ "eInn", 3 },
			{ "eHouse", 4 },
			{ "eArmorer", 5 },
			{ "eTavern", 6 },
			{ "eStarTower", 7 },
			{ "eAuctionHall", 8 },
			{ "eGuildHall", 9 },
			{ "eWarehouse", 10 },
			{ "eStable", 11 },
			{ "eWaterMill", 12 },
			{ "eWindMill", 13 },
			{ "eGuardPost", 14 },
			{ "eScriptorium", 15 },
			{ "eGreenhouse", 16 },
			{ "eWorkshop", 17 },
			{ "eLookout", 18 },
		};
		static constexpr size_t kCount = 19;
	};
	static_assert(static_cast<int>(TownMap::BuildingKind::eStore) == 0);
	static_assert(static_cast<int>(TownMap::BuildingKind::eWeaponShop) == 1);
	static_assert(static_cast<int>(TownMap::BuildingKind::eTemple) == 2);
	static_assert(static_cast<int>(TownMap::BuildingKind::eInn) == 3);
	static_assert(static_cast<int>(TownMap::BuildingKind::eHouse) == 4);
	static_assert(static_cast<int>(TownMap::BuildingKind::eArmorer) == 5);
	static_assert(static_cast<int>(TownMap::BuildingKind::eTavern) == 6);
	static_assert(static_cast<int>(TownMap::BuildingKind::eStarTower) == 7);
	static_assert(static_cast<int>(TownMap::BuildingKind::eAuctionHall) == 8);
	static_assert(static_cast<int>(TownMap::BuildingKind::eGuildHall) == 9);
	static_assert(static_cast<int>(TownMap::BuildingKind::eWarehouse) == 10);
	static_assert(static_cast<int>(TownMap::BuildingKind::eStable) == 11);
	static_assert(static_cast<int>(TownMap::BuildingKind::eWaterMill) == 12);
	static_assert(static_cast<int>(TownMap::BuildingKind::eWindMill) == 13);
	static_assert(static_cast<int>(TownMap::BuildingKind::eGuardPost) == 14);
	static_assert(static_cast<int>(TownMap::BuildingKind::eScriptorium) == 15);
	static_assert(static_cast<int>(TownMap::BuildingKind::eGreenhouse) == 16);
	static_assert(static_cast<int>(TownMap::BuildingKind::eWorkshop) == 17);
	static_assert(static_cast<int>(TownMap::BuildingKind::eLookout) == 18);

	template <> struct Info<TownMap::AmbientKind>
	{
		static constexpr std::string_view kName = "AmbientKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eFountain", 0 },
			{ "eMarket", 1 },
			{ "eSmithy", 2 },
			{ "eBell", 3 },
			{ "eRiver", 4 },
			{ "eWind", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(TownMap::AmbientKind::eFountain) == 0);
	static_assert(static_cast<int>(TownMap::AmbientKind::eMarket) == 1);
	static_assert(static_cast<int>(TownMap::AmbientKind::eSmithy) == 2);
	static_assert(static_cast<int>(TownMap::AmbientKind::eBell) == 3);
	static_assert(static_cast<int>(TownMap::AmbientKind::eRiver) == 4);
	static_assert(static_cast<int>(TownMap::AmbientKind::eWind) == 5);

	template <> struct Info<TownMap::PropKind>
	{
		static constexpr std::string_view kName = "PropKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eGrave", 0 },
			{ "eBoard", 1 },
			{ "eBench", 2 },
			{ "eStatue", 3 },
			{ "eFlagpole", 4 },
			{ "eWell", 5 },
			{ "eCart", 6 },
			{ "eHay", 7 },
			{ "eBarrels", 8 },
			{ "eCrates", 9 },
			{ "eSignpost", 10 },
			{ "eTarget", 11 },
			{ "eRack", 12 },
			{ "eLaundry", 13 },
			{ "eBunting", 14 },
			{ "eLogs", 15 },
		};
		static constexpr size_t kCount = 16;
	};
	static_assert(static_cast<int>(TownMap::PropKind::eGrave) == 0);
	static_assert(static_cast<int>(TownMap::PropKind::eBoard) == 1);
	static_assert(static_cast<int>(TownMap::PropKind::eBench) == 2);
	static_assert(static_cast<int>(TownMap::PropKind::eStatue) == 3);
	static_assert(static_cast<int>(TownMap::PropKind::eFlagpole) == 4);
	static_assert(static_cast<int>(TownMap::PropKind::eWell) == 5);
	static_assert(static_cast<int>(TownMap::PropKind::eCart) == 6);
	static_assert(static_cast<int>(TownMap::PropKind::eHay) == 7);
	static_assert(static_cast<int>(TownMap::PropKind::eBarrels) == 8);
	static_assert(static_cast<int>(TownMap::PropKind::eCrates) == 9);
	static_assert(static_cast<int>(TownMap::PropKind::eSignpost) == 10);
	static_assert(static_cast<int>(TownMap::PropKind::eTarget) == 11);
	static_assert(static_cast<int>(TownMap::PropKind::eRack) == 12);
	static_assert(static_cast<int>(TownMap::PropKind::eLaundry) == 13);
	static_assert(static_cast<int>(TownMap::PropKind::eBunting) == 14);
	static_assert(static_cast<int>(TownMap::PropKind::eLogs) == 15);

	template <> struct Info<GuildMissionKind>
	{
		static constexpr std::string_view kName = "GuildMissionKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Kill", 0 },
			{ "Boss", 1 },
			{ "Collect", 2 },
			{ "Gear", 3 },
			{ "Active", 4 },
			{ "Quest", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(GuildMissionKind::Kill) == 0);
	static_assert(static_cast<int>(GuildMissionKind::Boss) == 1);
	static_assert(static_cast<int>(GuildMissionKind::Collect) == 2);
	static_assert(static_cast<int>(GuildMissionKind::Gear) == 3);
	static_assert(static_cast<int>(GuildMissionKind::Active) == 4);
	static_assert(static_cast<int>(GuildMissionKind::Quest) == 5);

	template <> struct Info<GuildAchKind>
	{
		static constexpr std::string_view kName = "GuildAchKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "Kills", 0 },
			{ "BossKills", 1 },
			{ "Missions", 2 },
			{ "Coop", 3 },
			{ "Days", 4 },
			{ "Attend", 5 },
			{ "Level", 6 },
			{ "Members", 7 },
		};
		static constexpr size_t kCount = 8;
	};
	static_assert(static_cast<int>(GuildAchKind::Kills) == 0);
	static_assert(static_cast<int>(GuildAchKind::BossKills) == 1);
	static_assert(static_cast<int>(GuildAchKind::Missions) == 2);
	static_assert(static_cast<int>(GuildAchKind::Coop) == 3);
	static_assert(static_cast<int>(GuildAchKind::Days) == 4);
	static_assert(static_cast<int>(GuildAchKind::Attend) == 5);
	static_assert(static_cast<int>(GuildAchKind::Level) == 6);
	static_assert(static_cast<int>(GuildAchKind::Members) == 7);

	template <> struct Info<GuildPerm>
	{
		static constexpr std::string_view kName = "GuildPerm";
		static constexpr bool kFlags = true;
		static constexpr Item kItems[] = {
			{ "Invite", 1 },
			{ "Approve", 2 },
			{ "Kick", 4 },
			{ "Notice", 8 },
			{ "Board", 16 },
			{ "Rank", 32 },
			{ "Shop", 64 },
		};
		static constexpr size_t kCount = 7;
	};
	static_assert(static_cast<int>(GuildPerm::Invite) == 1);
	static_assert(static_cast<int>(GuildPerm::Approve) == 2);
	static_assert(static_cast<int>(GuildPerm::Kick) == 4);
	static_assert(static_cast<int>(GuildPerm::Notice) == 8);
	static_assert(static_cast<int>(GuildPerm::Board) == 16);
	static_assert(static_cast<int>(GuildPerm::Rank) == 32);
	static_assert(static_cast<int>(GuildPerm::Shop) == 64);

	template <> struct Info<AchCategory>
	{
		static constexpr std::string_view kName = "AchCategory";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eHunt", 0 },
			{ "eBoss", 1 },
			{ "eGrowth", 2 },
			{ "eGear", 3 },
			{ "eCraft", 4 },
			{ "eWorld", 5 },
		};
		static constexpr size_t kCount = 6;
	};
	static_assert(static_cast<int>(AchCategory::eHunt) == 0);
	static_assert(static_cast<int>(AchCategory::eBoss) == 1);
	static_assert(static_cast<int>(AchCategory::eGrowth) == 2);
	static_assert(static_cast<int>(AchCategory::eGear) == 3);
	static_assert(static_cast<int>(AchCategory::eCraft) == 4);
	static_assert(static_cast<int>(AchCategory::eWorld) == 5);
	static_assert(static_cast<int>(AchCategory::Count) == 6, "AchCategory: 열거자 수가 바뀌었다 — gen을 다시");

	template <> struct Info<AchKind>
	{
		static constexpr std::string_view kName = "AchKind";
		static constexpr bool kFlags = false;
		static constexpr Item kItems[] = {
			{ "eKill", 0 },
			{ "eKillElite", 1 },
			{ "eKillBoss", 2 },
			{ "ePartyKill", 3 },
			{ "eLevel", 4 },
			{ "eDeath", 5 },
			{ "eAdena", 6 },
			{ "eGear", 7 },
			{ "eEnchant", 8 },
			{ "eCraft", 9 },
			{ "eDismantle", 10 },
			{ "eEssence", 11 },
			{ "eQuest", 12 },
			{ "eGuildJoin", 13 },
			{ "eAuctionSell", 14 },
			{ "eAuctionBuy", 15 },
			{ "eTrade", 16 },
			{ "eZone", 17 },
		};
		static constexpr size_t kCount = 18;
	};
	static_assert(static_cast<int>(AchKind::eKill) == 0);
	static_assert(static_cast<int>(AchKind::eKillElite) == 1);
	static_assert(static_cast<int>(AchKind::eKillBoss) == 2);
	static_assert(static_cast<int>(AchKind::ePartyKill) == 3);
	static_assert(static_cast<int>(AchKind::eLevel) == 4);
	static_assert(static_cast<int>(AchKind::eDeath) == 5);
	static_assert(static_cast<int>(AchKind::eAdena) == 6);
	static_assert(static_cast<int>(AchKind::eGear) == 7);
	static_assert(static_cast<int>(AchKind::eEnchant) == 8);
	static_assert(static_cast<int>(AchKind::eCraft) == 9);
	static_assert(static_cast<int>(AchKind::eDismantle) == 10);
	static_assert(static_cast<int>(AchKind::eEssence) == 11);
	static_assert(static_cast<int>(AchKind::eQuest) == 12);
	static_assert(static_cast<int>(AchKind::eGuildJoin) == 13);
	static_assert(static_cast<int>(AchKind::eAuctionSell) == 14);
	static_assert(static_cast<int>(AchKind::eAuctionBuy) == 15);
	static_assert(static_cast<int>(AchKind::eTrade) == 16);
	static_assert(static_cast<int>(AchKind::eZone) == 17);
	static_assert(static_cast<int>(AchKind::Count) == 18, "AchKind: 열거자 수가 바뀌었다 — gen을 다시");

}
