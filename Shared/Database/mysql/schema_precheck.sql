-- ─────────────────────────────────────────────────────────────────────────────
-- schema_precheck.sql — 읽기 전용(아무것도 바꾸지 않는다). 언제 돌려도 된다.
-- 마지막 표의 next_step이 다음에 돌릴 파일이다(이 폴더 README.md · 저장소 README "DB 설정").
--   mysql -h "$H" -P 3306 -u harven_admin -p --ssl-mode=VERIFY_IDENTITY --ssl-ca="$CA" -t < Shared/Database/mysql/schema_precheck.sql
-- ─────────────────────────────────────────────────────────────────────────────
USE GameServerDB;

SELECT VERSION() AS mysql_version, DATABASE() AS db, CURRENT_USER() AS login;

-- 표(행 수는 추정치 — 정확한 수는 migrate_user_id.sql이 전후로 센다)
SELECT TABLE_NAME, TABLE_ROWS AS approx_rows
FROM information_schema.TABLES
WHERE TABLE_SCHEMA = 'GameServerDB'
ORDER BY TABLE_NAME;

-- 강화 열 형식(migrate_user_id.sql은 INT UNSIGNED를 요구한다 — 아니면 migrate_enchant_blacksmith.sql부터)
SELECT TABLE_NAME, COLUMN_NAME, COLUMN_TYPE
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = 'GameServerDB' AND LOWER(COLUMN_NAME) = 'enchant'
ORDER BY TABLE_NAME;

-- 판정에 쓰는 값
SELECT IFNULL(SUM(TABLE_NAME = 'Players'), 0), IFNULL(SUM(TABLE_NAME = 'User'), 0),
       IFNULL(SUM(TABLE_NAME = 'EnchantFirstAchievements'), 0), IFNULL(SUM(TABLE_NAME = 'EnchantGrace'), 0), COUNT(*)
INTO @has_players, @has_user, @has_first, @has_grace, @n_tables
FROM information_schema.TABLES
WHERE TABLE_SCHEMA = 'GameServerDB';

SELECT COUNT(*) INTO @narrow_enchant
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = 'GameServerDB' AND TABLE_NAME IN ('Inventory', 'AuctionListings', 'AuctionSettlements')
  AND COLUMN_NAME = 'enchant' AND COLUMN_TYPE NOT LIKE 'int%unsigned';

-- 프로시저: UserID 스키마 38개(migrate_user_id.sql) · 결사 성장 11개(migrate_guild_growth.sql) · 그 밖
SELECT IFNULL(SUM(ROUTINE_NAME IN ('P_AUCTION_AUDIT','P_AUCTION_BUY','P_AUCTION_CANCEL','P_AUCTION_CLOSE','P_AUCTION_EXPIRE','P_AUCTION_LISTING_SELECT_ALL','P_AUCTION_MAX_ID_SELECT','P_AUCTION_PRICE_SELECT','P_AUCTION_REGISTER','P_AUCTION_SETTLEMENT_CLAIM','P_AUCTION_SETTLEMENT_SELECT_ALL','P_ENCHANT_GRACE_SELECT','P_ENCHANT_TRADE','P_GUILD_CREATE','P_GUILD_DELETE','P_GUILD_MEMBER_DELETE','P_GUILD_MEMBER_SEEN_UPSERT','P_GUILD_MEMBER_SELECT_ALL','P_GUILD_MEMBER_UPSERT','P_GUILD_SELECT_ALL','P_GUILD_TRANSFER','P_GUILD_UPSERT','P_INVENTORY_DELETE','P_INVENTORY_SELECT','P_INVENTORY_SWAP','P_INVENTORY_UPSERT','P_SHOP_TRADE','P_USER_ADENA_UPSERT','P_USER_ALIGNMENT_UPSERT','P_USER_CREATE','P_USER_PROGRESS_SELECT','P_USER_PROGRESS_UPSERT','P_USER_SELECT','P_USER_SELECT_BY_NAME','P_USER_SKILL_DELETE','P_USER_SKILL_SELECT','P_USER_SKILL_UPSERT','P_USER_UPSERT')), 0),
       IFNULL(SUM(ROUTINE_NAME IN ('P_GUILD_LOG_INSERT','P_GUILD_LOG_SELECT_RECENT','P_GUILD_POST_DELETE','P_GUILD_POST_INSERT','P_GUILD_POST_MAX_ID','P_GUILD_POST_PIN','P_GUILD_POST_SELECT_ALL','P_GUILD_PROGRESS_SAVE','P_GUILD_PROGRESS_SELECT_ALL','P_GUILD_SEALS_ADD','P_GUILD_SHOP_BUY')), 0),
       IFNULL(SUM(ROUTINE_NAME NOT IN ('P_AUCTION_AUDIT','P_AUCTION_BUY','P_AUCTION_CANCEL','P_AUCTION_CLOSE','P_AUCTION_EXPIRE','P_AUCTION_LISTING_SELECT_ALL','P_AUCTION_MAX_ID_SELECT','P_AUCTION_PRICE_SELECT','P_AUCTION_REGISTER','P_AUCTION_SETTLEMENT_CLAIM','P_AUCTION_SETTLEMENT_SELECT_ALL','P_ENCHANT_GRACE_SELECT','P_ENCHANT_TRADE','P_GUILD_CREATE','P_GUILD_DELETE','P_GUILD_MEMBER_DELETE','P_GUILD_MEMBER_SEEN_UPSERT','P_GUILD_MEMBER_SELECT_ALL','P_GUILD_MEMBER_UPSERT','P_GUILD_SELECT_ALL','P_GUILD_TRANSFER','P_GUILD_UPSERT','P_INVENTORY_DELETE','P_INVENTORY_SELECT','P_INVENTORY_SWAP','P_INVENTORY_UPSERT','P_SHOP_TRADE','P_USER_ADENA_UPSERT','P_USER_ALIGNMENT_UPSERT','P_USER_CREATE','P_USER_PROGRESS_SELECT','P_USER_PROGRESS_UPSERT','P_USER_SELECT','P_USER_SELECT_BY_NAME','P_USER_SKILL_DELETE','P_USER_SKILL_SELECT','P_USER_SKILL_UPSERT','P_USER_UPSERT','P_GUILD_LOG_INSERT','P_GUILD_LOG_SELECT_RECENT','P_GUILD_POST_DELETE','P_GUILD_POST_INSERT','P_GUILD_POST_MAX_ID','P_GUILD_POST_PIN','P_GUILD_POST_SELECT_ALL','P_GUILD_PROGRESS_SAVE','P_GUILD_PROGRESS_SELECT_ALL','P_GUILD_SEALS_ADD','P_GUILD_SHOP_BUY')), 0)
INTO @new_procs, @guild_procs, @other_procs
FROM information_schema.ROUTINES
WHERE ROUTINE_SCHEMA = 'GameServerDB' AND ROUTINE_TYPE = 'PROCEDURE';

-- 결사 성장 표(GuildProgress · GuildLog · GuildPost)
SELECT COUNT(*) INTO @guild_tables
FROM information_schema.TABLES
WHERE TABLE_SCHEMA = 'GameServerDB' AND TABLE_NAME IN ('GuildProgress', 'GuildLog', 'GuildPost');

SET @growth_done = (@guild_procs = 11 AND @guild_tables = 3);
SET @then_growth = IF(@growth_done, '', ' -> migrate_guild_growth.sql');

-- 경제 1차(migrate_economy_phase1.sql): P_ITEM_BATCH_TRADE(제작 · 분해 한 트랜잭션) · P_AUCTION_BUY 9번째 인자 in_SettleExtraOr(거래 이력 bit 9)
SELECT IFNULL(SUM(ROUTINE_NAME = 'P_ITEM_BATCH_TRADE'), 0) INTO @econ_batch
FROM information_schema.ROUTINES
WHERE ROUTINE_SCHEMA = 'GameServerDB' AND ROUTINE_TYPE = 'PROCEDURE';
SELECT COUNT(*) INTO @econ_buy_arg
FROM information_schema.PARAMETERS
WHERE SPECIFIC_SCHEMA = 'GameServerDB' AND SPECIFIC_NAME = 'P_AUCTION_BUY' AND PARAMETER_NAME = 'in_SettleExtraOr';
SET @econ_done = (@econ_batch = 1 AND @econ_buy_arg = 1);
SET @then_econ = IF(@econ_done, '', ' -> migrate_economy_phase1.sql');

SELECT COUNT(*) INTO @transfer_table FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'GameServerDB' AND TABLE_NAME = 'PlayerTransfer';
SELECT COUNT(*) INTO @transfer_proc FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = 'GameServerDB' AND ROUTINE_NAME = 'P_PLAYER_TRANSFER' AND ROUTINE_TYPE = 'PROCEDURE';
SET @transfer_done = (@transfer_table = 1 AND @transfer_proc = 1);
SET @then_transfer = IF(@transfer_done, '', ' -> migrate_player_transfer.sql');

SELECT @n_tables AS tables, @has_players AS has_Players, @has_user AS has_User,
       @narrow_enchant AS narrow_enchant_cols, @has_first AS has_EnchantFirstAchievements, @has_grace AS has_EnchantGrace,
       @new_procs AS user_id_procs_of_38, @guild_procs AS guild_growth_procs_of_11, @guild_tables AS guild_growth_tables_of_3,
       @other_procs AS other_procs, @econ_batch AS economy_batch_proc, @econ_buy_arg AS economy_buy_arg, @transfer_done AS player_transfer_done,
       CASE
           WHEN @n_tables = 0 THEN 'EMPTY - 빈 DB: setup.sql로 새로 깐다(결사 성장 · 경제 1차까지 들어 있다)'
           WHEN @has_players > 0 AND @has_user > 0 THEN 'STOP - Players와 User가 함께 있다: 아무것도 돌리지 말고 알린다'
           WHEN @has_user > 0 AND @new_procs = 38 AND @growth_done AND @econ_done AND @transfer_done THEN 'DONE - 최신(dev) 스키마: schema_verify.sql로 확인만'
           WHEN @has_user > 0 AND @new_procs = 38 AND @growth_done AND @econ_done THEN 'RUN migrate_player_transfer.sql - 플레이어 교환·주기·거래 저장이 없다'
           WHEN @has_user > 0 AND @new_procs = 38 AND @growth_done THEN CONCAT('RUN migrate_economy_phase1.sql', @then_transfer, ' - 경제 1차가 없다')
           WHEN @has_user > 0 AND @new_procs = 38 THEN CONCAT('RUN migrate_guild_growth.sql', @then_econ, @then_transfer, ' - 결사 성장(표 3 · 프로시저 11)이 없다')
           WHEN @has_user > 0 THEN CONCAT('RUN migrate_user_id.sql', @then_growth, @then_econ, @then_transfer, ' - 표는 새 이름, 프로시저가 모자람')
           WHEN @has_players = 0 THEN 'STOP - Players도 User도 없다: 알린다'
           WHEN @narrow_enchant > 0 OR @has_first = 0 THEN 'RUN migrate_enchant_blacksmith.sql -> migrate_harven_grace.sql -> migrate_user_id.sql -> migrate_guild_growth.sql -> migrate_economy_phase1.sql -> migrate_player_transfer.sql - 강화 이전 스키마'
           WHEN @has_grace = 0 THEN 'RUN migrate_harven_grace.sql -> migrate_user_id.sql -> migrate_guild_growth.sql -> migrate_economy_phase1.sql -> migrate_player_transfer.sql'
           ELSE 'RUN migrate_user_id.sql -> migrate_guild_growth.sql -> migrate_economy_phase1.sql -> migrate_player_transfer.sql'
       END AS next_step;
