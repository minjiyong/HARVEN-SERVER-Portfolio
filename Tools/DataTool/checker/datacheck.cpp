// ─────────────────────────────────────────────────────────────────────────────
// datacheck — 서버와 같은 로더(Server/HARVEN_GameServer/Database/GameData/Load)를 리눅스 g++로 빌드한 검사기.
//   datacheck <GameData 폴더>                 읽기 + 검증(형 · 열 · 규칙 R번호). 오류는 "위치: 이유 [R번호]" 줄로
//   datacheck <GameData 폴더> --dump <출력>   통과하면 메모리 스냅샷을 정본 JSON으로(3자 일치의 셋째 — 서버 --dump-data와 같은 코드)
//   datacheck <GameData 폴더> --reload <다른 폴더>  첫 폴더를 올린 뒤 둘째 폴더로 핫 리로드(값만 바뀐 판만 받는지 — Phase 4)
//   datacheck <GameData 폴더> --generations N  N세대를 올려(핫 리로드처럼 해제하지 않는다) 세대 하나가 붙잡는 힙 바이트를 잰다(Phase 4 메모리 게이트)
// datatool check · golden · D2 시험이 부른다. 서버 실행 파일의 --check-data · --dump-data도 같은 일을 한다(HARVEN_DATACHECK).
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "Database/GameData/Load/GameData.h"

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "사용법: datacheck <GameData 폴더> [--dump <출력 폴더>]\n";
		return 2;
	}
	const std::string dir = argv[1];
	std::string dumpDir;
	int generations = 0;
	std::string reloadDir;
	for (int i = 2; i + 1 < argc; ++i)
	{
		if (std::strcmp(argv[i], "--dump") == 0) dumpDir = argv[i + 1];
		if (std::strcmp(argv[i], "--generations") == 0) generations = std::atoi(argv[i + 1]);
		if (std::strcmp(argv[i], "--reload") == 0) reloadDir = argv[i + 1];
	}
	if (!reloadDir.empty())
	{
		const GameData::LoadResult first = GameData::LoadFromDir(dir);
		if (!first.ok) { std::cout << GameData::Describe(first) << "\n"; return 1; }
		const GameData::ReloadResult r = GameData::Reload(reloadDir);
		std::cout << GameData::DescribeReload(r) << "\n";
		std::cout << "[reload] " << (r.published ? "published" : "rejected") << " generations=" << GameData::Generations() << "\n";
		return r.published ? 0 : 3;
	}
#if defined(__GLIBC__)
	if (generations > 0)
	{
		// 세대 하나 = 올린 스냅샷이 붙잡는 힙(읽기용 JSON · 파일 내용은 Build가 끝나면 풀린다). 첫 세대는 정적 초기화와 섞이므로 2세대부터 잰다.
		if (!GameData::LoadFromDir(dir).ok) return 1;
		const size_t a = mallinfo2().uordblks;
		for (int g = 0; g < generations; ++g)
			if (!GameData::LoadFromDir(dir).ok) return 1;
		const size_t b = mallinfo2().uordblks;
		std::printf("[generations] %d세대 · 세대당 힙 %.1f KB (전체 %.1f KB)\n", generations, (b - a) / 1024.0 / generations, (b - a) / 1024.0);
		return 0;
	}
#endif

	const GameData::LoadResult r = dumpDir.empty() ? GameData::CheckDir(dir) : GameData::LoadFromDir(dir);
	for (const auto& e : r.errors)
		std::cout << "ERROR\t" << e.where << "\t" << e.what << "\t" << e.rule << "\n";
	std::cout << GameData::Describe(r, 0) << "\n";
	if (!r.ok)
		return 1;
	if (!dumpDir.empty())
	{
		std::string err;
		if (!GameData::DumpToDir(dumpDir, err))
		{
			std::cout << "ERROR\t--dump\t" << err << "\t\n";
			return 1;
		}
	}
	return 0;
}
