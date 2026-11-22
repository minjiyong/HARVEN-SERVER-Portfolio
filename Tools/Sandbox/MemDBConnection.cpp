// ─────────────────────────────────────────────────────────────────────────────
// 샌드박스용 인메모리 DB — Libs/Database/DBConnection.cpp 대신 링크한다.
//
// 목적: RDS + ODBC가 없는 곳(리눅스 샌드박스에서 MinGW로 교차 빌드한 서버를 Wine으로
// 띄울 때)에서도 로그인 · 인벤토리 · 그로시 · 상점 · 진행 저장 · 결사 · 경매가 **같은 의미**로 돌게 한다.
// Shared/Database/mysql/setup.sql의 프로시저(P_USER_* · P_INVENTORY_* · …)를 한 줄씩 옮겼다(인자 · 반환 모양 · 성공/실패가 같다).
//
//  · 캐릭터는 UserID(playerId)로 둔다. 이름 색인은 ASCII 대소문자를 가리지 않는다 — MySQL utf8mb4_0900_ai_ci의
//    UQ_User_Name처럼 "FOO"와 "foo"는 같은 이름이다(악센트 · 한글 정규화까지 흉내 내지는 않는다).
//  · P_USER_CREATE처럼 AddUserInfoInDataBase의 created는 이 호출이 행을 넣었을 때만 true다.
//
//  · 모든 테이블은 프로세스 메모리 안의 맵이고, 한 번에 한 호출만(전역 뮤텍스) 처리한다.
//    프로시저 하나 = 락 한 번 = 트랜잭션 하나(중간 상태가 다른 호출에 보이지 않는다).
//  · **커밋 일지(Phase 5 장애 시험)**: HARVEN_MEMDB_JOURNAL=경로 이면 바꾸는 호출마다 한 줄(적용한 뒤, 락 안에서 쓰고 flush)을
//    붙이고, 켤 때 그 파일을 처음부터 다시 적용한다. 줄 하나 = 커밋 하나 — 끝이 잘린 줄(쓰다가 죽음)은 버린다.
//    그래서 프로세스를 죽였다 켜면 "커밋된 것만 남는" DB처럼 동작한다. 일지가 없으면 예전처럼 켤 때마다 비어 있다.
//  · HARVEN_DB_BUY_DELAY_MS=N 이면 AuctionBuy(P_AUCTION_BUY)가 트랜잭션 안(적용 전)에서 N ms 멈춘다 — 그 사이 서버를 죽이는 장애 시험용.
//  · HARVEN_DB_BATCH_DELAY_MS=N 이면 SaveItemBatch(P_ITEM_BATCH_TRADE — 제작 · 분해)가 N ms 멈춘다 — 그동안의 Busy 시험용(regress_craft.py).
//  · 이름에 "_SF"가 있는 캐릭터는 SaveItemBatch · SaveEnchant가 늘 실패한다(DB 반영 실패 → StorageError · 연결 닫기 시험).
//  · 실제 DB 동작 확인은 "실기 확인 필요". MSVC 빌드(GameServer.vcxproj)는 이 파일을 쓰지 않는다 — Tools/Sandbox/build_server.sh 전용.
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include "Database/DBConnection.h"
#include "Database/TransferSave.h"
#include "Database/GameData/WorldData.h"
#include "Database/GameData/GuildAuction.h"
#include "Database/GameData/ItemRules.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
	struct PlayerRow
	{
		int         playerId = 0;
		std::string name;              // 처음 만든 그대로(돌려줄 때 쓰는 이름)
		std::string password;
		int         x = 0, y = 0;
		uint8_t     level = 1;
		uint32_t    exp = 0;
		uint32_t    adena = 0;
		int16_t     alignment = 0;
		uint16_t    pkCount = 0;
	};

	struct InvRow
	{
		uint16_t itemId = 0;
		uint16_t count = 0;
		bool     equipped = false;
		uint32_t  enchant = 0;
		uint64_t extra = 0;
	};

	struct GuildRowM { std::string name, nameKey, notice; uint8_t emblem = 0; int leaderId = 0; int64_t createdAt = 0; };
	struct GuildGrowM { uint32_t seals = 0; std::vector<uint8_t> data; };   // GuildProgress
	struct MemberRowM { int guildId = 0; uint8_t rank = 3; int64_t lastSeen = 0; };

	struct MemDb
	{
		std::mutex lock;
		std::map<int, PlayerRow> players;                            // UserID → 행 (User)
		std::map<std::string, int> idByName;                         // NameKey(name) → UserID (UQ_User_Name)
		std::map<int, std::map<uint16_t, InvRow>> inventory;         // UserID → slot → 행 (UserInventory)
        std::map<std::string, std::string> transfers; // receipt → 불변 거래 내역(JSON)
		std::map<std::pair<uint16_t, uint32_t>, std::string> enchantFirst;
        std::map<int, std::map<std::pair<uint16_t, uint32_t>, uint16_t>> enchantGrace;
		std::map<int, std::vector<uint8_t>> progress;                // UserProgress
		std::map<int, std::map<uint16_t, DB_SKILL_INFO>> skills;     // UserSkill(UserID → SkillID → 행)
		int nextPlayerId = 1;
		// Phase 5
		std::map<int, GuildRowM> guilds;                             // Guild
		std::map<int, MemberRowM> members;                           // GuildMember(UserID → 행)
		std::map<int64_t, DB_LISTING_ROW> listings;                  // AuctionListing(모든 상태)
		std::map<int64_t, DB_SETTLEMENT_ROW> settlements;            // AuctionSettlement(받은 행은 claimedAt으로 표시 → 지움)
		std::map<int64_t, int64_t> claimedAt;                        // settlementId → 받은 시각
		std::vector<DB_PRICE_ROW> prices;                            // AuctionPriceHistory
		// 결사 성장
		std::map<int, GuildGrowM> guildGrow;                         // GuildProgress
		std::map<int, std::vector<DB_GUILD_LOG_ROW>> guildLogs;      // GuildLog(결사마다 넣은 차례)
		std::map<int64_t, DB_GUILD_POST_ROW> guildPosts;             // GuildPost
		// 일지
		FILE* journal = nullptr;
		bool replaying = false;
	};

	MemDb& DbRaw()
	{
		static MemDb db;
		return db;
	}

	// ── 일지 한 줄: 필드를 '|'로 잇는다. 문자열은 16진으로(구분자 · 줄바꿈과 겹치지 않게) ──
	struct Rec
	{
		std::ostringstream o;
		explicit Rec(const char* op) { o << op; }
		Rec& operator<<(long long v) { o << '|' << v; return *this; }
		Rec& s(const std::string& str)
		{
			static const char* kHex = "0123456789abcdef";
			o << '|';
			if (str.empty()) o << '-';
			for (unsigned char c : str) o << kHex[c >> 4] << kHex[c & 15];
			return *this;
		}
		Rec& u64(uint64_t v) { o << '|' << v; return *this; }
	};

	void Journal(MemDb& db, const Rec& r)
	{
		if (db.replaying || db.journal == nullptr) return;
		const std::string line = r.o.str() + "\n";
		std::fwrite(line.data(), 1, line.size(), db.journal);
		std::fflush(db.journal);   // 프로세스가 죽어도 OS에 넘어간 줄은 남는다(전원 차단은 시험하지 않는다)
	}

	// 슬롯 한 칸(upsert 또는 삭제)
	void ApplySlot(MemDb& db, int playerId, const DB_ITEM_SLOT_SAVE& s)
	{
		auto& inv = db.inventory[playerId];
		if (!s.hasItem || s.itemId == 0 || s.count == 0)
		{
			inv.erase(s.slotIndex);
			return;
		}
		InvRow& r = inv[s.slotIndex];
		r.itemId = s.itemId;
		r.count = s.count;
		r.equipped = s.equipped;
		r.enchant = s.enchant;
		r.extra = s.extra & 0x3FFFFFFFFFFFFFFFull;
	}
	void RecSlot(Rec& r, const DB_ITEM_SLOT_SAVE& s)
	{
		r << s.slotIndex << (s.hasItem ? 1 : 0) << s.itemId << s.count << (s.equipped ? 1 : 0) << s.enchant;
		r.u64(s.extra);
	}
	PlayerRow* PlayerById(MemDb& db, int playerId)
	{
		const auto it = db.players.find(playerId);
		return it == db.players.end() ? nullptr : &it->second;
	}
	// 이름 색인 키: ASCII 대문자만 소문자로(utf8mb4_0900_ai_ci에서 "FOO" = "foo"). UTF-8의 다른 바이트는 그대로.
	std::string NameKey(const std::string& name)
	{
		std::string key = name;
		for (char& c : key)
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		return key;
	}
	PlayerRow* PlayerByName(MemDb& db, const std::string& name)
	{
		const auto it = db.idByName.find(NameKey(name));
		return it == db.idByName.end() ? nullptr : PlayerById(db, it->second);
	}
	PlayerRow* InsertPlayer(MemDb& db, const std::string& name, PlayerRow row)
	{
		row.name = name;
		db.idByName[NameKey(name)] = row.playerId;
		db.nextPlayerId = std::max(db.nextPlayerId, row.playerId + 1);
		PlayerRow& stored = db.players[row.playerId];
		stored = std::move(row);
		return &stored;
	}

	// ── Phase 5 적용 함수(살아 있는 호출과 일지 다시 적용이 같은 함수를 쓴다) ──
	bool ApplyGuildCreate(MemDb& db, int pid, uint32_t adena, int gid, const std::string& name, const std::string& key, uint8_t emblem, int64_t createdAt)
	{
		if (db.guilds.count(gid) || db.members.count(pid)) return false;
		for (const auto& [id, g] : db.guilds) if (g.nameKey == key) return false;   // UQ_Guilds_NameKey
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr) return false;
		p->adena = adena;
		db.guilds[gid] = GuildRowM{ name, key, "", emblem, pid, createdAt };
		db.members[pid] = MemberRowM{ gid, static_cast<uint8_t>(GuildRule::Rank::Leader), 0 };
		return true;
	}
	bool ApplyMemberSet(MemDb& db, int gid, int pid, uint8_t rank)
	{
		if (!db.guilds.count(gid)) return false;
		auto it = db.members.find(pid);
		if (it != db.members.end() && it->second.guildId != gid) return false;   // 다른 결사
		if (it == db.members.end()) db.members[pid] = MemberRowM{ gid, rank, 0 };
		else it->second.rank = rank;
		return true;
	}
	bool ApplyMemberRemove(MemDb& db, int gid, int pid)
	{
		auto it = db.members.find(pid);
		if (it == db.members.end() || it->second.guildId != gid) return false;
		db.members.erase(it);
		return true;
	}
	bool ApplyTransfer(MemDb& db, int gid, int from, int to)
	{
		auto g = db.guilds.find(gid);
		auto a = db.members.find(from), b = db.members.find(to);
		if (g == db.guilds.end() || a == db.members.end() || b == db.members.end() || a->second.guildId != gid || b->second.guildId != gid) return false;
		a->second.rank = static_cast<uint8_t>(GuildRule::Rank::Officer);
		b->second.rank = static_cast<uint8_t>(GuildRule::Rank::Leader);
		g->second.leaderId = to;
		return true;
	}
	bool ApplySetInfo(MemDb& db, int gid, uint8_t emblem, const std::string& notice)
	{
		auto g = db.guilds.find(gid);
		if (g == db.guilds.end()) return false;
		g->second.emblem = emblem;
		g->second.notice = notice;
		return true;
	}
	bool ApplyDisband(MemDb& db, int gid)
	{
		if (!db.guilds.erase(gid)) return false;
		for (auto it = db.members.begin(); it != db.members.end();)
			it = it->second.guildId == gid ? db.members.erase(it) : std::next(it);
		db.guildGrow.erase(gid);   // ON DELETE CASCADE(GuildProgress · GuildLog · GuildPost)
		db.guildLogs.erase(gid);
		for (auto it = db.guildPosts.begin(); it != db.guildPosts.end();)
			it = it->second.guildId == gid ? db.guildPosts.erase(it) : std::next(it);
		return true;
	}
	// ── 결사 성장(setup.sql P_GUILD_* — 같은 의미) ──
	void ApplyGuildLog(MemDb& db, const DB_GUILD_LOG_ROW& log)
	{
		if (db.guilds.count(log.guildId)) db.guildLogs[log.guildId].push_back(log);
	}
	bool ApplyProgressSave(MemDb& db, int gid, const std::string& data)
	{
		if (!db.guilds.count(gid)) return false;
		db.guildGrow[gid].data.assign(data.begin(), data.end());
		return true;
	}
	bool ApplySealsAdd(MemDb& db, int gid, int32_t delta, const DB_GUILD_LOG_ROW& log)
	{
		if (!db.guilds.count(gid)) return false;
		GuildGrowM& g = db.guildGrow[gid];
		if (delta < 0 && g.seals < static_cast<uint32_t>(-static_cast<int64_t>(delta))) return false;
		g.seals = static_cast<uint32_t>(static_cast<int64_t>(g.seals) + delta);
		ApplyGuildLog(db, log);
		return true;
	}
	bool ApplyShopBuy(MemDb& db, int pid, int gid, uint32_t price, const DB_ITEM_SLOT_SAVE& slot, const DB_GUILD_LOG_ROW& log)
	{
		const auto m = db.members.find(pid);
		if (m == db.members.end() || m->second.guildId != gid) return false;
		const auto g = db.guildGrow.find(gid);
		if (g == db.guildGrow.end() || g->second.seals < price) return false;
		g->second.seals -= price;
		ApplySlot(db, pid, slot);
		ApplyGuildLog(db, log);
		return true;
	}
	bool ApplyPostAdd(MemDb& db, const DB_GUILD_POST_ROW& post, int64_t drop)
	{
		const auto m = db.members.find(post.authorId);
		if (m == db.members.end() || m->second.guildId != post.guildId || db.guildPosts.count(post.postId)) return false;
		if (drop != 0)
			if (const auto it = db.guildPosts.find(drop); it != db.guildPosts.end() && it->second.guildId == post.guildId) db.guildPosts.erase(it);
		db.guildPosts[post.postId] = post;
		return true;
	}
	bool ApplyPostDelete(MemDb& db, int gid, int64_t postId)
	{
		const auto it = db.guildPosts.find(postId);
		if (it == db.guildPosts.end() || it->second.guildId != gid) return false;
		db.guildPosts.erase(it);
		return true;
	}
	bool ApplyPostPin(MemDb& db, int gid, int64_t postId, bool pinned)
	{
		const auto it = db.guildPosts.find(postId);
		if (it == db.guildPosts.end() || it->second.guildId != gid) return false;
		it->second.pinned = pinned ? 1 : 0;
		return true;
	}
	void RecLog(Rec& r, const DB_GUILD_LOG_ROW& l)
	{
		r << l.guildId << static_cast<long long>(l.at) << l.kind << l.value;
		r.s(l.who).s(l.what);
	}
	bool ApplyRegister(MemDb& db, int pid, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& l)
	{
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr || db.listings.count(l.listingId)) return false;
		p->adena = adena;
		ApplySlot(db, pid, slot);
		DB_LISTING_ROW row = l;
		row.status = 0;
		db.listings[l.listingId] = row;
		return true;
	}
	// extraOr: 구매자 정산 줄 extra에 더하는 비트(산 장비의 거래 이력 bit 9 — P_AUCTION_BUY in_SettleExtraOr와 같다)
	bool ApplyBuy(MemDb& db, int pid, uint32_t adena, int64_t lid, uint32_t price, int64_t bs, int64_t ss, uint32_t gets, int64_t now, uint64_t extraOr)
	{
		auto it = db.listings.find(lid);
		if (it == db.listings.end() || it->second.status != 0 || it->second.price != price) return false;   // 판매 중일 때만
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr || db.settlements.count(bs) || db.settlements.count(ss)) return false;
		DB_LISTING_ROW& l = it->second;
		l.status = 1;
		p->adena = adena;
		DB_SETTLEMENT_ROW item{};
		item.settlementId = bs; item.playerId = pid; item.kind = 2; item.itemId = l.itemId; item.count = l.count; item.enchant = l.enchant; item.extra = l.extra | extraOr;
		item.listingId = lid; item.reason = static_cast<uint8_t>(AuctionSettleReason::Bought); item.createdAt = now;
		DB_SETTLEMENT_ROW money{};
		money.settlementId = ss; money.playerId = l.sellerId; money.kind = 1; money.adena = gets; money.listingId = lid;
		money.reason = static_cast<uint8_t>(AuctionSettleReason::Sold); money.createdAt = now;
		db.settlements[bs] = item;
		db.settlements[ss] = money;
		db.prices.push_back(DB_PRICE_ROW{ l.itemId, l.count ? price / l.count : price, now });
		return true;
	}
	bool ApplyClose(MemDb& db, int64_t lid, uint8_t status, int64_t sid, int64_t now)
	{
		auto it = db.listings.find(lid);
		if (it == db.listings.end() || it->second.status != 0 || (status != 2 && status != 3) || db.settlements.count(sid)) return false;
		DB_LISTING_ROW& l = it->second;
		l.status = status;
		DB_SETTLEMENT_ROW item{};
		item.settlementId = sid; item.playerId = l.sellerId; item.kind = 2; item.itemId = l.itemId; item.count = l.count; item.enchant = l.enchant; item.extra = l.extra;
		item.listingId = lid; item.createdAt = now;
		item.reason = static_cast<uint8_t>(status == 2 ? AuctionSettleReason::Cancelled : AuctionSettleReason::Expired);
		db.settlements[sid] = item;
		return true;
	}
	bool ApplyClaim(MemDb& db, int pid, int64_t sid, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot, int64_t now)
	{
		auto it = db.settlements.find(sid);
		if (it == db.settlements.end() || it->second.playerId != pid || db.claimedAt.count(sid)) return false;   // 받지 않은 내 행만
		PlayerRow* p = PlayerById(db, pid);
		if (p == nullptr) return false;
		p->adena = adena;
		if (it->second.kind == 2) ApplySlot(db, pid, slot);
		db.claimedAt[sid] = now;
		return true;
	}

	// ── 경제 1차 제작 · 분해(P_ITEM_BATCH_TRADE와 같은 의미) ──
	// 그로시가 바뀌면 지금 값이 adenaBefore여야, 칸마다 지금 행이 before와 같아야(빈 칸이면 행이 없어야) — 하나라도 어긋나면 아무것도 바꾸지 않는다.
	bool BatchMatches(MemDb& db, int pid, const DB_ITEM_BATCH_SAVE& save)
	{
		if (save.before.empty() || save.before.size() != save.after.size()) return false;
		if (save.adenaBefore != save.adenaAfter)
		{
			const PlayerRow* p = PlayerById(db, pid);
			if (p == nullptr || p->adena != save.adenaBefore) return false;
		}
		const auto inv = db.inventory.find(pid);
		for (size_t i = 0; i < save.before.size(); ++i)
		{
			const DB_ITEM_SLOT_SAVE& b = save.before[i];
			if (save.after[i].slotIndex != b.slotIndex) return false;
			const InvRow* row = nullptr;
			if (inv != db.inventory.end())
				if (const auto it = inv->second.find(b.slotIndex); it != inv->second.end()) row = &it->second;
			if (!b.hasItem) { if (row != nullptr) return false; continue; }
			if (row == nullptr || row->itemId != b.itemId || row->count != b.count || row->equipped != b.equipped
				|| row->enchant != b.enchant || row->extra != (b.extra & 0x3FFFFFFFFFFFFFFFull)) return false;
		}
		return true;
	}
	void ApplyItemBatch(MemDb& db, int pid, uint32_t adenaBefore, uint32_t adenaAfter, const std::vector<DB_ITEM_SLOT_SAVE>& after)
	{
		if (adenaBefore != adenaAfter)
			if (PlayerRow* p = PlayerById(db, pid)) p->adena = adenaAfter;
		for (const DB_ITEM_SLOT_SAVE& a : after) ApplySlot(db, pid, a);
	}

	// ── 일지 다시 적용 ──
	std::vector<std::string> Split(const std::string& line)
	{
		std::vector<std::string> f;
		size_t a = 0;
		for (size_t i = 0; i <= line.size(); ++i)
			if (i == line.size() || line[i] == '|') { f.push_back(line.substr(a, i - a)); a = i + 1; }
		return f;
	}
	std::string Unhex(const std::string& h)
	{
		if (h == "-") return {};
		std::string out;
		for (size_t i = 0; i + 1 < h.size(); i += 2)
		{
			auto v = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
			out.push_back(static_cast<char>((v(h[i]) << 4) | v(h[i + 1])));
		}
		return out;
	}
	struct F
	{
		const std::vector<std::string>& f;
		size_t i = 1;
		long long n() { return i < f.size() ? std::atoll(f[i++].c_str()) : 0; }
		uint64_t u() { return i < f.size() ? std::strtoull(f[i++].c_str(), nullptr, 10) : 0; }
		std::string s() { return i < f.size() ? Unhex(f[i++]) : std::string(); }
		DB_ITEM_SLOT_SAVE slot()
		{
			DB_ITEM_SLOT_SAVE sl;
			sl.slotIndex = static_cast<uint16_t>(n()); sl.hasItem = n() != 0; sl.itemId = static_cast<uint16_t>(n()); sl.count = static_cast<uint16_t>(n());
			sl.equipped = n() != 0; sl.enchant = static_cast<uint32_t>(n()); sl.extra = u();
			return sl;
		}
	};

	void ReplayLine(MemDb& db, const std::string& line)
	{
		const auto f = Split(line);
		if (f.empty()) return;
		F r{ f };
		const std::string& op = f[0];
		if (op == "P")
		{
			PlayerRow row;
			const std::string name = r.s();
			row.password = r.s();
			row.playerId = static_cast<int>(r.n()); row.x = static_cast<int>(r.n()); row.y = static_cast<int>(r.n());
			row.level = static_cast<uint8_t>(r.n()); row.exp = static_cast<uint32_t>(r.n()); row.adena = static_cast<uint32_t>(r.n());
			InsertPlayer(db, name, row);
		}
		else if (op == "U" || op == "UI")   // U = 예전 일지(이름으로 찾음) · UI = UserID
		{
			PlayerRow* p = op == "U" ? PlayerByName(db, r.s()) : PlayerById(db, static_cast<int>(r.n()));
			const int x = static_cast<int>(r.n()), y = static_cast<int>(r.n());
			const auto level = static_cast<uint8_t>(r.n());
			const auto exp = static_cast<uint32_t>(r.n());
			if (p != nullptr) { p->x = x; p->y = y; p->level = level; p->exp = exp; }
		}
		else if (op == "A") { const int pid = static_cast<int>(r.n()); if (auto* p = PlayerById(db, pid)) p->adena = static_cast<uint32_t>(r.n()); }
		else if (op == "L") { const int pid = static_cast<int>(r.n()); if (auto* p = PlayerById(db, pid)) { p->alignment = static_cast<int16_t>(r.n()); p->pkCount = static_cast<uint16_t>(r.n()); } }
		else if (op == "S") { const int pid = static_cast<int>(r.n()); ApplySlot(db, pid, r.slot()); }
		else if (op == "E" || op == "EG")
        {
            const int pid = static_cast<int>(r.n()); const auto count = static_cast<uint8_t>(r.n());
            for (uint8_t i = 0; i < count; ++i) ApplySlot(db, pid, r.slot());
            const auto item = static_cast<uint16_t>(r.n()); const auto level = static_cast<uint32_t>(r.n()); const auto name = r.s();
            if (level >= EnchantRule::FirstNoticeLevel()) db.enchantFirst.emplace(std::make_pair(item, level), name);
            if (op == "EG")
            {
                const auto current = static_cast<uint32_t>(r.n()); const auto failures = static_cast<uint16_t>(r.n());
                if (EnchantRule::GraceAttempts(current) != 0) db.enchantGrace[pid][{item, current}] = failures;
            }
        }
        else if (op == "T") { const int pid = static_cast<int>(r.n()); const auto a = r.slot(); const auto b = r.slot(); ApplySlot(db, pid, a); ApplySlot(db, pid, b); }
		else if (op == "X") { const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n()); if (auto* p = PlayerById(db, pid)) p->adena = adena; ApplySlot(db, pid, r.slot()); }
		else if (op == "G")
		{
			const int pid = static_cast<int>(r.n());
			const std::string data = r.s();
			db.progress[pid] = std::vector<uint8_t>(data.begin(), data.end());
		}
		else if (op == "K")
		{
			const int pid = static_cast<int>(r.n());
			DB_SKILL_INFO sk{};
			sk.m_SkillId = static_cast<uint16_t>(r.n()); sk.m_Level = static_cast<uint8_t>(r.n()); sk.m_Pick1 = static_cast<uint8_t>(r.n()); sk.m_Pick2 = static_cast<uint8_t>(r.n());
			if (sk.m_Level <= 1 && sk.m_Pick1 == 0 && sk.m_Pick2 == 0) db.skills[pid].erase(sk.m_SkillId); else db.skills[pid][sk.m_SkillId] = sk;
		}
        else if (op == "PT")
        {
            const auto receipt = r.s(), data = r.s();
            for (int side = 0; side < 2; ++side)
            {
                const int pid = static_cast<int>(r.n()); const auto adena = static_cast<UInt32>(r.n()); const int n = static_cast<int>(r.n());
                if (auto* p = PlayerById(db, pid)) p->adena = adena;
                for (int i = 0; i < n; ++i) ApplySlot(db, pid, r.slot());
            }
            db.transfers[receipt] = data;
        }
		else if (op == "R") db.skills.erase(static_cast<int>(r.n()));
		else if (op == "GC")
		{
			const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n()); const int gid = static_cast<int>(r.n());
			const std::string name = r.s(), key = r.s(); const auto emblem = static_cast<uint8_t>(r.n());
			ApplyGuildCreate(db, pid, adena, gid, name, key, emblem, r.n());   // 끝 칸 CreatedAt(예전 일지는 0)
		}
		else if (op == "GM") { const int gid = static_cast<int>(r.n()); const int pid = static_cast<int>(r.n()); ApplyMemberSet(db, gid, pid, static_cast<uint8_t>(r.n())); }
		else if (op == "GR") { const int gid = static_cast<int>(r.n()); ApplyMemberRemove(db, gid, static_cast<int>(r.n())); }
		else if (op == "GT") { const int gid = static_cast<int>(r.n()); const int a = static_cast<int>(r.n()); ApplyTransfer(db, gid, a, static_cast<int>(r.n())); }
		else if (op == "GI") { const int gid = static_cast<int>(r.n()); const auto em = static_cast<uint8_t>(r.n()); ApplySetInfo(db, gid, em, r.s()); }
		else if (op == "GD") ApplyDisband(db, static_cast<int>(r.n()));
		else if (op == "GS") { const int pid = static_cast<int>(r.n()); auto it = db.members.find(pid); if (it != db.members.end()) it->second.lastSeen = r.n(); }
		else if (op == "AR")
		{
			const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n());
			const auto slot = r.slot();
			DB_LISTING_ROW l{};
			l.listingId = r.n(); l.sellerId = pid; l.itemId = static_cast<uint16_t>(r.n()); l.count = static_cast<uint16_t>(r.n()); l.enchant = static_cast<uint32_t>(r.n()); l.extra = r.u();
			l.price = static_cast<uint32_t>(r.n()); l.deposit = static_cast<uint32_t>(r.n()); l.createdAt = r.n(); l.expiresAt = r.n();
			ApplyRegister(db, pid, adena, slot, l);
		}
		else if (op == "AB")
		{
			const int pid = static_cast<int>(r.n()); const auto adena = static_cast<uint32_t>(r.n()); const int64_t lid = r.n(); const auto price = static_cast<uint32_t>(r.n());
			const int64_t bs = r.n(), ss = r.n(); const auto gets = static_cast<uint32_t>(r.n()); const int64_t now = r.n();
			const uint64_t extraOr = r.u();   // 경제 1차에서 붙은 칸 — 옛 일지는 0
			ApplyBuy(db, pid, adena, lid, price, bs, ss, gets, now, extraOr);
		}
		else if (op == "AC") { const int64_t lid = r.n(); const auto st = static_cast<uint8_t>(r.n()); const int64_t sid = r.n(); ApplyClose(db, lid, st, sid, r.n()); }
		else if (op == "GP") { const int gid = static_cast<int>(r.n()); ApplyProgressSave(db, gid, r.s()); }
		else if (op == "GA" || op == "GL" || op == "GB")
		{
			int pid = 0, gid = 0; int32_t delta = 0; uint32_t price = 0; DB_ITEM_SLOT_SAVE slot{};
			if (op == "GB") { pid = static_cast<int>(r.n()); gid = static_cast<int>(r.n()); price = static_cast<uint32_t>(r.n()); slot = r.slot(); }
			else if (op == "GA") { gid = static_cast<int>(r.n()); delta = static_cast<int32_t>(r.n()); }
			DB_GUILD_LOG_ROW l;
			l.guildId = static_cast<int>(r.n()); l.at = r.n(); l.kind = static_cast<uint8_t>(r.n()); l.value = static_cast<int32_t>(r.n());
			l.who = r.s(); l.what = r.s();
			if (op == "GA") ApplySealsAdd(db, gid, delta, l);
			else if (op == "GB") ApplyShopBuy(db, pid, gid, price, slot, l);
			else ApplyGuildLog(db, l);
		}
		else if (op == "PA")
		{
			DB_GUILD_POST_ROW p;
			p.postId = r.n(); p.guildId = static_cast<int>(r.n()); p.authorId = static_cast<int>(r.n()); p.author = r.s();
			p.category = static_cast<uint8_t>(r.n()); p.pinned = static_cast<uint8_t>(r.n()); p.title = r.s(); p.body = r.s(); p.at = r.n();
			ApplyPostAdd(db, p, r.n());
		}
		else if (op == "PD") { const int gid = static_cast<int>(r.n()); ApplyPostDelete(db, gid, r.n()); }
		else if (op == "PP") { const int gid = static_cast<int>(r.n()); const int64_t pid = r.n(); ApplyPostPin(db, gid, pid, r.n() != 0); }
		else if (op == "IB")   // 제작 · 분해(P_ITEM_BATCH_TRADE) — 커밋된 것만 적혔으므로 검사 없이 바뀐 뒤를 넣는다
		{
			const int pid = static_cast<int>(r.n());
			const auto before = static_cast<uint32_t>(r.n()), after = static_cast<uint32_t>(r.n());
			const auto count = static_cast<size_t>(r.n());
			std::vector<DB_ITEM_SLOT_SAVE> slots;
			for (size_t i = 0; i < count && i < static_cast<size_t>(MAX_INVENTORY_SLOTS); ++i) slots.push_back(r.slot());
			ApplyItemBatch(db, pid, before, after, slots);
		}
		else if (op == "SC")
		{
			const int pid = static_cast<int>(r.n()); const int64_t sid = r.n(); const auto adena = static_cast<uint32_t>(r.n());
			const auto slot = r.slot();
			ApplyClaim(db, pid, sid, adena, slot, r.n());
		}
	}

	MemDb& Db()
	{
		MemDb& db = DbRaw();
		static std::once_flag once;
		std::call_once(once, [&db]
		{
			const char* path = std::getenv("HARVEN_MEMDB_JOURNAL");
			if (path == nullptr || *path == '\0') return;
			int applied = 0, dropped = 0;
			if (FILE* in = std::fopen(path, "rb"))
			{
				db.replaying = true;
				std::string cur;
				int c;
				while ((c = std::fgetc(in)) != EOF)
				{
					if (c == '\n') { ReplayLine(db, cur); ++applied; cur.clear(); }
					else cur.push_back(static_cast<char>(c));
				}
				if (!cur.empty()) ++dropped;   // 끝이 잘린 줄 = 커밋되지 않은 트랜잭션
				db.replaying = false;
				std::fclose(in);
			}
			db.journal = std::fopen(path, "ab");
			std::cout << "[MemDB] journal " << path << " replayed=" << applied << " dropped=" << dropped << "\n" << std::flush;
		});
		return db;
	}

	int64_t NowUnix() { return static_cast<int64_t>(std::time(nullptr)); }
}

CDBConnection::~CDBConnection() { Clear(); }

bool CDBConnection::Connect() { (void)Db(); return true; }
bool CDBConnection::EnsureConnected() { return true; }
void CDBConnection::Clear() {}
bool CDBConnection::Execute(const wchar_t*) { return true; }
CDBConnection::FetchResult CDBConnection::FetchRow() { return FetchResult::NoData; }
bool CDBConnection::Fetch() { return false; }
int CDBConnection::GetRowCount() { return 0; }
void CDBConnection::Unbind() {}
bool CDBConnection::BindParam(SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLPOINTER, SQLLEN*) { return true; }
bool CDBConnection::BindCol(SQLUSMALLINT, SQLSMALLINT, SQLULEN, SQLPOINTER, SQLLEN*) { return true; }
void CDBConnection::HandleError(SQLRETURN) {}

namespace
{
	void JournalNewPlayer(MemDb& db, const PlayerRow& row)
	{
		Rec r("P");
		r.s(row.name).s(row.password) << row.playerId << row.x << row.y << row.level << static_cast<long long>(row.exp) << static_cast<long long>(row.adena);
		Journal(db, r);
	}

	// 샌드박스 시험용 이름 규칙 "lvNN_…" — NN레벨(1~60)이면 그 값, 아니면 0
	int SandboxLevel(const std::string& name)
	{
		if (name.size() > 4 && name[0] == 'l' && name[1] == 'v' && std::isdigit(static_cast<unsigned char>(name[2])))
		{
			const int lv = std::atoi(name.c_str() + 2);
			if (lv >= 1 && lv <= 60)
				return lv;
		}
		return 0;
	}

	void PutSandboxSlot(MemDb& db, int playerId, uint16_t slot, uint16_t item, uint16_t count, bool equipped)
	{
		DB_ITEM_SLOT_SAVE s;
		s.slotIndex = slot; s.hasItem = true; s.itemId = item; s.count = count; s.equipped = equipped;
		ApplySlot(db, playerId, s);
		Rec r("S");
		r << playerId;
		RecSlot(r, s);
		Journal(db, r);
	}

	void GiveSandboxKit(MemDb& db, int playerId, const std::string& name)
	{
		// 처치 보상 시험(Tools/Test/regress_party.py loot): 이름에 "_FI"가 있으면 가방을 가득 채운다 — 0칸 각인술사 지팡이(착용) ·
		// 나머지 칸(1 ~ MAX_INVENTORY_SLOTS-1) 철검. 막타 전리품이 가방에 못 들어가 발밑에 떨어지는 길(줍기 우선권)을 본다. 첫 접속의 초보자 무기는 들어갈 자리가 없다.
		if (name.find("_FI") != string::npos)
		{
			PutSandboxSlot(db, playerId, 0, 19, 1, true);                  // 견습생의 지팡이
			for (uint16_t slot = 1; slot < MAX_INVENTORY_SLOTS; ++slot)
				PutSandboxSlot(db, playerId, slot, 3, 1, false);           // 철검
		}
		// 처치 보상 시험(regress_party quest): 이름에 "_Q<번호>"가 있으면 그 퀘스트를 받은 채(진행 0) 각인술사로 시작한다(지팡이 착용) —
		// 퀘스트를 주는 마을 NPC까지 가지 않고 들판에서 바로 처치 인정을 본다. 진행 기록 형식 = CUser::SerializeProgress(판 3).
		if (const size_t q = name.find("_Q"); q != string::npos && q + 2 < name.size() && std::isdigit(static_cast<unsigned char>(name[q + 2])))
		{
			const int questId = std::atoi(name.c_str() + q + 2);
			std::vector<uint8_t> blob = { 'P', '2', 3, 1 /* ClassId::Engraver */ };
			blob.resize(blob.size() + 12 + 4, 0);                          // 능력치 배분 6 · 천장 2
			blob.push_back(1);                                             // 진행 중 퀘스트 1개
			blob.push_back(static_cast<uint8_t>(questId & 0xFF));
			blob.push_back(static_cast<uint8_t>((questId >> 8) & 0xFF));
			blob.resize(blob.size() + 3 * 2, 0);                           // 목표 진행 3
			blob.resize(blob.size() + 32, 0);                              // 끝낸 퀘스트 비트
			blob.push_back(0);                                             // 반복 대기 없음
			db.progress[playerId] = blob;
			Rec r("G");
			r << playerId;
			r.s(std::string(blob.begin(), blob.end()));
			Journal(db, r);
			PutSandboxSlot(db, playerId, 0, 19, 1, true);
		}
		// Phase 5 경매 시험: 이름에 "_ak"가 있으면 올릴 물건 꾸러미(희귀 · 영웅 장비 넷 + 약초 물약 20)를 쥐여 준다(착용하지 않은 채)
		if (name.find("_ak") != string::npos)
		{
			auto inst = [](Rarity r, ItemOption a, uint8_t av, ItemOption b, uint8_t bv)
			{
				ItemInstance i;
				i.rarity = r;
				i.opts[0] = OptionRoll{ a, av };
				i.opts[1] = OptionRoll{ b, bv };
				return i.Pack();
			};
			const struct { uint16_t slot, item, count; uint32_t enchant; uint64_t extra; } kit[] = {
				{ 0, 3,  1, 2, inst(Rarity::Rare, ItemOption::Attack, 4, ItemOption::Str, 3) },          // 철검 +2(희귀)
				{ 1, 33, 1, 0, inst(Rarity::Uncommon, ItemOption::Armor, 3, ItemOption::Vit, 2) },       // 사슬 갑옷(고급)
				{ 2, 44, 1, 0, inst(Rarity::Heroic, ItemOption::CritChance, 12, ItemOption::Luk, 4) },   // 은 목걸이(영웅)
				{ 3, 47, 1, 0, inst(Rarity::Rare, ItemOption::MaxHp, 6, ItemOption::Wis, 3) },           // 서약의 투구(희귀)
				{ 4, 1, 20, 0, 0 },                                                                      // 약초 물약 20
			};
			for (const auto& k : kit)
			{
				DB_ITEM_SLOT_SAVE s;
				s.slotIndex = k.slot; s.hasItem = true; s.itemId = k.item; s.count = k.count; s.enchant = k.enchant; s.extra = k.extra;
				ApplySlot(db, playerId, s);
				Rec r("S");
				r << playerId;
				RecSlot(r, s);
				Journal(db, r);
			}
		}
		// 경제 1차 제작 · 분해 시험(Tools/Test/regress_craft.py): 이름에 "_CR" · "_CP" · "_CF"가 있으면 제작 꾸러미(기사로 들어온다 — 착용 장검이 기사 것) —
		//   (대문자 — 다른 시험의 소문자 무작위 꼬리표와 겹치지 않게. "_FI"와 같은 까닭)
		//   0: 하급 사냥 정수 61×100 · 1: 61×20(작은 묶음 — 제작이 먼저 쓴다) · 2: 장비 파편 66×50 · 3: 잿빛 결정 64×30 · 4: 중급 사냥 정수 62×100 ·
		//   5: 철검 3(고급) · 6: 사슬 갑옷 33(고급, 거래 이력 bit 9 — 분해량 절반) · 7: 강철 장검 4(희귀, 착용) · 8: 각인 두루마리 13×5 · 9: 구리 장검 67(강화 대상).
		//   "_CP"는 그로시 0(LackAdena), "_CF"는 나머지 칸(10~)을 철검으로 채운다(제작 BagFull), "_SF"는 같은 꾸러미에 DB 반영이 늘 실패한다(SaveItemBatch).
		if (name.find("_CR") != string::npos || name.find("_CP") != string::npos || name.find("_CF") != string::npos || name.find("_SF") != string::npos)
		{
			const struct { uint16_t slot, item, count; bool equipped; uint64_t extra; } kit[] = {
				{ 0, 61, 100, false, 0 }, { 1, 61, 20, false, 0 }, { 2, 66, 50, false, 0 }, { 3, 64, 30, false, 0 }, { 4, 62, 100, false, 0 },
				{ 5, 3, 1, false, 0 }, { 6, 33, 1, false, ItemRules::kTradedBit }, { 7, 4, 1, true, 0 }, { 8, 13, 5, false, 0 }, { 9, 67, 1, false, 0 },
			};
			uint16_t used = 0;
			for (const auto& k : kit)
			{
				DB_ITEM_SLOT_SAVE s;
				s.slotIndex = k.slot; s.hasItem = true; s.itemId = k.item; s.count = k.count; s.equipped = k.equipped; s.extra = k.extra;
				ApplySlot(db, playerId, s);
				Rec r("S");
				r << playerId;
				RecSlot(r, s);
				Journal(db, r);
				used = static_cast<uint16_t>(std::max<int>(used, k.slot + 1));
			}
			if (name.find("_CF") != string::npos)
				for (uint16_t slot = used; slot < MAX_INVENTORY_SLOTS; ++slot)
					PutSandboxSlot(db, playerId, slot, 3, 1, false);       // 철검
			if (name.find("_CP") != string::npos)
				if (PlayerRow* p = PlayerById(db, playerId))
				{
					p->adena = 0;
					Rec r("A");
					r << playerId << 0;
					Journal(db, r);
				}
		}
		// Phase 6 전체 플레이 녹화: 이름에 "_fp"가 있으면 경매에 올릴 은 목걸이(영웅) · 귀환 두루마리 · 물약을 쥐여 준다
		if (name.find("_fp") != string::npos)
		{
			ItemInstance neck;
			neck.rarity = Rarity::Heroic;
			neck.opts[0] = OptionRoll{ ItemOption::CritChance, 12 };
			neck.opts[1] = OptionRoll{ ItemOption::Luk, 4 };
			const struct { uint16_t slot, item, count; uint32_t enchant; uint64_t extra; } kit[] = {
				{ 0, 44, 1, 0, neck.Pack() },   // 은 목걸이(영웅) — 경매장에 올린다
				{ 1, 10, 3, 0, 0 },             // 귀환 두루마리 3
				{ 2, 1, 30, 0, 0 },             // 약초 물약 30(물약 칸 F1)
				{ 3, 8, 10, 0, 0 },             // 진한 약초 물약 10(전투 칸 1)
			};
			for (const auto& k : kit)
			{
				DB_ITEM_SLOT_SAVE s;
				s.slotIndex = k.slot; s.hasItem = true; s.itemId = k.item; s.count = k.count; s.enchant = k.enchant; s.extra = k.extra;
				ApplySlot(db, playerId, s);
				Rec r("S");
				r << playerId;
				RecSlot(r, s);
				Journal(db, r);
			}
		}
	}

	// P_USER_SELECT · P_USER_SELECT_BY_NAME의 한 줄
	void FillUserInfo(const PlayerRow& r, DB_USER_INFO& info)
	{
		info = DB_USER_INFO{};
		info.m_PlayerId = r.playerId;
		info.m_Name = r.name;
		info.m_X = r.x;
		info.m_Y = r.y;
		// 샌드박스 시험용: 이름이 "xyX_Y_…"이면 저장 위치를 (X, Y)로 돌려준다 — 걸을 수 없는 저장 위치에서의 로그인 회귀 시험
		// (Tools/Test/regress_session.py). "lvNN_xyX_Y_…"도 같다(레벨 NN 캐릭터를 그 자리에 — 몬스터 AI 시험 regress_monster_ai.py).
		// 이 파일은 샌드박스 빌드 전용이라 실제 MySQL에는 이런 규칙이 없다.
		const std::string& name = r.name;
		size_t xyAt = std::string::npos;
		if (name.size() > 4 && name[0] == 'x' && name[1] == 'y' && std::isdigit(static_cast<unsigned char>(name[2])))
			xyAt = 2;
		else if (SandboxLevel(name) > 0)
			if (const size_t p = name.find("_xy"); p != std::string::npos && p + 3 < name.size() && std::isdigit(static_cast<unsigned char>(name[p + 3])))
				xyAt = p + 3;
		if (xyAt != std::string::npos)
		{
			int x = 0, y = 0;
			if (std::sscanf(name.c_str() + xyAt, "%d_%d_", &x, &y) == 2)
			{
				info.m_X = x;
				info.m_Y = y;
			}
		}
		info.m_Level = r.level;
		info.m_Exp = r.exp;
		info.m_Adena = r.adena;
		info.m_Alignment = r.alignment;
		info.m_PkCount = r.pkCount;
	}
}

// P_USER_SELECT_BY_NAME — 이름은 ASCII 대소문자를 가리지 않는다
bool CDBConnection::ExtractUserInfo(const string& name, DB_USER_INFO& out, bool& found)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	found = false;
	const PlayerRow* row = PlayerByName(db, name);
	// 샌드박스 시험용: "lvNN_…" 이름은 처음 접속에서도 NN레벨로 들어오게, 처음 찾을 때 마을 시작 칸에 미리 만들어 둔다
	// (신규 캐릭터 경로는 레벨 1로 들어오므로). 실제 RDS에는 이런 규칙이 없다.
	if (row == nullptr && SandboxLevel(name) > 0)
	{
		PlayerRow r;
		r.playerId = db.nextPlayerId++;
		const size_t pick = std::hash<string>{}(name) % WorldData::TownSpawnPoints().size();
		r.x = WorldData::TownSpawnPoints()[pick].x;
		r.y = WorldData::TownSpawnPoints()[pick].y;
		r.level = static_cast<uint8_t>(SandboxLevel(name));
		r.exp = 0;
		r.adena = 50000;   // 시험 캐릭터: 옛길 이동 · 상점을 바로 쓸 수 있게(Phase 3-C 보스전 녹화)
		row = InsertPlayer(db, name, r);
		JournalNewPlayer(db, *row);
		GiveSandboxKit(db, row->playerId, name);
	}
	if (row == nullptr)
		return true;   // 행 없음(오류 아님)
	FillUserInfo(*row, out);
	found = true;
	return true;
}

// P_USER_SELECT
bool CDBConnection::ExtractUserInfo(int playerId, DB_USER_INFO& out, bool& found)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	found = false;
	const PlayerRow* row = PlayerById(db, playerId);
	if (row == nullptr)
		return true;
	FillUserInfo(*row, out);
	found = true;
	return true;
}

// P_USER_CREATE: 없을 때만 넣고, 있든 없든 그 이름의 UserID를 돌려준다. created = 이 호출이 넣었는가.
bool CDBConnection::AddUserInfoInDataBase(const string& name, const string& password, int& outPlayerId, bool& created)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // 이름 확인과 넣기가 한 번에 — 같은 이름을 동시에 만들면 하나만 created
	created = false;
	if (const PlayerRow* existing = PlayerByName(db, name))
	{
		outPlayerId = existing->playerId;
		return true;
	}
	PlayerRow row;
	row.playerId = db.nextPlayerId++;
	row.password = password;
	// 샌드박스 시험용: 이름이 "lvNN_…"이면 NN레벨로 만든다(기술 녹화 · 시험장 — 모든 기술을 쓸 수 있게).
	// 이 파일은 샌드박스 빌드 전용이라 실제 RDS에는 이런 규칙이 없다.
	if (const int lv = SandboxLevel(name); lv > 0)
		row.level = static_cast<uint8_t>(lv);
	const PlayerRow* inserted = InsertPlayer(db, name, row);
	JournalNewPlayer(db, *inserted);
	outPlayerId = inserted->playerId;
	created = true;
	return true;
}

// P_USER_UPSERT — 지도 밖 위치(0~1999 밖)는 저장된 위치를 그대로 둔다
bool CDBConnection::SaveUserInfo(int playerId, short x, short y, UInt8 level, UInt32 exp)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	PlayerRow* p = PlayerById(db, playerId);
	if (p == nullptr)
		return true;   // UPDATE ... WHERE UserID = ? — 행이 없으면 0행 갱신(오류 아님)
	if (x >= 0 && x <= 1999) p->x = x;
	if (y >= 0 && y <= 1999) p->y = y;
	p->level = level;
	p->exp = exp;
	Rec r("UI");
	r << playerId << p->x << p->y << level << static_cast<long long>(exp);
	Journal(db, r);
	return true;
}

// P_INVENTORY_SELECT
bool CDBConnection::ExtractInventory(int playerId, vector<DB_ITEM_INFO>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	const auto it = db.inventory.find(playerId);
	if (it == db.inventory.end())
		return true;
	for (const auto& [slot, r] : it->second)
	{
		DB_ITEM_INFO info{};
		info.m_SlotIndex = slot;
		info.m_ItemId = r.itemId;
		info.m_Count = r.count;
		info.m_bEquipped = r.equipped;
		info.m_Enchant = r.enchant;
		info.m_Extra = r.extra;
		out.push_back(info);
	}
	return true;
}

bool CDBConnection::SaveInventorySlot(int playerId, uint16_t slotIndex, uint16_t itemId, uint16_t count, bool equipped, uint32_t enchant, uint64_t extra)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	DB_ITEM_SLOT_SAVE s;
	s.slotIndex = slotIndex;
	s.hasItem = true;
	s.itemId = itemId;
	s.count = count;
	s.equipped = equipped;
	s.enchant = enchant;
	s.extra = extra;
	ApplySlot(db, playerId, s);
	Rec r("S");
	r << playerId;
	RecSlot(r, s);
	Journal(db, r);
	return true;
}

bool CDBConnection::DeleteInventorySlot(int playerId, uint16_t slotIndex)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	DB_ITEM_SLOT_SAVE s;
	s.slotIndex = slotIndex;
	s.hasItem = false;
	ApplySlot(db, playerId, s);
	Rec r("S");
	r << playerId;
	RecSlot(r, s);
	Journal(db, r);
	return true;
}

bool CDBConnection::SaveTwoInventorySlots(int playerId, const DB_ITEM_SLOT_SAVE& a, const DB_ITEM_SLOT_SAVE& b)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // 두 슬롯이 한 트랜잭션
	ApplySlot(db, playerId, a);
	ApplySlot(db, playerId, b);
	Rec r("T");
	r << playerId;
	RecSlot(r, a);
	RecSlot(r, b);
	Journal(db, r);
	return true;
}

bool CDBConnection::SavePlayerAdena(int playerId, uint32_t adena)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (auto* p = PlayerById(db, playerId))
	{
		p->adena = adena;
		Rec r("A");
		r << playerId << static_cast<long long>(adena);
		Journal(db, r);
	}
	return true;
}

bool CDBConnection::SavePlayerAlignment(int playerId, int16_t alignment, uint16_t pkCount)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (auto* p = PlayerById(db, playerId))
	{
		p->alignment = alignment;
		p->pkCount = pkCount;
		Rec r("L");
		r << playerId << alignment << pkCount;
		Journal(db, r);
	}
	return true;
}

bool CDBConnection::ShopTransaction(int playerId, uint32_t adena, const DB_ITEM_SLOT_SAVE& slot)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // 그로시 + 슬롯 = 한 트랜잭션
	if (auto* p = PlayerById(db, playerId))
		p->adena = adena;
	// P_SHOP_TRADE는 칸의 Extra를 늘 0으로 쓴다(인자에 없다) — 똑같이 한다. extra가 있는 칸은 서버가 바로 뒤에 P_INVENTORY_UPSERT로 다시 쓴다
	DB_ITEM_SLOT_SAVE row = slot;
	row.extra = 0;
	ApplySlot(db, playerId, row);
	Rec r("X");
	r << playerId << static_cast<long long>(adena);
	RecSlot(r, row);
	Journal(db, r);
	return true;
}

bool CDBConnection::LoadPlayerProgress(int playerId, std::vector<uint8_t>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	const auto it = db.progress.find(playerId);
	if (it != db.progress.end())
		out = it->second;
	return true;
}

bool CDBConnection::SavePlayerProgress(int playerId, const std::vector<uint8_t>& data)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	db.progress[playerId] = data;
	Rec r("G");
	r << playerId;
	r.s(std::string(data.begin(), data.end()));
	Journal(db, r);
	return true;
}

// ── Phase 2-D 기술 성장(UserSkill) — setup.sql P_USER_SKILL_SELECT · _UPSERT · _DELETE와 같은 의미 ──
bool CDBConnection::LoadPlayerSkills(int playerId, std::vector<DB_SKILL_INFO>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	const auto it = db.skills.find(playerId);
	if (it != db.skills.end())
		for (const auto& [id, row] : it->second)
			out.push_back(row);
	return true;
}

bool CDBConnection::SavePlayerSkill(int playerId, const DB_SKILL_INFO& skill)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);   // upsert(기본값이면 삭제) = 한 트랜잭션
	if (skill.m_Level < 1 || skill.m_Level > 10 || skill.m_Pick1 > 2 || skill.m_Pick2 > 2)
		return false;                 // CHECK 제약과 같다
	if (skill.m_Level <= 1 && skill.m_Pick1 == 0 && skill.m_Pick2 == 0)
		db.skills[playerId].erase(skill.m_SkillId);
	else
		db.skills[playerId][skill.m_SkillId] = skill;
	Rec r("K");
	r << playerId << skill.m_SkillId << skill.m_Level << skill.m_Pick1 << skill.m_Pick2;
	Journal(db, r);
	return true;
}

bool CDBConnection::ResetPlayerSkills(int playerId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	db.skills.erase(playerId);
	Rec r("R");
	r << playerId;
	Journal(db, r);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 5 결사 · 경매장 — setup.sql 5~6장 프로시저와 같은 의미(상태 검사에 걸리면 false · 아무것도 바뀌지 않는다)
// ─────────────────────────────────────────────────────────────────────────────
bool CDBConnection::LoadGuilds(vector<DB_GUILD_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [gid, r] : db.guilds)
		out.push_back(DB_GUILD_ROW{ gid, r.name, r.emblem, r.notice, r.leaderId });
	return true;
}

bool CDBConnection::LoadGuildMembers(vector<DB_GUILD_MEMBER_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [pid, m] : db.members)
	{
		DB_GUILD_MEMBER_ROW r;
		r.guildId = m.guildId;
		r.playerId = pid;
		r.rank = m.rank;
		const PlayerRow* p = PlayerById(db, pid);
		r.name = p ? p->name : std::string();
		r.level = p ? p->level : 1;
		r.lastSeenMin = m.lastSeen > 0 ? static_cast<uint32_t>(m.lastSeen / 60) : 0u;
		out.push_back(r);
	}
	return true;
}

bool CDBConnection::GuildCreate(int playerId, uint32_t adenaAfter, int guildId, const string& name, const string& nameKey, uint8_t emblem)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const int64_t createdAt = static_cast<int64_t>(std::time(nullptr));
	if (!ApplyGuildCreate(db, playerId, adenaAfter, guildId, name, nameKey, emblem, createdAt)) return false;
	Rec r("GC");
	r << playerId << static_cast<long long>(adenaAfter) << guildId;
	r.s(name).s(nameKey) << emblem << static_cast<long long>(createdAt);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildMemberSet(int guildId, int playerId, uint8_t rank)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyMemberSet(db, guildId, playerId, rank)) return false;
	Rec r("GM");
	r << guildId << playerId << rank;
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildMemberRemove(int guildId, int playerId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyMemberRemove(db, guildId, playerId)) return false;
	Rec r("GR");
	r << guildId << playerId;
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildTransfer(int guildId, int fromId, int toId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyTransfer(db, guildId, fromId, toId)) return false;
	Rec r("GT");
	r << guildId << fromId << toId;
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildSetInfo(int guildId, uint8_t emblem, const string& notice)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplySetInfo(db, guildId, emblem, notice)) return false;
	Rec r("GI");
	r << guildId << emblem;
	r.s(notice);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildDisband(int guildId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyDisband(db, guildId)) return false;
	Rec r("GD");
	r << guildId;
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildMemberSeen(int playerId, int64_t unixSec)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	auto it = db.members.find(playerId);
	if (it == db.members.end()) return true;
	it->second.lastSeen = unixSec;
	Rec r("GS");
	r << playerId << static_cast<long long>(unixSec);
	Journal(db, r);
	return true;
}

bool CDBConnection::LoadAuctionListings(vector<DB_LISTING_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [id, l] : db.listings)
		if (l.status == 0) out.push_back(l);
	return true;
}

bool CDBConnection::LoadAuctionSettlements(vector<DB_SETTLEMENT_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [id, s] : db.settlements)
		if (!db.claimedAt.count(id)) out.push_back(s);
	return true;
}

bool CDBConnection::LoadAuctionPrices(vector<DB_PRICE_ROW>& out, int64_t sinceUnix)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& p : db.prices)
		if (p.soldAt >= sinceUnix) out.push_back(p);
	return true;
}

bool CDBConnection::AuctionMaxIds(int64_t& maxListing, int64_t& maxSettlement, int& maxGuild)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	maxListing = db.listings.empty() ? 0 : db.listings.rbegin()->first;
	maxSettlement = db.settlements.empty() ? 0 : db.settlements.rbegin()->first;
	maxGuild = db.guilds.empty() ? 0 : db.guilds.rbegin()->first;
	return true;
}

bool CDBConnection::AuctionRegister(int sellerId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, const DB_LISTING_ROW& l)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyRegister(db, sellerId, adenaAfter, slot, l)) return false;
	Rec r("AR");
	r << sellerId << static_cast<long long>(adenaAfter);
	RecSlot(r, slot);
	r << static_cast<long long>(l.listingId) << l.itemId << l.count << l.enchant;
	r.u64(l.extra);
	r << static_cast<long long>(l.price) << static_cast<long long>(l.deposit) << static_cast<long long>(l.createdAt) << static_cast<long long>(l.expiresAt);
	Journal(db, r);
	return true;
}

bool CDBConnection::AuctionBuy(int buyerId, uint32_t adenaAfter, int64_t listingId, uint32_t price, int64_t buyerSettleId, int64_t sellerSettleId, uint32_t sellerGets, int64_t nowUnix,
	uint64_t settleExtraOr)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	// 장애 시험: 트랜잭션 안(아직 아무것도 적용하지 않음)에서 멈춘다 — 이 사이에 죽으면 없었던 일(롤백)
	if (const char* d = std::getenv("HARVEN_DB_BUY_DELAY_MS"))
	{
		const int ms = std::atoi(d);
		if (ms > 0)
		{
			std::cout << "[MemDB] AuctionBuy listing=" << listingId << " — 트랜잭션 안에서 " << ms << "ms 멈춤(장애 시험)\n" << std::flush;
			std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		}
	}
	if (!ApplyBuy(db, buyerId, adenaAfter, listingId, price, buyerSettleId, sellerSettleId, sellerGets, nowUnix, settleExtraOr)) return false;
	Rec r("AB");
	r << buyerId << static_cast<long long>(adenaAfter) << static_cast<long long>(listingId) << static_cast<long long>(price)
	  << static_cast<long long>(buyerSettleId) << static_cast<long long>(sellerSettleId) << static_cast<long long>(sellerGets) << static_cast<long long>(nowUnix);
	r.u64(settleExtraOr);
	Journal(db, r);
	return true;
}

bool CDBConnection::AuctionClose(int64_t listingId, uint8_t status, int64_t settleId, int64_t nowUnix)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyClose(db, listingId, status, settleId, nowUnix)) return false;
	Rec r("AC");
	r << static_cast<long long>(listingId) << status << static_cast<long long>(settleId) << static_cast<long long>(nowUnix);
	Journal(db, r);
	return true;
}

bool CDBConnection::SettlementClaim(int playerId, int64_t settlementId, uint32_t adenaAfter, const DB_ITEM_SLOT_SAVE& slot, int64_t nowUnix)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyClaim(db, playerId, settlementId, adenaAfter, slot, nowUnix)) return false;
	Rec r("SC");
	r << playerId << static_cast<long long>(settlementId) << static_cast<long long>(adenaAfter);
	RecSlot(r, slot);
	r << static_cast<long long>(nowUnix);
	Journal(db, r);
	return true;
}

bool CDBConnection::AuditTotals(uint64_t& walletAdena, uint64_t& settleAdena, uint64_t& invUnits, uint64_t& listingUnits, uint64_t& settleUnits, uint64_t& feesAndDeposits)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	walletAdena = settleAdena = invUnits = listingUnits = settleUnits = feesAndDeposits = 0;
	for (const auto& [id, p] : db.players) walletAdena += p.adena;
	for (const auto& [pid, inv] : db.inventory) for (const auto& [slot, r] : inv) invUnits += r.count;
	for (const auto& [id, l] : db.listings)
	{
		feesAndDeposits += l.deposit;
		if (l.status == 0) listingUnits += l.count;
		if (l.status == 1) feesAndDeposits += AuctionRule::Fee(l.price);
	}
	for (const auto& [id, s] : db.settlements)
	{
		if (db.claimedAt.count(id)) continue;
		if (s.kind == 1) settleAdena += s.adena; else settleUnits += s.count;
	}
	return true;
}

bool CDBConnection::SaveEnchant(int playerId, const DB_ENCHANT_SAVE& save, bool& first)
{
    first = false;
    if (save.count < 2 || save.count > 3) return false;
    const auto& gearBefore = save.before[1]; const auto& gearAfter = save.after[1];
    const bool success = gearAfter.hasItem && gearBefore.enchant < EnchantRule::kMaxLevel && gearAfter.enchant == gearBefore.enchant + 1;
    if (save.itemId != gearBefore.itemId || !EnchantRule::ValidGraceChange(gearBefore.enchant, save.graceBefore, save.graceAfter, success)) return false;
    MemDb& db = Db(); std::scoped_lock lock(db.lock);
    // 샌드박스 시험용(regress_items_edge.py enchantsf): 이름에 "_SF"가 있는 캐릭터는 강화 반영도 늘 실패 — StorageError · 연결 닫기 길을 본다
    if (const PlayerRow* who = PlayerById(db, playerId); who != nullptr && who->name.find("_SF") != std::string::npos)
    {
        std::cout << "[MemDB] P_ENCHANT_TRADE forced failure pid=" << playerId << " (_SF)\n" << std::flush;
        return false;
    }
    const auto graceKey = std::make_pair(save.itemId, gearBefore.enchant);
    const auto playerGrace = db.enchantGrace.find(playerId);
    uint16_t failures = 0;
    if (playerGrace != db.enchantGrace.end())
        if (const auto it = playerGrace->second.find(graceKey); it != playerGrace->second.end()) failures = it->second;
    if (EnchantRule::GraceAttempts(gearBefore.enchant) != 0 && failures != save.graceBefore) return false;
    auto inv = db.inventory.find(playerId);
    if (inv == db.inventory.end()) return false;
    for (size_t i = 0; i < save.count; ++i)
    {
        const auto& b = save.before[i]; const auto it = inv->second.find(b.slotIndex);
        if (it == inv->second.end()) return false;
        const auto& r = it->second;
        if (r.itemId != b.itemId || r.count != b.count || r.equipped != b.equipped || r.enchant != b.enchant || r.extra != b.extra) return false;
    }
    for (size_t i = 0; i < save.count; ++i) ApplySlot(db, playerId, save.after[i]);
    if (EnchantRule::GraceAttempts(gearBefore.enchant) != 0) db.enchantGrace[playerId][graceKey] = save.graceAfter;
    if (save.level >= EnchantRule::FirstNoticeLevel()) first = db.enchantFirst.emplace(std::make_pair(save.itemId, save.level), save.name).second;
    Rec r("EG"); r << playerId << save.count;
    for (size_t i = 0; i < save.count; ++i) RecSlot(r, save.after[i]);
    r << save.itemId << save.level; r.s(save.name); r << gearBefore.enchant << save.graceAfter; Journal(db, r);
    return true;
}

// P_ITEM_BATCH_TRADE — 경제 1차 제작 · 분해(칸 여럿 + 그로시 한 트랜잭션)
bool CDBConnection::SaveItemBatch(int playerId, const DB_ITEM_BATCH_SAVE& save)
{
	if (save.before.empty() || save.before.size() != save.after.size() || save.before.size() > static_cast<size_t>(MAX_INVENTORY_SLOTS))
		return false;
	// HARVEN_DB_BATCH_DELAY_MS=N이면 트랜잭션 전에 N ms 멈춘다 — 제작이 DB에 머무는 동안의 Busy(강화 · 제작 · 장착)를 시험한다(regress_craft.py busy)
	static const int delayMs = [] { const char* v = std::getenv("HARVEN_DB_BATCH_DELAY_MS"); return v != nullptr ? std::atoi(v) : 0; }();
	if (delayMs > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
	MemDb& db = Db();
	std::scoped_lock lock(db.lock);
	// 샌드박스 시험용(regress_craft.py storage): 이름에 "_SF"가 있는 캐릭터는 늘 실패 — DB 반영 실패(StorageError · 연결 닫기) 길을 본다
	const PlayerRow* who = PlayerById(db, playerId);
	if (who != nullptr && who->name.find("_SF") != std::string::npos)
	{
		std::cout << "[MemDB] P_ITEM_BATCH_TRADE forced failure pid=" << playerId << " (_SF)\n" << std::flush;
		return false;
	}
	if (!BatchMatches(db, playerId, save))
	{
		std::cout << "[MemDB] P_ITEM_BATCH_TRADE stale pid=" << playerId << "\n" << std::flush;   // MySQL은 SIGNAL 'Stale batch …'
		return false;
	}
	ApplyItemBatch(db, playerId, save.adenaBefore, save.adenaAfter, save.after);
	Rec r("IB");
	r << playerId << static_cast<long long>(save.adenaBefore) << static_cast<long long>(save.adenaAfter) << static_cast<long long>(save.after.size());
	for (const DB_ITEM_SLOT_SAVE& a : save.after) RecSlot(r, a);
	Journal(db, r);
	return true;
}

DB_TRANSFER_RESULT CDBConnection::SaveTransfer(const DB_TRANSFER_SAVE& save)
{
    if (!TransferSave::Valid(save)) return DB_TRANSFER_RESULT::Rejected;
    auto& db = Db(); std::scoped_lock lock(db.lock);
    const auto data = TransferSave::Json(save);
    if (const auto it = db.transfers.find(save.receipt); it != db.transfers.end())
        return it->second == data ? DB_TRANSFER_RESULT::Committed : DB_TRANSFER_RESULT::Rejected;
    for (int side = 0; side < 2; ++side)
    {
        const auto* p = PlayerById(db, save.playerIds[side]);
        if (!p || p->adena != save.players[side].adenaBefore || (!save.players[side].before.empty() && !BatchMatches(db, save.playerIds[side], save.players[side])))
            return DB_TRANSFER_RESULT::Rejected;
    }
    if (const char* delay = std::getenv("HARVEN_DB_TRANSFER_DELAY_MS"))
        std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(std::atoi(delay), 0, 5000)));
    Rec r("PT"); r.s(save.receipt).s(data);
    for (int side = 0; side < 2; ++side)
    {
        const auto& p = save.players[side]; const int pid = save.playerIds[side];
        PlayerById(db, pid)->adena = p.adenaAfter;
        r << pid << p.adenaAfter << static_cast<long long>(p.after.size());
        for (const auto& a : p.after) { ApplySlot(db, pid, a); RecSlot(r, a); }
    }
    db.transfers[save.receipt] = data; Journal(db, r);
    // COMMIT 응답 유실을 한 번 재현: 재시도는 receipt 확인만 하고 중복 지급하지 않는다.
    if (std::getenv("HARVEN_DB_TRANSFER_LOST_ACK")) return DB_TRANSFER_RESULT::Retry;
    return DB_TRANSFER_RESULT::Committed;
}

bool CDBConnection::LoadEnchantGrace(int playerId, std::vector<DB_ENCHANT_GRACE>& out)
{
    out.clear();
    MemDb& db = Db(); std::scoped_lock lock(db.lock);
    if (const auto it = db.enchantGrace.find(playerId); it != db.enchantGrace.end())
        for (const auto& [key, failures] : it->second)
            if (EnchantRule::GraceAttempts(key.second) != 0) out.push_back({key.first, key.second, failures});
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 결사 성장 — setup.sql P_GUILD_PROGRESS_* · P_GUILD_SEALS_ADD · P_GUILD_SHOP_BUY · P_GUILD_LOG_* · P_GUILD_POST_*와 같은 의미
// ─────────────────────────────────────────────────────────────────────────────
bool CDBConnection::LoadGuildProgress(vector<DB_GUILD_PROGRESS_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [gid, row] : db.guilds)
	{
		DB_GUILD_PROGRESS_ROW r;
		r.guildId = gid;
		r.createdAt = row.createdAt;
		if (const auto it = db.guildGrow.find(gid); it != db.guildGrow.end())
		{
			r.seals = it->second.seals;
			r.data = it->second.data;
		}
		out.push_back(std::move(r));
	}
	return true;
}

bool CDBConnection::GuildProgressSave(int guildId, const std::vector<uint8_t>& data)
{
	if (data.size() > 16000) return false;
	auto& db = Db();
	std::scoped_lock g(db.lock);
	const std::string bytes(data.begin(), data.end());
	if (!ApplyProgressSave(db, guildId, bytes)) return false;
	Rec r("GP");
	r << guildId;
	r.s(bytes);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildSealsAdd(int guildId, int32_t delta, const DB_GUILD_LOG_ROW& log)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplySealsAdd(db, guildId, delta, log)) return false;
	Rec r("GA");
	r << guildId << delta;
	RecLog(r, log);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildShopBuy(int playerId, int guildId, uint32_t price, const DB_ITEM_SLOT_SAVE& slot, const DB_GUILD_LOG_ROW& log)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyShopBuy(db, playerId, guildId, price, slot, log)) return false;
	Rec r("GB");
	r << playerId << guildId << static_cast<long long>(price);
	RecSlot(r, slot);
	RecLog(r, log);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildLogAdd(const DB_GUILD_LOG_ROW& log)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	ApplyGuildLog(db, log);
	Rec r("GL");
	RecLog(r, log);
	Journal(db, r);
	return true;
}

bool CDBConnection::LoadGuildLogs(vector<DB_GUILD_LOG_ROW>& out, int keepPerGuild)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (auto& [gid, rows] : db.guildLogs)
	{
		if (keepPerGuild >= 0 && rows.size() > static_cast<size_t>(keepPerGuild))
			rows.erase(rows.begin(), rows.end() - keepPerGuild);
		out.insert(out.end(), rows.begin(), rows.end());
	}
	return true;
}

bool CDBConnection::LoadGuildPosts(vector<DB_GUILD_POST_ROW>& out)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	out.clear();
	for (const auto& [id, p] : db.guildPosts)
		out.push_back(p);
	std::stable_sort(out.begin(), out.end(), [](const DB_GUILD_POST_ROW& a, const DB_GUILD_POST_ROW& b) { return a.guildId < b.guildId; });
	return true;
}

bool CDBConnection::GuildPostAdd(const DB_GUILD_POST_ROW& post, int64_t dropPostId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyPostAdd(db, post, dropPostId)) return false;
	Rec r("PA");
	r << static_cast<long long>(post.postId) << post.guildId << post.authorId;
	r.s(post.author) << post.category << post.pinned;
	r.s(post.title).s(post.body) << static_cast<long long>(post.at) << static_cast<long long>(dropPostId);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildPostDelete(int guildId, int64_t postId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyPostDelete(db, guildId, postId)) return false;
	Rec r("PD");
	r << guildId << static_cast<long long>(postId);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildPostPin(int guildId, int64_t postId, bool pinned)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	if (!ApplyPostPin(db, guildId, postId, pinned)) return false;
	Rec r("PP");
	r << guildId << static_cast<long long>(postId) << (pinned ? 1 : 0);
	Journal(db, r);
	return true;
}

bool CDBConnection::GuildPostMaxId(int64_t& maxPostId)
{
	auto& db = Db();
	std::scoped_lock g(db.lock);
	maxPostId = db.guildPosts.empty() ? 0 : db.guildPosts.rbegin()->first;
	return true;
}
