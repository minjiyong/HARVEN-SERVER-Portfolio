-- UserID / REIW naming upgrade for an existing GameServerDB that already has the previous setup.sql,
-- migrate_enchant_blacksmith.sql and migrate_harven_grace.sql applied (tables Players, Inventory, ...).
-- Every row is kept: tables, columns, indexes and constraints are renamed in place, and the old
-- procedures are replaced by the P_<NOUN>_<VERB> procedures of setup.sql.
--
--   1. Stop every game server that uses this database. The old server cannot run on the new names
--      and the new server cannot run on the old ones; MySQL DDL is not transactional.
--   2. Take an RDS snapshot (or mysqldump --routines) first.
--   3. Run as the schema owner from the repository root:
--      mysql --host=<RDS_ENDPOINT> --port=3306 --user=harven_admin --password --ssl-mode=VERIFY_IDENTITY
--            --ssl-ca=<ABSOLUTE_CA_PATH> < Shared/Database/mysql/migrate_user_id.sql
--   4. Start the updated server.
--
-- The script checks the schema before it changes anything and stops with an error (no change) on
-- anything it does not expect: unknown constraints, a missing table/column/index, Players and User
-- side by side, or a Name collation other than utf8mb4_0900_ai_ci. Every step checks the current
-- state first, so running it again (or after an interrupted run) only finishes what is missing;
-- on an already migrated schema it changes nothing except re-creating the same procedures.
-- Row counts of all eleven tables are compared before and after.
USE GameServerDB;

DELIMITER $$

DROP PROCEDURE IF EXISTS P_USER_ID_MIGRATE_DDL$$
CREATE PROCEDURE P_USER_ID_MIGRATE_DDL(IN in_Sql TEXT)
BEGIN
    SET @user_id_migrate_sql = in_Sql;
    PREPARE user_id_migrate_stmt FROM @user_id_migrate_sql;
    EXECUTE user_id_migrate_stmt;
    DEALLOCATE PREPARE user_id_migrate_stmt;
END$$

DROP PROCEDURE IF EXISTS P_USER_ID_MIGRATE$$
CREATE PROCEDURE P_USER_ID_MIGRATE()
BEGIN
    DECLARE v_i INT;
    DECLARE v_n INT;
    DECLARE v_table VARCHAR(64);
    DECLARE v_oldTable VARCHAR(64);
    DECLARE v_old VARCHAR(64);
    DECLARE v_new VARCHAR(64);
    DECLARE v_kind VARCHAR(16);
    DECLARE v_def TEXT;
    DECLARE v_found INT;
    DECLARE v_text TEXT;
    DECLARE v_msg VARCHAR(128);

    DROP TEMPORARY TABLE IF EXISTS tmp_UserIdTable;
    DROP TEMPORARY TABLE IF EXISTS tmp_UserIdColumn;
    DROP TEMPORARY TABLE IF EXISTS tmp_UserIdIndex;
    DROP TEMPORARY TABLE IF EXISTS tmp_UserIdOldConstraint;
    DROP TEMPORARY TABLE IF EXISTS tmp_UserIdNewConstraint;

    -- ── Plan ──
    CREATE TEMPORARY TABLE tmp_UserIdTable (
        Seq INT AUTO_INCREMENT PRIMARY KEY, OldName VARCHAR(64) NOT NULL, NewName VARCHAR(64) NOT NULL,
        RowsBefore BIGINT NULL, RowsAfter BIGINT NULL);
    INSERT INTO tmp_UserIdTable (OldName, NewName) VALUES
        ('Players', 'User'), ('Inventory', 'UserInventory'), ('PlayerProgress', 'UserProgress'),
        ('PlayerSkills', 'UserSkill'), ('Guilds', 'Guild'), ('GuildMembers', 'GuildMember'),
        ('AuctionListings', 'AuctionListing'), ('AuctionSettlements', 'AuctionSettlement'),
        ('AuctionPriceHistory', 'AuctionPriceHistory'),
        ('EnchantFirstAchievements', 'EnchantFirstAchievement'), ('EnchantGrace', 'EnchantGrace');

    CREATE TEMPORARY TABLE tmp_UserIdColumn (
        Seq INT AUTO_INCREMENT PRIMARY KEY, OldTable VARCHAR(64) NOT NULL, NewTable VARCHAR(64) NOT NULL,
        OldName VARCHAR(64) NOT NULL, NewName VARCHAR(64) NOT NULL);
    INSERT INTO tmp_UserIdColumn (OldTable, NewTable, OldName, NewName) VALUES
        ('Players', 'User', 'playerId', 'UserID'), ('Players', 'User', 'name', 'Name'),
        ('Players', 'User', 'password', 'Password'), ('Players', 'User', 'x', 'PosX'),
        ('Players', 'User', 'y', 'PosY'), ('Players', 'User', 'level', 'Level'),
        ('Players', 'User', 'exp', 'Exp'), ('Players', 'User', 'adena', 'Adena'),
        ('Players', 'User', 'alignment', 'Alignment'), ('Players', 'User', 'pkCount', 'PkCount'),
        ('Inventory', 'UserInventory', 'playerId', 'UserID'), ('Inventory', 'UserInventory', 'slotIndex', 'SlotIndex'),
        ('Inventory', 'UserInventory', 'itemId', 'ItemID'), ('Inventory', 'UserInventory', 'count', 'Count'),
        ('Inventory', 'UserInventory', 'equipped', 'Equipped'), ('Inventory', 'UserInventory', 'enchant', 'Enchant'),
        ('Inventory', 'UserInventory', 'extra', 'Extra'),
        ('PlayerProgress', 'UserProgress', 'playerId', 'UserID'), ('PlayerProgress', 'UserProgress', 'data', 'Data'),
        ('PlayerProgress', 'UserProgress', 'updatedAt', 'UpdatedAt'),
        ('PlayerSkills', 'UserSkill', 'playerId', 'UserID'), ('PlayerSkills', 'UserSkill', 'skillId', 'SkillID'),
        ('PlayerSkills', 'UserSkill', 'level', 'Level'), ('PlayerSkills', 'UserSkill', 'pick1', 'Pick1'),
        ('PlayerSkills', 'UserSkill', 'pick2', 'Pick2'), ('PlayerSkills', 'UserSkill', 'updatedAt', 'UpdatedAt'),
        ('Guilds', 'Guild', 'guildId', 'GuildID'), ('Guilds', 'Guild', 'name', 'Name'),
        ('Guilds', 'Guild', 'nameKey', 'NameKey'), ('Guilds', 'Guild', 'emblem', 'Emblem'),
        ('Guilds', 'Guild', 'notice', 'Notice'), ('Guilds', 'Guild', 'leaderId', 'LeaderID'),
        ('Guilds', 'Guild', 'createdAt', 'CreatedAt'),
        ('GuildMembers', 'GuildMember', 'playerId', 'UserID'), ('GuildMembers', 'GuildMember', 'guildId', 'GuildID'),
        ('GuildMembers', 'GuildMember', 'rank', 'Rank'), ('GuildMembers', 'GuildMember', 'lastSeen', 'LastSeen'),
        ('AuctionListings', 'AuctionListing', 'listingId', 'ListingID'), ('AuctionListings', 'AuctionListing', 'sellerId', 'SellerID'),
        ('AuctionListings', 'AuctionListing', 'itemId', 'ItemID'), ('AuctionListings', 'AuctionListing', 'count', 'Count'),
        ('AuctionListings', 'AuctionListing', 'enchant', 'Enchant'), ('AuctionListings', 'AuctionListing', 'extra', 'Extra'),
        ('AuctionListings', 'AuctionListing', 'price', 'Price'), ('AuctionListings', 'AuctionListing', 'deposit', 'Deposit'),
        ('AuctionListings', 'AuctionListing', 'createdAt', 'CreatedAt'), ('AuctionListings', 'AuctionListing', 'expiresAt', 'ExpiresAt'),
        ('AuctionListings', 'AuctionListing', 'status', 'Status'), ('AuctionListings', 'AuctionListing', 'buyerId', 'BuyerID'),
        ('AuctionListings', 'AuctionListing', 'closedAt', 'ClosedAt'),
        ('AuctionSettlements', 'AuctionSettlement', 'settlementId', 'SettlementID'),
        ('AuctionSettlements', 'AuctionSettlement', 'playerId', 'UserID'), ('AuctionSettlements', 'AuctionSettlement', 'kind', 'Kind'),
        ('AuctionSettlements', 'AuctionSettlement', 'adena', 'Adena'), ('AuctionSettlements', 'AuctionSettlement', 'itemId', 'ItemID'),
        ('AuctionSettlements', 'AuctionSettlement', 'count', 'Count'), ('AuctionSettlements', 'AuctionSettlement', 'enchant', 'Enchant'),
        ('AuctionSettlements', 'AuctionSettlement', 'extra', 'Extra'), ('AuctionSettlements', 'AuctionSettlement', 'listingId', 'ListingID'),
        ('AuctionSettlements', 'AuctionSettlement', 'reason', 'Reason'), ('AuctionSettlements', 'AuctionSettlement', 'createdAt', 'CreatedAt'),
        ('AuctionSettlements', 'AuctionSettlement', 'claimedAt', 'ClaimedAt'),
        ('AuctionPriceHistory', 'AuctionPriceHistory', 'id', 'PriceHistoryID'),
        ('AuctionPriceHistory', 'AuctionPriceHistory', 'itemId', 'ItemID'),
        ('AuctionPriceHistory', 'AuctionPriceHistory', 'price', 'Price'),
        ('AuctionPriceHistory', 'AuctionPriceHistory', 'soldAt', 'SoldAt'),
        ('EnchantFirstAchievements', 'EnchantFirstAchievement', 'itemId', 'ItemID'),
        ('EnchantFirstAchievements', 'EnchantFirstAchievement', 'enchant', 'Enchant'),
        ('EnchantFirstAchievements', 'EnchantFirstAchievement', 'playerId', 'UserID'),
        ('EnchantFirstAchievements', 'EnchantFirstAchievement', 'name', 'Name'),
        ('EnchantFirstAchievements', 'EnchantFirstAchievement', 'achievedAt', 'AchievedAt'),
        ('EnchantFirstAchievements', 'EnchantFirstAchievement', 'claimToken', 'ClaimToken'),
        ('EnchantGrace', 'EnchantGrace', 'playerId', 'UserID'), ('EnchantGrace', 'EnchantGrace', 'itemId', 'ItemID'),
        ('EnchantGrace', 'EnchantGrace', 'currentEnchant', 'CurrentEnchant'), ('EnchantGrace', 'EnchantGrace', 'failures', 'Failures');

    CREATE TEMPORARY TABLE tmp_UserIdIndex (
        Seq INT AUTO_INCREMENT PRIMARY KEY, OldTable VARCHAR(64) NOT NULL, NewTable VARCHAR(64) NOT NULL,
        OldName VARCHAR(64) NOT NULL, NewName VARCHAR(64) NOT NULL);
    INSERT INTO tmp_UserIdIndex (OldTable, NewTable, OldName, NewName) VALUES
        ('Players', 'User', 'UQ_Players_Name', 'UQ_User_Name'),
        ('Guilds', 'Guild', 'UQ_Guilds_NameKey', 'UQ_Guild_NameKey'),
        ('GuildMembers', 'GuildMember', 'IX_GuildMembers_Guild', 'IX_GuildMember_Guild'),
        ('AuctionListings', 'AuctionListing', 'IX_AuctionListings_Status', 'IX_AuctionListing_Status'),
        ('AuctionListings', 'AuctionListing', 'IX_AuctionListings_Seller', 'IX_AuctionListing_Seller'),
        ('AuctionSettlements', 'AuctionSettlement', 'IX_AuctionSettlements_Player', 'IX_AuctionSettlement_User'),
        ('AuctionPriceHistory', 'AuctionPriceHistory', 'IX_AuctionPriceHistory_Item', 'IX_AuctionPriceHistory_Item');

    CREATE TEMPORARY TABLE tmp_UserIdOldConstraint (
        Seq INT AUTO_INCREMENT PRIMARY KEY, Name VARCHAR(64) NOT NULL, Kind VARCHAR(16) NOT NULL);
    INSERT INTO tmp_UserIdOldConstraint (Name, Kind) VALUES
        ('FK_Inventory_Players', 'FOREIGN KEY'), ('FK_PlayerProgress_Players', 'FOREIGN KEY'),
        ('FK_PlayerSkills_Players', 'FOREIGN KEY'), ('FK_GuildMembers_Guilds', 'FOREIGN KEY'),
        ('FK_GuildMembers_Players', 'FOREIGN KEY'), ('FK_AuctionListings_Players', 'FOREIGN KEY'),
        ('FK_AuctionSettlements_Players', 'FOREIGN KEY'), ('FK_EnchantGrace_Players', 'FOREIGN KEY'),
        ('CK_PlayerSkills_level', 'CHECK'), ('CK_PlayerSkills_pick', 'CHECK'), ('CK_GuildMembers_rank', 'CHECK');

    CREATE TEMPORARY TABLE tmp_UserIdNewConstraint (
        Seq INT AUTO_INCREMENT PRIMARY KEY, TableName VARCHAR(64) NOT NULL, Name VARCHAR(64) NOT NULL,
        Kind VARCHAR(16) NOT NULL, Definition TEXT NOT NULL);
    INSERT INTO tmp_UserIdNewConstraint (TableName, Name, Kind, Definition) VALUES
        ('UserInventory', 'FK_UserInventory_User', 'FOREIGN KEY', 'FOREIGN KEY (UserID) REFERENCES User(UserID)'),
        ('UserProgress', 'FK_UserProgress_User', 'FOREIGN KEY', 'FOREIGN KEY (UserID) REFERENCES User(UserID) ON DELETE CASCADE'),
        ('UserSkill', 'CK_UserSkill_Level', 'CHECK', 'CHECK (Level BETWEEN 1 AND 10)'),
        ('UserSkill', 'CK_UserSkill_Pick', 'CHECK', 'CHECK (Pick1 BETWEEN 0 AND 2 AND Pick2 BETWEEN 0 AND 2)'),
        ('UserSkill', 'FK_UserSkill_User', 'FOREIGN KEY', 'FOREIGN KEY (UserID) REFERENCES User(UserID) ON DELETE CASCADE'),
        ('GuildMember', 'CK_GuildMember_Rank', 'CHECK', 'CHECK (`Rank` BETWEEN 0 AND 3)'),
        ('GuildMember', 'FK_GuildMember_Guild', 'FOREIGN KEY', 'FOREIGN KEY (GuildID) REFERENCES Guild(GuildID) ON DELETE CASCADE'),
        ('GuildMember', 'FK_GuildMember_User', 'FOREIGN KEY', 'FOREIGN KEY (UserID) REFERENCES User(UserID)'),
        ('AuctionListing', 'FK_AuctionListing_User', 'FOREIGN KEY', 'FOREIGN KEY (SellerID) REFERENCES User(UserID)'),
        ('AuctionSettlement', 'FK_AuctionSettlement_User', 'FOREIGN KEY', 'FOREIGN KEY (UserID) REFERENCES User(UserID)'),
        ('EnchantGrace', 'FK_EnchantGrace_User', 'FOREIGN KEY', 'FOREIGN KEY (UserID) REFERENCES User(UserID) ON DELETE CASCADE');

    -- ── Checks (nothing is changed before all of them pass) ──
    -- Each table exists under exactly one of its two names.
    SELECT GROUP_CONCAT(t.OldName) INTO v_text FROM tmp_UserIdTable t
    WHERE (SELECT COUNT(*) FROM information_schema.TABLES s
           WHERE s.TABLE_SCHEMA = DATABASE() AND s.TABLE_NAME IN (t.OldName, t.NewName)) <> 1;
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Missing or duplicated (old and new name) table: ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;
    -- Every column exists under its old or its new name (exact case).
    SELECT GROUP_CONCAT(CONCAT(c.NewTable, '.', c.NewName)) INTO v_text FROM tmp_UserIdColumn c
    WHERE (SELECT COUNT(*) FROM information_schema.COLUMNS s
           WHERE s.TABLE_SCHEMA = DATABASE() AND s.TABLE_NAME IN (c.OldTable, c.NewTable)
             AND CAST(s.COLUMN_NAME AS BINARY) IN (CAST(c.OldName AS BINARY), CAST(c.NewName AS BINARY))) <> 1;
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Unexpected columns, schema is not setup.sql + both migrations: ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;
    -- migrate_enchant_blacksmith.sql widened the three enchant columns.
    SELECT COUNT(*) INTO v_found FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME IN ('Inventory', 'UserInventory', 'AuctionListings', 'AuctionListing', 'AuctionSettlements', 'AuctionSettlement')
      AND COLUMN_NAME = 'enchant' AND COLUMN_TYPE = 'int unsigned';
    IF v_found <> 3 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Enchant columns are not INT UNSIGNED: apply migrate_enchant_blacksmith.sql first';
    END IF;
    -- The server relies on the case-insensitive unique character name.
    IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME IN ('Players', 'User') AND COLUMN_NAME = 'name'
                     AND COLLATION_NAME = 'utf8mb4_0900_ai_ci') THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Players.name collation is not utf8mb4_0900_ai_ci';
    END IF;
    -- Every index exists under its old or its new name.
    SELECT GROUP_CONCAT(i.NewName) INTO v_text FROM tmp_UserIdIndex i
    WHERE NOT EXISTS (SELECT 1 FROM information_schema.STATISTICS s
                      WHERE s.TABLE_SCHEMA = DATABASE() AND s.TABLE_NAME IN (i.OldTable, i.NewTable)
                        AND s.INDEX_NAME IN (i.OldName, i.NewName));
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Missing index: ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;
    -- Only the known foreign keys and checks (old or new names) are on these tables.
    SELECT GROUP_CONCAT(s.CONSTRAINT_NAME) INTO v_text FROM information_schema.TABLE_CONSTRAINTS s
    WHERE s.CONSTRAINT_SCHEMA = DATABASE() AND s.CONSTRAINT_TYPE IN ('FOREIGN KEY', 'CHECK')
      AND s.TABLE_NAME IN ('Players', 'User', 'Inventory', 'UserInventory', 'PlayerProgress', 'UserProgress',
                           'PlayerSkills', 'UserSkill', 'Guilds', 'Guild', 'GuildMembers', 'GuildMember',
                           'AuctionListings', 'AuctionListing', 'AuctionSettlements', 'AuctionSettlement',
                           'AuctionPriceHistory', 'EnchantFirstAchievements', 'EnchantFirstAchievement', 'EnchantGrace')
      AND s.CONSTRAINT_NAME NOT IN (
          'FK_Inventory_Players', 'FK_PlayerProgress_Players', 'FK_PlayerSkills_Players', 'FK_GuildMembers_Guilds',
          'FK_GuildMembers_Players', 'FK_AuctionListings_Players', 'FK_AuctionSettlements_Players', 'FK_EnchantGrace_Players',
          'CK_PlayerSkills_level', 'CK_PlayerSkills_pick', 'CK_GuildMembers_rank',
          'FK_UserInventory_User', 'FK_UserProgress_User', 'FK_UserSkill_User', 'FK_GuildMember_Guild',
          'FK_GuildMember_User', 'FK_AuctionListing_User', 'FK_AuctionSettlement_User', 'FK_EnchantGrace_User',
          'CK_UserSkill_Level', 'CK_UserSkill_Pick', 'CK_GuildMember_Rank');
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Unknown constraints (drop or rename by hand first): ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;

    -- Row counts before (under whichever name the table has now).
    SELECT COUNT(*) INTO v_n FROM tmp_UserIdTable;
    SET v_i = 1;
    WHILE v_i <= v_n DO
        SELECT OldName, NewName INTO v_old, v_new FROM tmp_UserIdTable WHERE Seq = v_i;
        SET v_table = IF(EXISTS (SELECT 1 FROM information_schema.TABLES
                                 WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = v_new), v_new, v_old);
        CALL P_USER_ID_MIGRATE_DDL(CONCAT('SELECT COUNT(*) INTO @user_id_migrate_rows FROM `', v_table, '`'));
        UPDATE tmp_UserIdTable SET RowsBefore = @user_id_migrate_rows WHERE Seq = v_i;
        SET v_i = v_i + 1;
    END WHILE;

    -- ── 1. Drop the old foreign keys and checks (they name old tables and columns) ──
    SELECT COUNT(*) INTO v_n FROM tmp_UserIdOldConstraint;
    SET v_i = 1;
    WHILE v_i <= v_n DO
        SELECT Name, Kind INTO v_old, v_kind FROM tmp_UserIdOldConstraint WHERE Seq = v_i;
        SET v_table = NULL;
        SELECT MIN(TABLE_NAME) INTO v_table FROM information_schema.TABLE_CONSTRAINTS
        WHERE CONSTRAINT_SCHEMA = DATABASE() AND CONSTRAINT_NAME = v_old AND CONSTRAINT_TYPE = v_kind;
        IF v_table IS NOT NULL THEN
            CALL P_USER_ID_MIGRATE_DDL(CONCAT('ALTER TABLE `', v_table, '` DROP ',
                IF(v_kind = 'CHECK', 'CHECK', 'FOREIGN KEY'), ' `', v_old, '`'));
        END IF;
        SET v_i = v_i + 1;
    END WHILE;

    -- ── 2. Rename the tables ──
    SELECT COUNT(*) INTO v_n FROM tmp_UserIdTable;
    SET v_i = 1;
    WHILE v_i <= v_n DO
        SELECT OldName, NewName INTO v_old, v_new FROM tmp_UserIdTable WHERE Seq = v_i;
        IF CAST(v_old AS BINARY) <> CAST(v_new AS BINARY) AND EXISTS (
            SELECT 1 FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = v_old) THEN
            CALL P_USER_ID_MIGRATE_DDL(CONCAT('RENAME TABLE `', v_old, '` TO `', v_new, '`'));
        END IF;
        SET v_i = v_i + 1;
    END WHILE;

    -- ── 3. Rename the columns (metadata only) ──
    SELECT COUNT(*) INTO v_n FROM tmp_UserIdColumn;
    SET v_i = 1;
    WHILE v_i <= v_n DO
        SELECT NewTable, OldName, NewName INTO v_table, v_old, v_new FROM tmp_UserIdColumn WHERE Seq = v_i;
        IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = v_table
                     AND CAST(COLUMN_NAME AS BINARY) = CAST(v_old AS BINARY)) THEN
            CALL P_USER_ID_MIGRATE_DDL(CONCAT('ALTER TABLE `', v_table, '` RENAME COLUMN `', v_old, '` TO `', v_new, '`'));
        END IF;
        SET v_i = v_i + 1;
    END WHILE;

    -- ── 4. Rename the indexes (metadata only) ──
    SELECT COUNT(*) INTO v_n FROM tmp_UserIdIndex;
    SET v_i = 1;
    WHILE v_i <= v_n DO
        SELECT NewTable, OldName, NewName INTO v_table, v_old, v_new FROM tmp_UserIdIndex WHERE Seq = v_i;
        IF v_old <> v_new AND EXISTS (SELECT 1 FROM information_schema.STATISTICS
                                      WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = v_table AND INDEX_NAME = v_old) THEN
            CALL P_USER_ID_MIGRATE_DDL(CONCAT('ALTER TABLE `', v_table, '` RENAME INDEX `', v_old, '` TO `', v_new, '`'));
        END IF;
        SET v_i = v_i + 1;
    END WHILE;

    -- ── 5. Add the foreign keys and checks under the new names (validates every row again) ──
    SELECT COUNT(*) INTO v_n FROM tmp_UserIdNewConstraint;
    SET v_i = 1;
    WHILE v_i <= v_n DO
        SELECT TableName, Name, Kind, Definition INTO v_table, v_new, v_kind, v_def
        FROM tmp_UserIdNewConstraint WHERE Seq = v_i;
        IF NOT EXISTS (SELECT 1 FROM information_schema.TABLE_CONSTRAINTS
                       WHERE CONSTRAINT_SCHEMA = DATABASE() AND TABLE_NAME = v_table
                         AND CONSTRAINT_NAME = v_new AND CONSTRAINT_TYPE = v_kind) THEN
            CALL P_USER_ID_MIGRATE_DDL(CONCAT('ALTER TABLE `', v_table, '` ADD CONSTRAINT `', v_new, '` ', v_def));
        END IF;
        SET v_i = v_i + 1;
    END WHILE;

    -- ── Verify the result ──
    SELECT GROUP_CONCAT(CONCAT(c.NewTable, '.', c.NewName)) INTO v_text FROM tmp_UserIdColumn c
    WHERE NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS s
                      WHERE s.TABLE_SCHEMA = DATABASE() AND s.TABLE_NAME = c.NewTable
                        AND CAST(s.COLUMN_NAME AS BINARY) = CAST(c.NewName AS BINARY));
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Column rename incomplete: ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;
    SELECT GROUP_CONCAT(i.NewName) INTO v_text FROM tmp_UserIdIndex i
    WHERE NOT EXISTS (SELECT 1 FROM information_schema.STATISTICS s
                      WHERE s.TABLE_SCHEMA = DATABASE() AND s.TABLE_NAME = i.NewTable AND s.INDEX_NAME = i.NewName);
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Index rename incomplete: ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;
    SELECT GROUP_CONCAT(n.Name) INTO v_text FROM tmp_UserIdNewConstraint n
    WHERE NOT EXISTS (SELECT 1 FROM information_schema.TABLE_CONSTRAINTS s
                      WHERE s.CONSTRAINT_SCHEMA = DATABASE() AND s.TABLE_NAME = n.TableName
                        AND s.CONSTRAINT_NAME = n.Name AND s.CONSTRAINT_TYPE = n.Kind);
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Constraint missing: ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;
    SELECT COUNT(*) INTO v_n FROM tmp_UserIdTable;
    SET v_i = 1;
    WHILE v_i <= v_n DO
        SELECT NewName INTO v_new FROM tmp_UserIdTable WHERE Seq = v_i;
        CALL P_USER_ID_MIGRATE_DDL(CONCAT('SELECT COUNT(*) INTO @user_id_migrate_rows FROM `', v_new, '`'));
        UPDATE tmp_UserIdTable SET RowsAfter = @user_id_migrate_rows WHERE Seq = v_i;
        SET v_i = v_i + 1;
    END WHILE;
    SELECT GROUP_CONCAT(NewName) INTO v_text FROM tmp_UserIdTable WHERE RowsAfter <> RowsBefore;
    IF v_text IS NOT NULL THEN
        SET v_msg = LEFT(CONCAT('Row count changed: ', v_text), 128);
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = v_msg;
    END IF;

    SELECT NewName AS TableName, RowsBefore, RowsAfter FROM tmp_UserIdTable ORDER BY Seq;
    DROP TEMPORARY TABLE tmp_UserIdTable;
    DROP TEMPORARY TABLE tmp_UserIdColumn;
    DROP TEMPORARY TABLE tmp_UserIdIndex;
    DROP TEMPORARY TABLE tmp_UserIdOldConstraint;
    DROP TEMPORARY TABLE tmp_UserIdNewConstraint;
END$$

DELIMITER ;

-- The mysql client stops at the first error, so nothing below runs unless the tables are migrated.
CALL P_USER_ID_MIGRATE();
DROP PROCEDURE P_USER_ID_MIGRATE;
DROP PROCEDURE P_USER_ID_MIGRATE_DDL;

-- Old procedures (name-keyed ones included) — the new server never calls them.
DROP PROCEDURE IF EXISTS isPlayerRegistered;
DROP PROCEDURE IF EXISTS AddNewPlayer;
DROP PROCEDURE IF EXISTS VerifyPlayerPassword;
DROP PROCEDURE IF EXISTS ExtractPlayerInfo;
DROP PROCEDURE IF EXISTS SavePlayerInfo;
DROP PROCEDURE IF EXISTS GetInventory;
DROP PROCEDURE IF EXISTS SaveInventorySlot;
DROP PROCEDURE IF EXISTS DeleteInventorySlot;
DROP PROCEDURE IF EXISTS SaveTwoInventorySlots;
DROP PROCEDURE IF EXISTS SavePlayerAdena;
DROP PROCEDURE IF EXISTS SavePlayerAlignment;
DROP PROCEDURE IF EXISTS ShopTransaction;
DROP PROCEDURE IF EXISTS LoadPlayerProgress;
DROP PROCEDURE IF EXISTS SavePlayerProgress;
DROP PROCEDURE IF EXISTS LoadPlayerSkills;
DROP PROCEDURE IF EXISTS SavePlayerSkill;
DROP PROCEDURE IF EXISTS ResetPlayerSkills;
DROP PROCEDURE IF EXISTS LoadGuilds;
DROP PROCEDURE IF EXISTS LoadGuildMembers;
DROP PROCEDURE IF EXISTS GuildCreate;
DROP PROCEDURE IF EXISTS GuildMemberSet;
DROP PROCEDURE IF EXISTS GuildMemberRemove;
DROP PROCEDURE IF EXISTS GuildTransfer;
DROP PROCEDURE IF EXISTS GuildSetInfo;
DROP PROCEDURE IF EXISTS GuildDisband;
DROP PROCEDURE IF EXISTS GuildMemberSeen;
DROP PROCEDURE IF EXISTS LoadAuctionListings;
DROP PROCEDURE IF EXISTS LoadAuctionSettlements;
DROP PROCEDURE IF EXISTS LoadAuctionPrices;
DROP PROCEDURE IF EXISTS AuctionMaxIds;
DROP PROCEDURE IF EXISTS AuctionRegister;
DROP PROCEDURE IF EXISTS AuctionBuy;
DROP PROCEDURE IF EXISTS AuctionClose;
DROP PROCEDURE IF EXISTS SettlementClaim;
DROP PROCEDURE IF EXISTS AuctionCancel;
DROP PROCEDURE IF EXISTS AuctionExpire;
DROP PROCEDURE IF EXISTS AuctionAudit;
DROP PROCEDURE IF EXISTS LoadEnchantGrace;
DROP PROCEDURE IF EXISTS EnchantTransaction;

-- New procedures: identical to the procedure block at the end of setup.sql.
DELIMITER $$

DROP PROCEDURE IF EXISTS P_USER_SELECT_BY_NAME$$
CREATE PROCEDURE P_USER_SELECT_BY_NAME(IN in_Name VARCHAR(20))
BEGIN
    -- One row (the character) or no row (no such name). Names compare case-insensitively.
    SELECT UserID, Name, PosX, PosY, Level, Exp, Adena, Alignment, PkCount
    FROM User WHERE Name = in_Name;
END$$

DROP PROCEDURE IF EXISTS P_USER_CREATE$$
CREATE PROCEDURE P_USER_CREATE(IN in_Name VARCHAR(20), IN in_Password VARCHAR(20))
BEGIN
    -- Created = 1 only when this call inserted the row. A concurrent first login with the same
    -- name waits on the unique key, gets ER_DUP_ENTRY after the winner commits and returns the
    -- winner's UserID with Created = 0. Any other error is raised to the caller.
    DECLARE v_Created TINYINT UNSIGNED DEFAULT 1;
    DECLARE CONTINUE HANDLER FOR 1062 SET v_Created = 0;
    INSERT INTO User (Name, Password) VALUES (in_Name, in_Password);
    SELECT UserID, v_Created AS Created FROM User WHERE Name = in_Name FOR SHARE;
END$$

DROP PROCEDURE IF EXISTS P_USER_SELECT$$
CREATE PROCEDURE P_USER_SELECT(IN in_UserID INT)
BEGIN
    SELECT UserID, Name, PosX, PosY, Level, Exp, Adena, Alignment, PkCount
    FROM User WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_USER_UPSERT$$
CREATE PROCEDURE P_USER_UPSERT(IN in_UserID INT, IN in_PosX INT, IN in_PosY INT,
                               IN in_Level TINYINT UNSIGNED, IN in_Exp INT)
BEGIN
    UPDATE User SET PosX = CASE WHEN in_PosX BETWEEN 0 AND 1999 THEN in_PosX ELSE PosX END,
                    PosY = CASE WHEN in_PosY BETWEEN 0 AND 1999 THEN in_PosY ELSE PosY END,
                    Level = in_Level, Exp = in_Exp WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_INVENTORY_SELECT$$
CREATE PROCEDURE P_INVENTORY_SELECT(IN in_UserID INT)
BEGIN
    SELECT SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra
    FROM UserInventory WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_INVENTORY_UPSERT$$
CREATE PROCEDURE P_INVENTORY_UPSERT(IN in_UserID INT, IN in_SlotIndex SMALLINT, IN in_ItemID SMALLINT,
                                    IN in_Count SMALLINT, IN in_Equipped TINYINT UNSIGNED,
                                    IN in_Enchant INT UNSIGNED, IN in_Extra BIGINT)
BEGIN
    INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
    VALUES (in_UserID, in_SlotIndex, in_ItemID, in_Count, in_Equipped, in_Enchant, in_Extra)
    ON DUPLICATE KEY UPDATE ItemID = in_ItemID, `Count` = in_Count, Equipped = in_Equipped,
                            Enchant = in_Enchant, Extra = in_Extra;
END$$

DROP PROCEDURE IF EXISTS P_INVENTORY_DELETE$$
CREATE PROCEDURE P_INVENTORY_DELETE(IN in_UserID INT, IN in_SlotIndex SMALLINT)
BEGIN
    DELETE FROM UserInventory WHERE UserID = in_UserID AND SlotIndex = in_SlotIndex;
END$$

DROP PROCEDURE IF EXISTS P_INVENTORY_SWAP$$
CREATE PROCEDURE P_INVENTORY_SWAP(
    IN in_UserID INT,
    IN in_SlotA SMALLINT, IN in_HasA TINYINT UNSIGNED, IN in_ItemA SMALLINT,
    IN in_CountA SMALLINT, IN in_EquippedA TINYINT UNSIGNED,
    IN in_SlotB SMALLINT, IN in_HasB TINYINT UNSIGNED, IN in_ItemB SMALLINT,
    IN in_CountB SMALLINT, IN in_EquippedB TINYINT UNSIGNED,
    IN in_EnchantA INT UNSIGNED, IN in_EnchantB INT UNSIGNED,
    IN in_ExtraA BIGINT, IN in_ExtraB BIGINT)
BEGIN
    DECLARE v_slot SMALLINT;
    DECLARE v_has TINYINT UNSIGNED;
    DECLARE v_item SMALLINT;
    DECLARE v_count SMALLINT;
    DECLARE v_equipped TINYINT UNSIGNED;
    DECLARE v_enchant INT UNSIGNED;
    DECLARE v_extra BIGINT;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    IF in_SlotA > in_SlotB THEN
        SET v_slot = in_SlotA, v_has = in_HasA, v_item = in_ItemA, v_count = in_CountA,
            v_equipped = in_EquippedA, v_enchant = in_EnchantA, v_extra = in_ExtraA;
        SET in_SlotA = in_SlotB, in_HasA = in_HasB, in_ItemA = in_ItemB, in_CountA = in_CountB,
            in_EquippedA = in_EquippedB, in_EnchantA = in_EnchantB, in_ExtraA = in_ExtraB;
        SET in_SlotB = v_slot, in_HasB = v_has, in_ItemB = v_item, in_CountB = v_count,
            in_EquippedB = v_equipped, in_EnchantB = v_enchant, in_ExtraB = v_extra;
    END IF;
    START TRANSACTION;
    IF in_HasA = 1 THEN
        INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
        VALUES (in_UserID, in_SlotA, in_ItemA, in_CountA, in_EquippedA, in_EnchantA, in_ExtraA)
        ON DUPLICATE KEY UPDATE ItemID = in_ItemA, `Count` = in_CountA, Equipped = in_EquippedA,
                                Enchant = in_EnchantA, Extra = in_ExtraA;
    ELSE
        DELETE FROM UserInventory WHERE UserID = in_UserID AND SlotIndex = in_SlotA;
    END IF;
    IF in_HasB = 1 THEN
        INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
        VALUES (in_UserID, in_SlotB, in_ItemB, in_CountB, in_EquippedB, in_EnchantB, in_ExtraB)
        ON DUPLICATE KEY UPDATE ItemID = in_ItemB, `Count` = in_CountB, Equipped = in_EquippedB,
                                Enchant = in_EnchantB, Extra = in_ExtraB;
    ELSE
        DELETE FROM UserInventory WHERE UserID = in_UserID AND SlotIndex = in_SlotB;
    END IF;
    COMMIT;
END$$

DROP PROCEDURE IF EXISTS P_USER_ADENA_UPSERT$$
CREATE PROCEDURE P_USER_ADENA_UPSERT(IN in_UserID INT, IN in_Adena INT)
BEGIN
    UPDATE User SET Adena = CASE WHEN in_Adena >= 0 THEN in_Adena ELSE Adena END
    WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_USER_ALIGNMENT_UPSERT$$
CREATE PROCEDURE P_USER_ALIGNMENT_UPSERT(IN in_UserID INT, IN in_Alignment SMALLINT, IN in_PkCount SMALLINT)
BEGIN
    UPDATE User SET Alignment = in_Alignment, PkCount = in_PkCount WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_SHOP_TRADE$$
CREATE PROCEDURE P_SHOP_TRADE(IN in_UserID INT, IN in_Adena INT, IN in_SlotIndex SMALLINT,
                              IN in_HasItem TINYINT UNSIGNED, IN in_ItemID SMALLINT,
                              IN in_Count SMALLINT, IN in_Equipped TINYINT UNSIGNED,
                              IN in_Enchant INT UNSIGNED)
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    UPDATE User SET Adena = in_Adena WHERE UserID = in_UserID;
    IF in_HasItem = 1 THEN
        INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
        VALUES (in_UserID, in_SlotIndex, in_ItemID, in_Count, in_Equipped, in_Enchant, 0)
        ON DUPLICATE KEY UPDATE ItemID = in_ItemID, `Count` = in_Count, Equipped = in_Equipped,
                                Enchant = in_Enchant, Extra = 0;
    ELSE
        DELETE FROM UserInventory WHERE UserID = in_UserID AND SlotIndex = in_SlotIndex;
    END IF;
    COMMIT;
END$$

DROP PROCEDURE IF EXISTS P_USER_PROGRESS_SELECT$$
CREATE PROCEDURE P_USER_PROGRESS_SELECT(IN in_UserID INT)
BEGIN
    SELECT Data FROM UserProgress WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_USER_PROGRESS_UPSERT$$
CREATE PROCEDURE P_USER_PROGRESS_UPSERT(IN in_UserID INT, IN in_Data VARBINARY(4000))
BEGIN
    INSERT INTO UserProgress (UserID, Data) VALUES (in_UserID, in_Data)
    ON DUPLICATE KEY UPDATE Data = in_Data, UpdatedAt = UTC_TIMESTAMP(6);
END$$

DROP PROCEDURE IF EXISTS P_USER_SKILL_SELECT$$
CREATE PROCEDURE P_USER_SKILL_SELECT(IN in_UserID INT)
BEGIN
    SELECT SkillID, Level, Pick1, Pick2 FROM UserSkill WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_USER_SKILL_UPSERT$$
CREATE PROCEDURE P_USER_SKILL_UPSERT(IN in_UserID INT, IN in_SkillID SMALLINT,
                                     IN in_Level TINYINT UNSIGNED, IN in_Pick1 TINYINT UNSIGNED,
                                     IN in_Pick2 TINYINT UNSIGNED)
BEGIN
    IF in_Level <= 1 AND in_Pick1 = 0 AND in_Pick2 = 0 THEN
        DELETE FROM UserSkill WHERE UserID = in_UserID AND SkillID = in_SkillID;
    ELSE
        INSERT INTO UserSkill (UserID, SkillID, Level, Pick1, Pick2)
        VALUES (in_UserID, in_SkillID, in_Level, in_Pick1, in_Pick2)
        ON DUPLICATE KEY UPDATE Level = in_Level, Pick1 = in_Pick1, Pick2 = in_Pick2,
                                UpdatedAt = UTC_TIMESTAMP(6);
    END IF;
END$$

DROP PROCEDURE IF EXISTS P_USER_SKILL_DELETE$$
CREATE PROCEDURE P_USER_SKILL_DELETE(IN in_UserID INT)
BEGIN
    DELETE FROM UserSkill WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_SELECT_ALL$$
CREATE PROCEDURE P_GUILD_SELECT_ALL()
BEGIN
    SELECT GuildID, Name, Emblem, Notice, LeaderID FROM Guild;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_MEMBER_SELECT_ALL$$
CREATE PROCEDURE P_GUILD_MEMBER_SELECT_ALL()
BEGIN
    SELECT m.GuildID, m.UserID, m.`Rank`, u.Name, u.Level, m.LastSeen
    FROM GuildMember m JOIN User u ON u.UserID = m.UserID;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_CREATE$$
CREATE PROCEDURE P_GUILD_CREATE(IN in_UserID INT, IN in_Adena INT, IN in_GuildID INT,
                                IN in_Name VARCHAR(16), IN in_NameKey VARCHAR(16),
                                IN in_Emblem TINYINT UNSIGNED)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF NOT EXISTS (SELECT 1 FROM Guild WHERE NameKey = in_NameKey OR GuildID = in_GuildID)
       AND NOT EXISTS (SELECT 1 FROM GuildMember WHERE UserID = in_UserID) THEN
        UPDATE User SET Adena = in_Adena WHERE UserID = in_UserID;
        INSERT INTO Guild (GuildID, Name, NameKey, Emblem, LeaderID, CreatedAt)
        VALUES (in_GuildID, in_Name, in_NameKey, in_Emblem, in_UserID, UNIX_TIMESTAMP());
        INSERT INTO GuildMember (UserID, GuildID, `Rank`) VALUES (in_UserID, in_GuildID, 0);
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_MEMBER_UPSERT$$
CREATE PROCEDURE P_GUILD_MEMBER_UPSERT(IN in_GuildID INT, IN in_UserID INT, IN in_Rank TINYINT UNSIGNED)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF EXISTS (SELECT 1 FROM Guild WHERE GuildID = in_GuildID) THEN
        IF EXISTS (SELECT 1 FROM GuildMember WHERE UserID = in_UserID AND GuildID = in_GuildID) THEN
            UPDATE GuildMember SET `Rank` = in_Rank WHERE UserID = in_UserID;
            SET v_ok = 1;
        ELSEIF NOT EXISTS (SELECT 1 FROM GuildMember WHERE UserID = in_UserID) THEN
            INSERT INTO GuildMember (UserID, GuildID, `Rank`) VALUES (in_UserID, in_GuildID, in_Rank);
            SET v_ok = 1;
        END IF;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_MEMBER_DELETE$$
CREATE PROCEDURE P_GUILD_MEMBER_DELETE(IN in_GuildID INT, IN in_UserID INT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DELETE FROM GuildMember WHERE UserID = in_UserID AND GuildID = in_GuildID;
    IF ROW_COUNT() > 0 THEN SET v_ok = 1; END IF;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_TRANSFER$$
CREATE PROCEDURE P_GUILD_TRANSFER(IN in_GuildID INT, IN in_FromUserID INT, IN in_ToUserID INT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE v_firstRank TINYINT UNSIGNED DEFAULT NULL;
    DECLARE v_secondRank TINYINT UNSIGNED DEFAULT NULL;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    -- Lock both member rows in the same UserID order for every transfer.
    IF in_FromUserID <> in_ToUserID THEN
        SELECT `Rank` INTO v_firstRank FROM GuildMember
        WHERE UserID = LEAST(in_FromUserID, in_ToUserID) AND GuildID = in_GuildID FOR UPDATE;
        SELECT `Rank` INTO v_secondRank FROM GuildMember
        WHERE UserID = GREATEST(in_FromUserID, in_ToUserID) AND GuildID = in_GuildID FOR UPDATE;
    END IF;
    IF v_firstRank IS NOT NULL AND v_secondRank IS NOT NULL THEN
        UPDATE GuildMember SET `Rank` = CASE WHEN UserID = in_FromUserID THEN 1 ELSE 0 END
        WHERE UserID IN (in_FromUserID, in_ToUserID) AND GuildID = in_GuildID;
        UPDATE Guild SET LeaderID = in_ToUserID WHERE GuildID = in_GuildID;
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_UPSERT$$
CREATE PROCEDURE P_GUILD_UPSERT(IN in_GuildID INT, IN in_Emblem TINYINT UNSIGNED,
                                IN in_Notice VARCHAR(40))
BEGIN
    UPDATE Guild SET Emblem = in_Emblem, Notice = in_Notice WHERE GuildID = in_GuildID;
    SELECT EXISTS(SELECT 1 FROM Guild WHERE GuildID = in_GuildID) AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_DELETE$$
CREATE PROCEDURE P_GUILD_DELETE(IN in_GuildID INT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DELETE FROM Guild WHERE GuildID = in_GuildID;
    IF ROW_COUNT() > 0 THEN SET v_ok = 1; END IF;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_MEMBER_SEEN_UPSERT$$
CREATE PROCEDURE P_GUILD_MEMBER_SEEN_UPSERT(IN in_UserID INT, IN in_LastSeen BIGINT)
BEGIN
    UPDATE GuildMember SET LastSeen = in_LastSeen WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_LISTING_SELECT_ALL$$
CREATE PROCEDURE P_AUCTION_LISTING_SELECT_ALL()
BEGIN
    SELECT ListingID, SellerID, ItemID, `Count`, Enchant, Extra, Price, Deposit, CreatedAt, ExpiresAt
    FROM AuctionListing WHERE Status = 0;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_SETTLEMENT_SELECT_ALL$$
CREATE PROCEDURE P_AUCTION_SETTLEMENT_SELECT_ALL()
BEGIN
    SELECT SettlementID, UserID, Kind, Adena, ItemID, `Count`, Enchant, Extra,
           ListingID, Reason, CreatedAt
    FROM AuctionSettlement WHERE ClaimedAt IS NULL;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_PRICE_SELECT$$
CREATE PROCEDURE P_AUCTION_PRICE_SELECT(IN in_Since BIGINT)
BEGIN
    SELECT ItemID, Price, SoldAt FROM AuctionPriceHistory WHERE SoldAt >= in_Since ORDER BY SoldAt;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_MAX_ID_SELECT$$
CREATE PROCEDURE P_AUCTION_MAX_ID_SELECT()
BEGIN
    SELECT COALESCE((SELECT MAX(ListingID) FROM AuctionListing), 0) AS MaxListingID,
           COALESCE((SELECT MAX(SettlementID) FROM AuctionSettlement), 0) AS MaxSettlementID,
           COALESCE((SELECT MAX(GuildID) FROM Guild), 0) AS MaxGuildID;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_REGISTER$$
CREATE PROCEDURE P_AUCTION_REGISTER(
    IN in_SellerID INT, IN in_Adena INT, IN in_SlotIndex SMALLINT,
    IN in_HasItem TINYINT UNSIGNED, IN in_SlotItemID SMALLINT, IN in_SlotCount SMALLINT,
    IN in_Equipped TINYINT UNSIGNED, IN in_Enchant INT UNSIGNED, IN in_Extra BIGINT,
    IN in_ListingID BIGINT, IN in_ItemID SMALLINT, IN in_Count SMALLINT,
    IN in_ListingEnchant INT UNSIGNED, IN in_ListingExtra BIGINT, IN in_Price INT,
    IN in_Deposit INT, IN in_CreatedAt BIGINT, IN in_ExpiresAt BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF NOT EXISTS (SELECT 1 FROM AuctionListing WHERE ListingID = in_ListingID) THEN
        UPDATE User SET Adena = in_Adena WHERE UserID = in_SellerID;
        IF in_HasItem = 1 THEN
            UPDATE UserInventory SET ItemID = in_SlotItemID, `Count` = in_SlotCount,
                Equipped = in_Equipped, Enchant = in_Enchant, Extra = in_Extra
            WHERE UserID = in_SellerID AND SlotIndex = in_SlotIndex;
        ELSE
            DELETE FROM UserInventory WHERE UserID = in_SellerID AND SlotIndex = in_SlotIndex;
        END IF;
        INSERT INTO AuctionListing
            (ListingID, SellerID, ItemID, `Count`, Enchant, Extra, Price, Deposit,
             CreatedAt, ExpiresAt, Status)
        VALUES (in_ListingID, in_SellerID, in_ItemID, in_Count, in_ListingEnchant, in_ListingExtra,
                in_Price, in_Deposit, in_CreatedAt, in_ExpiresAt, 0);
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_BUY$$
CREATE PROCEDURE P_AUCTION_BUY(
    IN in_BuyerID INT, IN in_Adena INT, IN in_ListingID BIGINT, IN in_Price INT,
    IN in_BuyerSettlementID BIGINT, IN in_SellerSettlementID BIGINT, IN in_SellerGets INT,
    IN in_Now BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE v_seller INT;
    DECLARE v_itemId SMALLINT;
    DECLARE v_count SMALLINT;
    DECLARE v_enchant INT UNSIGNED;
    DECLARE v_extra BIGINT;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    UPDATE AuctionListing SET Status = 1, BuyerID = in_BuyerID, ClosedAt = in_Now
    WHERE ListingID = in_ListingID AND Status = 0 AND Price = in_Price;
    IF ROW_COUNT() = 1 THEN
        SELECT SellerID, ItemID, `Count`, Enchant, Extra
        INTO v_seller, v_itemId, v_count, v_enchant, v_extra
        FROM AuctionListing WHERE ListingID = in_ListingID;
        UPDATE User SET Adena = in_Adena WHERE UserID = in_BuyerID;
        INSERT INTO AuctionSettlement
            (SettlementID, UserID, Kind, Adena, ItemID, `Count`, Enchant, Extra,
             ListingID, Reason, CreatedAt)
        VALUES (in_BuyerSettlementID, in_BuyerID, 2, 0, v_itemId, v_count, v_enchant, v_extra,
                in_ListingID, 1, in_Now);
        INSERT INTO AuctionSettlement
            (SettlementID, UserID, Kind, Adena, ItemID, `Count`, Enchant, Extra,
             ListingID, Reason, CreatedAt)
        VALUES (in_SellerSettlementID, v_seller, 1, in_SellerGets, 0, 0, 0, 0,
                in_ListingID, 0, in_Now);
        INSERT INTO AuctionPriceHistory (ItemID, Price, SoldAt)
        VALUES (v_itemId, in_Price DIV CASE WHEN v_count > 0 THEN v_count ELSE 1 END, in_Now);
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_CLOSE$$
CREATE PROCEDURE P_AUCTION_CLOSE(IN in_ListingID BIGINT, IN in_Status TINYINT UNSIGNED,
                                 IN in_SettlementID BIGINT, IN in_Now BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF in_Status IN (2, 3) THEN
        UPDATE AuctionListing SET Status = in_Status, ClosedAt = in_Now
        WHERE ListingID = in_ListingID AND Status = 0;
        IF ROW_COUNT() = 1 THEN
            INSERT INTO AuctionSettlement
                (SettlementID, UserID, Kind, Adena, ItemID, `Count`, Enchant,
                 Extra, ListingID, Reason, CreatedAt)
            SELECT in_SettlementID, SellerID, 2, 0, ItemID, `Count`, Enchant,
                   Extra, ListingID, in_Status, in_Now
            FROM AuctionListing WHERE ListingID = in_ListingID;
            SET v_ok = 1;
        END IF;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_SETTLEMENT_CLAIM$$
CREATE PROCEDURE P_AUCTION_SETTLEMENT_CLAIM(
    IN in_UserID INT, IN in_SettlementID BIGINT, IN in_Adena INT,
    IN in_SlotIndex SMALLINT, IN in_HasItem TINYINT UNSIGNED,
    IN in_SlotItemID SMALLINT, IN in_SlotCount SMALLINT, IN in_Equipped TINYINT UNSIGNED,
    IN in_Enchant INT UNSIGNED, IN in_Extra BIGINT, IN in_Now BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE v_kind TINYINT UNSIGNED;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    UPDATE AuctionSettlement SET ClaimedAt = in_Now
    WHERE SettlementID = in_SettlementID AND UserID = in_UserID AND ClaimedAt IS NULL;
    IF ROW_COUNT() = 1 THEN
        SELECT Kind INTO v_kind FROM AuctionSettlement WHERE SettlementID = in_SettlementID;
        UPDATE User SET Adena = in_Adena WHERE UserID = in_UserID;
        IF v_kind = 2 AND in_HasItem = 1 THEN
            INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
            VALUES (in_UserID, in_SlotIndex, in_SlotItemID, in_SlotCount, in_Equipped, in_Enchant, in_Extra)
            ON DUPLICATE KEY UPDATE ItemID = in_SlotItemID, `Count` = in_SlotCount,
                                    Equipped = in_Equipped, Enchant = in_Enchant, Extra = in_Extra;
        END IF;
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_CANCEL$$
CREATE PROCEDURE P_AUCTION_CANCEL(IN in_ListingID BIGINT, IN in_SettlementID BIGINT, IN in_Now BIGINT)
BEGIN
    CALL P_AUCTION_CLOSE(in_ListingID, 2, in_SettlementID, in_Now);
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_EXPIRE$$
CREATE PROCEDURE P_AUCTION_EXPIRE(IN in_ListingID BIGINT, IN in_SettlementID BIGINT, IN in_Now BIGINT)
BEGIN
    CALL P_AUCTION_CLOSE(in_ListingID, 3, in_SettlementID, in_Now);
END$$

DROP PROCEDURE IF EXISTS P_AUCTION_AUDIT$$
CREATE PROCEDURE P_AUCTION_AUDIT()
BEGIN
    SELECT COALESCE((SELECT SUM(CAST(Adena AS SIGNED)) FROM User), 0) AS WalletAdena,
           COALESCE((SELECT SUM(CAST(Adena AS SIGNED)) FROM AuctionSettlement
                     WHERE ClaimedAt IS NULL AND Kind = 1), 0) AS SettleAdena,
           COALESCE((SELECT SUM(CAST(`Count` AS SIGNED)) FROM UserInventory), 0) AS InventoryUnits,
           COALESCE((SELECT SUM(CAST(`Count` AS SIGNED)) FROM AuctionListing WHERE Status = 0), 0) AS ListingUnits,
           COALESCE((SELECT SUM(CAST(`Count` AS SIGNED)) FROM AuctionSettlement
                     WHERE ClaimedAt IS NULL AND Kind = 2), 0) AS SettleUnits,
           COALESCE((SELECT SUM(CAST(Deposit AS SIGNED)) FROM AuctionListing), 0)
             + COALESCE((SELECT SUM(CAST(GREATEST(Price DIV 20, 1) AS SIGNED))
                         FROM AuctionListing WHERE Status = 1), 0) AS FeesAndDeposits;
END$$

DROP PROCEDURE IF EXISTS P_ENCHANT_GRACE_SELECT$$
CREATE PROCEDURE P_ENCHANT_GRACE_SELECT(IN in_UserID INT)
BEGIN
    SELECT ItemID, CurrentEnchant, Failures FROM EnchantGrace WHERE UserID = in_UserID;
END$$

DROP PROCEDURE IF EXISTS P_ENCHANT_TRADE$$
CREATE PROCEDURE P_ENCHANT_TRADE(IN in_UserID INT, IN in_Slots LONGTEXT,
    IN in_ItemID SMALLINT, IN in_Level INT UNSIGNED, IN in_Name VARCHAR(20))
BEGIN
    DECLARE v_i INT DEFAULT 0;
    DECLARE v_count INT;
    DECLARE v_slot SMALLINT;
    DECLARE v_matches INT;
    DECLARE v_lockedSlot SMALLINT;
    DECLARE v_first INT DEFAULT 0;
    DECLARE v_path VARCHAR(32);
    DECLARE v_data JSON;
    DECLARE v_token CHAR(36);
    DECLARE v_graceLevel INT UNSIGNED;
    DECLARE v_graceLimit INT;
    DECLARE v_graceBefore INT;
    DECLARE v_graceAfter INT;
    DECLARE v_lockedFailures INT;
    DECLARE v_success BOOLEAN;
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_matches = 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    SET v_count = JSON_LENGTH(in_Slots);
    IF v_count NOT BETWEEN 2 AND 3 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid enchant slots'; END IF;
    START TRANSACTION;
    SET v_data = JSON_EXTRACT(in_Slots, '$[1]');
    SET v_graceLevel = JSON_EXTRACT(v_data, '$.level');
    SET v_graceLimit = COALESCE(JSON_EXTRACT(v_data, '$.graceLimit'), 0);
    IF JSON_EXTRACT(v_data, '$.item') <> in_ItemID OR
       (v_graceLevel BETWEEN 6 AND 10 AND JSON_EXTRACT(v_data, '$.graceLimit') IS NULL) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Missing enchant grace state';
    END IF;
    IF v_graceLimit > 0 THEN
        SET v_graceBefore = JSON_EXTRACT(v_data, '$.graceBefore');
        SET v_graceAfter = JSON_EXTRACT(v_data, '$.graceAfter');
        SET v_success = JSON_EXTRACT(v_data, '$.keep') = 1 AND JSON_EXTRACT(v_data, '$.newLevel') = v_graceLevel + 1;
        IF v_graceLimit > 65535 OR v_graceBefore IS NULL OR v_graceAfter IS NULL OR
           v_graceBefore NOT BETWEEN 0 AND 65535 OR v_graceAfter NOT BETWEEN 0 AND 65535 OR
           (v_success AND v_graceAfter <> 0) OR
           (NOT v_success AND (v_graceBefore + 1 >= v_graceLimit OR v_graceAfter <> v_graceBefore + 1)) THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid enchant grace transition';
        END IF;
        -- 같은 키의 첫 시도도 행 잠금으로 직렬화한다. 실패 시 INSERT까지 롤백한다.
        INSERT INTO EnchantGrace(UserID, ItemID, CurrentEnchant, Failures)
          VALUES(in_UserID, in_ItemID, v_graceLevel, 0) ON DUPLICATE KEY UPDATE Failures = Failures;
        SELECT Failures INTO v_lockedFailures FROM EnchantGrace
          WHERE UserID = in_UserID AND ItemID = in_ItemID AND CurrentEnchant = v_graceLevel FOR UPDATE;
        IF v_lockedFailures <> v_graceBefore THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale enchant grace'; END IF;
        UPDATE EnchantGrace SET Failures = v_graceAfter
          WHERE UserID = in_UserID AND ItemID = in_ItemID AND CurrentEnchant = v_graceLevel;
    END IF;
    WHILE v_i < v_count DO
        SET v_path = CONCAT('$[', v_i, ']');
        SET v_data = JSON_EXTRACT(in_Slots, v_path);
        SET v_slot = JSON_EXTRACT(v_data, '$.slot');
        SET v_matches = 1;
        SELECT SlotIndex INTO v_lockedSlot FROM UserInventory
          WHERE UserID = in_UserID AND SlotIndex = v_slot
            AND ItemID = JSON_EXTRACT(v_data, '$.item') AND `Count` = JSON_EXTRACT(v_data, '$.count')
            AND Equipped = JSON_EXTRACT(v_data, '$.equipped') AND Enchant = JSON_EXTRACT(v_data, '$.level')
            AND Extra = JSON_EXTRACT(v_data, '$.extra') FOR UPDATE;
        IF v_matches <> 1 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale enchant inventory'; END IF;
        IF JSON_EXTRACT(v_data, '$.keep') = 0 THEN
            DELETE FROM UserInventory WHERE UserID = in_UserID AND SlotIndex = v_slot;
        ELSE
            UPDATE UserInventory SET `Count` = JSON_EXTRACT(v_data, '$.newCount'), Enchant = JSON_EXTRACT(v_data, '$.newLevel')
              WHERE UserID = in_UserID AND SlotIndex = v_slot;
        END IF;
        SET v_i = v_i + 1;
    END WHILE;
    -- 최초 달성 기록: 서버가 문턱(시트 EnchantConst.first_notice_level) 이상 성공에만 도달 단계를, 아니면 0을 넘긴다.
    -- 예전엔 11을 박아 두어 시트 값을 낮추면 MySQL만 기록을 빠뜨렸다(인메모리 DB는 시트 값을 따랐다).
    IF in_Level > 0 THEN
        SET v_token = UUID();
        INSERT INTO EnchantFirstAchievement(ItemID, Enchant, UserID, Name, ClaimToken)
          VALUES(in_ItemID, in_Level, in_UserID, in_Name, v_token)
          ON DUPLICATE KEY UPDATE ClaimToken = ClaimToken;
        SELECT ClaimToken = v_token INTO v_first FROM EnchantFirstAchievement WHERE ItemID = in_ItemID AND Enchant = in_Level;
    END IF;
    COMMIT;
    SELECT 1 AS Ok, v_first AS IsFirst;
END$$

DELIMITER ;
