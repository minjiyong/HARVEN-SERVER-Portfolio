#pragma once
// 생성물 — datatool export(시트 Monster 탭의 code_name 열). 손으로 고치지 않는다.
// 코드가 이름으로 부르는 id만 여기 온다. id는 영구(Bin/GameData/ids.lock) — 이름을 바꾸면 이 헤더의 이름도 바뀐다.
#include <cstdint>

namespace MonsterTable::Id
{
	inline constexpr uint16_t Moroz = 29;   // 모로즈 — 벨고라의 혹한
	inline constexpr uint16_t Urgan = 30;   // 우르간 — 그락 대족장
	inline constexpr uint16_t Severin = 31;   // 세베린 — 몰락한 기사단장
	inline constexpr uint16_t BurrowQueen = 32;   // 굴개미 여왕 — 둔덕의 어미
	inline constexpr uint16_t Zmaj = 33;   // 즈마이 — 바트라의 늙은 용
	inline constexpr uint16_t Gramok = 34;   // 그라목 — 삼키는 주인
	inline constexpr uint16_t TrainingDummy = 35;   // 허수아비
}
