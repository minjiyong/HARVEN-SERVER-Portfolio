# MySQL 8.4 / Amazon RDS setup

`setup.sql` is the fresh schema for MySQL 8.4. It creates fifteen InnoDB tables
and the 51 procedures used by `Libs/Database/DBConnection.cpp`. Apply it only
to an empty `GameServerDB`; it replaces procedures when rerun, and it refuses to
run (no change) on a database that still has the old `Players` table — convert
that one with `migrate_user_id.sql` (below).

## Naming (REIW DB convention)

- Procedures `P_<NOUN>_<VERB>` in upper snake case. `SELECT` / `SELECT_ALL` /
  `UPSERT` / `DELETE` for plain CRUD, a specific verb for a multi-statement
  transaction (`P_INVENTORY_SWAP`, `P_SHOP_TRADE`, `P_AUCTION_BUY`, …).
- Parameters `in_<PascalCase>` (`in_UserID`, `in_SlotIndex`, `in_ItemID`).
- PascalCase singular table names and PascalCase columns; IDs end in `ID`.
- A character is keyed by the numeric `User.UserID` everywhere. The name is used
  only to log in: `P_USER_SELECT_BY_NAME` (one row or no row) and
  `P_USER_CREATE` (returns `UserID, Created`; `Created = 1` only when that call
  inserted the row, so two simultaneous first logins with one new name create it
  once). Names are unique case-insensitively (`utf8mb4_0900_ai_ci`): `FOO` and
  `foo` are the same character.

| Old table | New table | Key column(s) |
| --- | --- | --- |
| `Players` | `User` | `UserID` (`Name`, `Password`, `PosX`, `PosY`, `Level`, `Exp`, `Adena`, `Alignment`, `PkCount`) |
| `Inventory` | `UserInventory` | `UserID, SlotIndex` (`ItemID`, `Count`, `Equipped`, `Enchant`, `Extra`) |
| `PlayerProgress` | `UserProgress` | `UserID` (`Data`, `UpdatedAt`) |
| `PlayerSkills` | `UserSkill` | `UserID, SkillID` (`Level`, `Pick1`, `Pick2`, `UpdatedAt`) |
| `Guilds` | `Guild` | `GuildID` (`Name`, `NameKey`, `Emblem`, `Notice`, `LeaderID`, `CreatedAt`) |
| `GuildMembers` | `GuildMember` | `UserID` (`GuildID`, `Rank`, `LastSeen`) |
| `AuctionListings` | `AuctionListing` | `ListingID` (`SellerID`, `BuyerID`, …) |
| `AuctionSettlements` | `AuctionSettlement` | `SettlementID` (owner `UserID`, `ListingID`, …) |
| `AuctionPriceHistory` | `AuctionPriceHistory` | `PriceHistoryID` (`ItemID`, `Price`, `SoldAt`) |
| `EnchantFirstAchievements` | `EnchantFirstAchievement` | `ItemID, Enchant` (`UserID`, `Name`, `AchievedAt`, `ClaimToken`) |
| `EnchantGrace` | `EnchantGrace` | `UserID, ItemID, CurrentEnchant` (`Failures`) |
| — (guild growth) | `GuildProgress` | `GuildID` (`Seals`, `Data`, `UpdatedAt`) — level · contribution · missions … as one versioned blob; `Seals` only changes by ± |
| — (guild growth) | `GuildLog` | `GuildLogID` (`GuildID`, `At`, `Kind`, `Value`, `Who`, `What`) |
| — (guild growth) | `GuildPost` | `GuildPostID` (`GuildID`, `AuthorID`, `Author`, `Category`, `Pinned`, `Title`, `Body`, `At`) |

Constraints and indexes follow the table names (`FK_UserInventory_User`,
`CK_UserSkill_Level`, `UQ_User_Name`, `IX_AuctionSettlement_User`, …).

| Old procedure | New procedure |
| --- | --- |
| `isPlayerRegistered`, `ExtractPlayerInfo` (by name) | `P_USER_SELECT_BY_NAME(in_Name)` |
| `AddNewPlayer` | `P_USER_CREATE(in_Name, in_Password)` |
| — (snapshot read by id) | `P_USER_SELECT(in_UserID)` |
| `SavePlayerInfo` (by name) | `P_USER_UPSERT(in_UserID, in_PosX, in_PosY, in_Level, in_Exp)` |
| `VerifyPlayerPassword` | removed (no caller) |
| `SavePlayerAdena` / `SavePlayerAlignment` | `P_USER_ADENA_UPSERT` / `P_USER_ALIGNMENT_UPSERT` |
| `GetInventory` / `SaveInventorySlot` / `DeleteInventorySlot` / `SaveTwoInventorySlots` | `P_INVENTORY_SELECT` / `_UPSERT` / `_DELETE` / `P_INVENTORY_SWAP` |
| `ShopTransaction` | `P_SHOP_TRADE` |
| `LoadPlayerProgress` / `SavePlayerProgress` | `P_USER_PROGRESS_SELECT` / `_UPSERT` |
| `LoadPlayerSkills` / `SavePlayerSkill` / `ResetPlayerSkills` | `P_USER_SKILL_SELECT` / `_UPSERT` / `_DELETE` |
| `LoadGuilds` / `LoadGuildMembers` | `P_GUILD_SELECT_ALL` / `P_GUILD_MEMBER_SELECT_ALL` |
| `GuildCreate` / `GuildSetInfo` / `GuildDisband` / `GuildTransfer` | `P_GUILD_CREATE` / `P_GUILD_UPSERT` / `P_GUILD_DELETE` / `P_GUILD_TRANSFER` |
| `GuildMemberSet` / `GuildMemberRemove` / `GuildMemberSeen` | `P_GUILD_MEMBER_UPSERT` / `_DELETE` / `P_GUILD_MEMBER_SEEN_UPSERT` |
| `LoadAuctionListings` / `LoadAuctionSettlements` / `LoadAuctionPrices` / `AuctionMaxIds` | `P_AUCTION_LISTING_SELECT_ALL` / `P_AUCTION_SETTLEMENT_SELECT_ALL` / `P_AUCTION_PRICE_SELECT` / `P_AUCTION_MAX_ID_SELECT` |
| `AuctionRegister` / `AuctionBuy` / `AuctionClose` / `AuctionCancel` / `AuctionExpire` / `AuctionAudit` | `P_AUCTION_REGISTER` / `_BUY` / `_CLOSE` / `_CANCEL` / `_EXPIRE` / `_AUDIT` |
| `SettlementClaim` | `P_AUCTION_SETTLEMENT_CLAIM` |
| `LoadEnchantGrace` / `EnchantTransaction` | `P_ENCHANT_GRACE_SELECT` / `P_ENCHANT_TRADE` |
| — (economy phase 1 craft · dismantle) `SaveItemBatch` | `P_ITEM_BATCH_TRADE` |
| `LoadGuildProgress` / `GuildProgressSave` / `GuildSealsAdd` / `GuildShopBuy` | `P_GUILD_PROGRESS_SELECT_ALL` / `P_GUILD_PROGRESS_SAVE` / `P_GUILD_SEALS_ADD` / `P_GUILD_SHOP_BUY` |
| `GuildLogAdd` / `LoadGuildLogs` | `P_GUILD_LOG_INSERT` / `P_GUILD_LOG_SELECT_RECENT` |
| `LoadGuildPosts` / `GuildPostAdd` / `GuildPostDelete` / `GuildPostPin` / `GuildPostMaxId` | `P_GUILD_POST_SELECT_ALL` / `P_GUILD_POST_INSERT` / `P_GUILD_POST_DELETE` / `P_GUILD_POST_PIN` / `P_GUILD_POST_MAX_ID` |

## RDS prerequisites

1. Confirm the instance endpoint and port under **Connectivity & security**.
   While game servers run on developer machines (WSL2 / a local Linux box),
   public access must be enabled and the RDS security group must allow TCP 3306
   from each machine's current public IP `/32`. Never allow `0.0.0.0/0`. Later,
   restrict production RDS to the game servers only and disable public access.
2. Create a custom parameter group in the `mysql8.4` family, set
   `require_secure_transport=ON`, and attach it to the instance. The default
   group cannot be edited. Download the official AWS RDS CA bundle to each machine
   that runs the server, outside the repository (a copy at
   `Shared/Database/mysql/global-bundle.pem` is Git-ignored), then set
   `HARVEN_DB_CA` to its absolute path.
3. Use the master user for schema installation and account creation only. After
   installation, create a separate game-server account and grant `EXECUTE` on
   `GameServerDB.*`. MySQL routines use the schema owner's definer rights by
   default. Keep both passwords outside the repository.
4. Install the MySQL Connector/ODBC **Unicode** driver (Ubuntu 24.04 `.deb`
   from the MySQL downloads page) on every Linux host or Docker image that runs
   the server. Check its exact registered name with `odbcinst -q -d`; do not
   assume the driver version. The MariaDB ODBC driver does not understand
   `SSLMODE=VERIFY_IDENTITY` and must not be used.

For the application account, run the following as the schema owner with a
fresh password entered locally (the placeholder is not a credential):

```sql
CREATE USER 'harven_app'@'%' IDENTIFIED BY '<GENERATED_PASSWORD>' REQUIRE SSL;
GRANT EXECUTE ON GameServerDB.* TO 'harven_app'@'%';
```

The security group remains the network access boundary; `%` here does not
open the RDS network port. Verify with `SHOW GRANTS FOR 'harven_app'@'%'`.
The grant is schema-wide, so the renamed procedures need no new grant.

The current development RDS already has this restricted account. For a new RDS
instance, run the `CREATE USER` and `GRANT` statements above. Keep credentials
outside the repository.

## Fresh schema installation

Use the official MySQL command-line client or MySQL Workbench with the RDS
endpoint, CA bundle, and master credentials. The SQL file uses `DELIMITER` and
must be run as a script, not sent as one SQL statement through ODBC. From the
repository root (`sudo apt-get install mysql-client` for the `mysql` command):

```sh
mysql --host=<RDS_ENDPOINT> --port=3306 --user=harven_admin --password --ssl-mode=VERIFY_IDENTITY --ssl-ca=<ABSOLUTE_CA_PATH> < Shared/Database/mysql/setup.sql
```

MySQL DDL is not transactional. If installation fails, inspect the schema
before retrying.

The database name is `GameServerDB`; table names use the capitalization above
(RDS on Linux compares table names case-sensitively). When using a separate test
database, replace the `GameServerDB` occurrences in a **copy** of the script.

## Upgrading the existing development RDS (`migrate_user_id.sql`)

The development RDS still has the previous schema (`Players`, `Inventory`, …,
with `migrate_enchant_blacksmith.sql` and `migrate_harven_grace.sql` applied).
`migrate_user_id.sql` renames its tables, columns, indexes and constraints in
place and replaces the 39 old procedures with the 38 above. Every row stays,
including `AUTO_INCREMENT` counters, Korean names and binary progress data.

**Run it while every game server that uses the database is stopped** — the old
server cannot run on the new names and the new server cannot run on the old
ones, and DDL is not transactional.

1. Stop all game servers. Take an RDS snapshot (or `mysqldump --routines`).
2. Run the read-only `schema_precheck.sql` first. Its last row, `next_step`, names
   the file(s) to run: `migrate_user_id.sql` alone, the two older migrations
   before it, `setup.sql` for an empty database, or `STOP` for an unexpected state.
3. From the repository root, as the schema owner:

   ```sh
   mysql --host=<RDS_ENDPOINT> --port=3306 --user=harven_admin --password --ssl-mode=VERIFY_IDENTITY --ssl-ca=<ABSOLUTE_CA_PATH> < Shared/Database/mysql/migrate_user_id.sql
   ```

   It prints the row count of each table before and after; they must match
   (the script stops with an error if they do not).
4. Run the read-only `schema_verify.sql`: the missing/leftover lists are empty and
   the last summary row shows `ok = 1` (it ends with `SHOW GRANTS` for `harven_app`).
5. Start the updated server.

Before changing anything the script checks that the schema is exactly the
expected one and otherwise stops with an error and changes nothing: a missing
table/column/index (for example `EnchantGrace` missing → apply the two older
migrations first), enchant columns not yet `INT UNSIGNED`, a `name` collation
other than `utf8mb4_0900_ai_ci`, unknown foreign keys or checks on these tables,
or `Players` and `User` side by side. After such a refusal only the two helper
procedures `P_USER_ID_MIGRATE` and `P_USER_ID_MIGRATE_DDL` are left behind; the
next run replaces and drops them.

Every step checks the current state first, so the script can be run again:
after an interrupted run it finishes what is missing, and on an already
migrated schema it is a no-op (it only re-creates the same procedures).

For an even older schema, apply `migrate_enchant_blacksmith.sql` (pre-enhancement
schema) or `migrate_harven_grace.sql` (blacksmith already installed) first, then
`migrate_user_id.sql`. Those two older scripts still use the old names. Stop the
servers and back up the database before any migration.

## Game server connection settings

Build `linux-release` (or `linux-debug`) and give the server these process
environment variables. On a developer PC put them in `harven.env` at the
repository root (copy of `harven.env.example`, Git-ignored;
`Tools/Setup/setup_wsl.sh` creates it): the server finds that file by walking up
from its executable and loads it before anything else (`Libs/Base/Core/LocalEnv`).
Variables already set in the process environment win, so production can keep
using `export`, a systemd `EnvironmentFile=`, or container secrets instead:

| Variable | Value |
| --- | --- |
| `HARVEN_DB_DRIVER` | Exact registered Unicode ODBC driver name from `odbcinst -q -d` (e.g. `MySQL ODBC 26.7 Unicode Driver` — a wrong or missing driver prints `ODBC [IM002]`) |
| `HARVEN_DB_HOST` | RDS endpoint hostname, without scheme or port |
| `HARVEN_DB_PORT` | `3306` (default if omitted) |
| `HARVEN_DB_NAME` | `GameServerDB` (default if omitted) |
| `HARVEN_DB_USER` | Restricted game-server DB user |
| `HARVEN_DB_PASSWORD` | That user's password |
| `HARVEN_DB_CA` | Path to the downloaded RDS CA PEM bundle (in `harven.env` a relative path is resolved from that file's folder) |

The server uses `SQLDriverConnectW` with `SSLMODE=VERIFY_IDENTITY`; a missing
required value causes connection failure. Do not put passwords in tracked
files, command-line arguments, screenshots, or logs. The server
currently creates eight DB connections, matching its eight DB worker shards.

If the database becomes unreachable while the server runs, saves are not
dropped: each DB shard keeps its pending writes in order and retries the same
write (200 ms, doubling to 2 s; logged once per outage). Logins and auction /
guild transactions fail immediately instead of waiting.

## Verification checklist

- `SHOW TABLES` returns fifteen tables; `information_schema.ROUTINES` shows 51
  procedures in `GameServerDB`, all named `P_…`.
- All eight DB connections succeed with TLS and the restricted account.
- Korean names/notices and `UserProgress.Data` survive a save/relogin cycle.
- Inventory two-slot swap, shop purchase, auction buy/claim, and guild changes
  commit atomically; rejected or conflicting requests do not partially write.

Production should use a separate, private RDS instance reachable only from the
game servers.

## Guild growth (`migrate_guild_growth.sql`)

A database created by an earlier `setup.sql` (or converted by `migrate_user_id.sql`) gets the three guild growth
tables and eleven procedures from `migrate_guild_growth.sql`. Stop the server, back up, run it once (it is safe to
rerun — tables are `IF NOT EXISTS`, procedures are replaced), then `schema_verify.sql` must print `ok = 1`.
Existing rows are not touched; a guild without a `GuildProgress` row starts at level 1 with an empty treasury.

## Economy phase 1 (`migrate_economy_phase1.sql`)

Crafting and dismantling at the blacksmith (`Shared/Document/ECONOMY.md`) commit several inventory slots and the
adena fee in one transaction through `P_ITEM_BATCH_TRADE(in_UserID, in_Slots, in_AdenaBefore, in_AdenaAfter)`.
`in_Slots` is a JSON array with one object per touched slot — the current row (`has`, `item`, `count`, `equipped`,
`enchant`, `extra`; `has = 0` means the slot must be empty) and the new row (`keep`, `newItem`, `newCount`,
`newEquipped`, `newEnchant`, `newExtra`; `keep = 0` deletes it). Every current row is checked with
`SELECT … FOR UPDATE`, and when `in_AdenaBefore <> in_AdenaAfter` the locked `User.Adena` must equal
`in_AdenaBefore`; any mismatch raises `SIGNAL` and rolls everything back. On commit it returns one row `Ok = 1`.

No table changes: the bag grew to 50 slots, which only means `UserInventory.SlotIndex` 30–49 rows may now exist.
A database created by an earlier `setup.sql` gets the procedure from `migrate_economy_phase1.sql` (stop the server,
back up, run it once — it is safe to rerun), then `schema_verify.sql` must print `ok = 1`.

## Player transfer (`migrate_player_transfer.sql`)

Exchange, gift, and direct sale use `P_PLAYER_TRANSFER(receipt, json)` to check and write both players' slots and
wallets in one transaction. Apply `migrate_player_transfer.sql` before starting the updated server, then run
`schema_verify.sql`. Fresh installs already include it in `setup.sql`. Existing inventory rows are preserved.

`PlayerTransfer` stores a unique 128-bit receipt, both user IDs, kind, committed time, and the complete proposal
and before/after values. Replaying the same receipt and payload returns success without applying it again;
a conflicting payload or stale slot/wallet rolls back. Do not delete receipts while an unresolved request can
still be retried. During an ambiguous DB response the server holds both players until the receipt resolves.
