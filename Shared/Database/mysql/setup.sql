-- Fresh MySQL 8.4 installation. Run with the mysql CLI as the schema owner:
-- mysql --host=<endpoint> --user=<admin> --ssl-mode=VERIFY_IDENTITY
--       --ssl-ca=<rds-ca.pem> --password < Shared/Database/mysql/setup.sql
-- This file creates the schema but does not create users or store credentials.
-- An existing pre-UserID schema (table Players) is converted with migrate_user_id.sql instead;
-- this script refuses to run next to it.
--
-- Naming (REIW DB convention): procedures P_<NOUN>_<VERB> (SELECT / SELECT_ALL / UPSERT / DELETE for
-- plain CRUD, a specific verb for a multi-statement transaction), parameters in_<PascalCase>,
-- PascalCase singular table names and PascalCase columns, IDs end in ID. A character is found by
-- name only in P_USER_SELECT_BY_NAME and P_USER_CREATE; every other procedure takes in_UserID.
CREATE DATABASE IF NOT EXISTS GameServerDB CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE GameServerDB;

DELIMITER $$
DROP PROCEDURE IF EXISTS P_SETUP_CHECK$$
CREATE PROCEDURE P_SETUP_CHECK()
BEGIN
    -- New tables created next to the old ones would start the server on an empty character list.
    IF EXISTS (SELECT 1 FROM information_schema.TABLES
               WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'Players') THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'Old schema (Players) found: run migrate_user_id.sql instead of setup.sql';
    END IF;
END$$
DELIMITER ;
CALL P_SETUP_CHECK();
DROP PROCEDURE P_SETUP_CHECK;

CREATE TABLE IF NOT EXISTS User (
    UserID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    Name VARCHAR(20) NOT NULL,
    Password VARCHAR(20) NOT NULL,
    PosX INT NOT NULL DEFAULT 0,
    PosY INT NOT NULL DEFAULT 0,
    Level TINYINT UNSIGNED NOT NULL DEFAULT 1,
    Exp INT NOT NULL DEFAULT 0,
    Adena INT NOT NULL DEFAULT 0,
    Alignment SMALLINT NOT NULL DEFAULT 0,
    PkCount SMALLINT NOT NULL DEFAULT 0,
    UNIQUE KEY UQ_User_Name (Name)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS UserInventory (
    UserID INT NOT NULL,
    SlotIndex SMALLINT NOT NULL,
    ItemID SMALLINT NOT NULL,
    `Count` SMALLINT NOT NULL,
    Equipped TINYINT UNSIGNED NOT NULL DEFAULT 0,
    Enchant INT UNSIGNED NOT NULL DEFAULT 0,
    Extra BIGINT NOT NULL DEFAULT 0,
    PRIMARY KEY (UserID, SlotIndex),
    CONSTRAINT FK_UserInventory_User FOREIGN KEY (UserID) REFERENCES User(UserID)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS UserProgress (
    UserID INT NOT NULL PRIMARY KEY,
    Data VARBINARY(4000) NOT NULL,
    UpdatedAt DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    CONSTRAINT FK_UserProgress_User FOREIGN KEY (UserID) REFERENCES User(UserID) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS UserSkill (
    UserID INT NOT NULL,
    SkillID SMALLINT NOT NULL,
    Level TINYINT UNSIGNED NOT NULL DEFAULT 1,
    Pick1 TINYINT UNSIGNED NOT NULL DEFAULT 0,
    Pick2 TINYINT UNSIGNED NOT NULL DEFAULT 0,
    UpdatedAt DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (UserID, SkillID),
    CONSTRAINT CK_UserSkill_Level CHECK (Level BETWEEN 1 AND 10),
    CONSTRAINT CK_UserSkill_Pick CHECK (Pick1 BETWEEN 0 AND 2 AND Pick2 BETWEEN 0 AND 2),
    CONSTRAINT FK_UserSkill_User FOREIGN KEY (UserID) REFERENCES User(UserID) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS Guild (
    GuildID INT NOT NULL PRIMARY KEY,
    Name VARCHAR(16) NOT NULL,
    NameKey VARCHAR(16) NOT NULL,
    Emblem TINYINT UNSIGNED NOT NULL DEFAULT 0,
    Notice VARCHAR(40) NOT NULL DEFAULT '',
    LeaderID INT NOT NULL,
    CreatedAt BIGINT NOT NULL DEFAULT 0,
    UNIQUE KEY UQ_Guild_NameKey (NameKey)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS GuildMember (
    UserID INT NOT NULL PRIMARY KEY,
    GuildID INT NOT NULL,
    `Rank` TINYINT UNSIGNED NOT NULL,
    LastSeen BIGINT NOT NULL DEFAULT 0,
    KEY IX_GuildMember_Guild (GuildID),
    CONSTRAINT CK_GuildMember_Rank CHECK (`Rank` BETWEEN 0 AND 3),
    CONSTRAINT FK_GuildMember_Guild FOREIGN KEY (GuildID) REFERENCES Guild(GuildID) ON DELETE CASCADE,
    CONSTRAINT FK_GuildMember_User FOREIGN KEY (UserID) REFERENCES User(UserID)
) ENGINE=InnoDB;

-- 결사 성장(Shared/Document/GUILD.md): 진행 덩어리 · 금고 · 기록 · 게시판. 결사를 지우면 함께 지워진다.
CREATE TABLE IF NOT EXISTS GuildProgress (
    GuildID INT NOT NULL PRIMARY KEY,
    Seals INT UNSIGNED NOT NULL DEFAULT 0,
    Data VARBINARY(16000) NULL,
    UpdatedAt BIGINT NOT NULL DEFAULT 0,
    CONSTRAINT FK_GuildProgress_Guild FOREIGN KEY (GuildID) REFERENCES Guild(GuildID) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS GuildLog (
    GuildLogID BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    GuildID INT NOT NULL,
    At BIGINT NOT NULL,
    Kind TINYINT UNSIGNED NOT NULL,
    Value INT NOT NULL DEFAULT 0,
    Who VARCHAR(20) NOT NULL DEFAULT '',
    What VARCHAR(30) NOT NULL DEFAULT '',
    KEY IX_GuildLog_Guild (GuildID, GuildLogID),
    CONSTRAINT FK_GuildLog_Guild FOREIGN KEY (GuildID) REFERENCES Guild(GuildID) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS GuildPost (
    GuildPostID BIGINT NOT NULL PRIMARY KEY,
    GuildID INT NOT NULL,
    AuthorID INT NOT NULL,
    Author VARCHAR(20) NOT NULL,
    Category TINYINT UNSIGNED NOT NULL,
    Pinned TINYINT UNSIGNED NOT NULL DEFAULT 0,
    Title VARCHAR(16) NOT NULL,
    Body VARCHAR(60) NOT NULL,
    At BIGINT NOT NULL,
    KEY IX_GuildPost_Guild (GuildID),
    CONSTRAINT CK_GuildPost_Category CHECK (Category BETWEEN 0 AND 4),
    CONSTRAINT FK_GuildPost_Guild FOREIGN KEY (GuildID) REFERENCES Guild(GuildID) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS AuctionListing (
    ListingID BIGINT NOT NULL PRIMARY KEY,
    SellerID INT NOT NULL,
    ItemID SMALLINT NOT NULL,
    `Count` SMALLINT NOT NULL,
    Enchant INT UNSIGNED NOT NULL DEFAULT 0,
    Extra BIGINT NOT NULL DEFAULT 0,
    Price INT NOT NULL,
    Deposit INT NOT NULL,
    CreatedAt BIGINT NOT NULL,
    ExpiresAt BIGINT NOT NULL,
    Status TINYINT UNSIGNED NOT NULL DEFAULT 0,
    BuyerID INT NULL,
    ClosedAt BIGINT NULL,
    KEY IX_AuctionListing_Status (Status, ExpiresAt),
    KEY IX_AuctionListing_Seller (SellerID, Status),
    CONSTRAINT FK_AuctionListing_User FOREIGN KEY (SellerID) REFERENCES User(UserID)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS AuctionSettlement (
    SettlementID BIGINT NOT NULL PRIMARY KEY,
    UserID INT NOT NULL,
    Kind TINYINT UNSIGNED NOT NULL,
    Adena INT NOT NULL DEFAULT 0,
    ItemID SMALLINT NOT NULL DEFAULT 0,
    `Count` SMALLINT NOT NULL DEFAULT 0,
    Enchant INT UNSIGNED NOT NULL DEFAULT 0,
    Extra BIGINT NOT NULL DEFAULT 0,
    ListingID BIGINT NOT NULL,
    Reason TINYINT UNSIGNED NOT NULL,
    CreatedAt BIGINT NOT NULL,
    ClaimedAt BIGINT NULL,
    KEY IX_AuctionSettlement_User (UserID, ClaimedAt),
    CONSTRAINT FK_AuctionSettlement_User FOREIGN KEY (UserID) REFERENCES User(UserID)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS AuctionPriceHistory (
    PriceHistoryID BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    ItemID SMALLINT NOT NULL,
    Price INT NOT NULL,
    SoldAt BIGINT NOT NULL,
    KEY IX_AuctionPriceHistory_Item (ItemID, SoldAt)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS EnchantFirstAchievement (
    ItemID SMALLINT NOT NULL,
    Enchant INT UNSIGNED NOT NULL,
    UserID INT NOT NULL,
    Name VARCHAR(20) NOT NULL,
    AchievedAt DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    ClaimToken CHAR(36) NOT NULL,
    PRIMARY KEY (ItemID, Enchant)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS EnchantGrace (
    UserID INT NOT NULL,
    ItemID SMALLINT NOT NULL,
    CurrentEnchant INT UNSIGNED NOT NULL,
    Failures SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (UserID, ItemID, CurrentEnchant),
    CONSTRAINT FK_EnchantGrace_User FOREIGN KEY (UserID) REFERENCES User(UserID) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ── Procedures ──────────────────────────────────────────────────────────────
-- migrate_user_id.sql repeats this block byte for byte (from the next line to the end of the file).
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

-- 경매 구매(한 트랜잭션): 목록 닫기 · 구매자 그로시 · 정산 두 줄(구매자 물건 · 판매자 대금) · 시세.
-- in_SettleExtraOr: 구매자 정산 줄 Extra에 더하는 비트 — 산 장비의 거래 이력(인스턴스 bit 9 = 512, 경제 1차 — 분해량 절반). 장비가 아니면 0(서버가 정한다).
DROP PROCEDURE IF EXISTS P_AUCTION_BUY$$
CREATE PROCEDURE P_AUCTION_BUY(
    IN in_BuyerID INT, IN in_Adena INT, IN in_ListingID BIGINT, IN in_Price INT,
    IN in_BuyerSettlementID BIGINT, IN in_SellerSettlementID BIGINT, IN in_SellerGets INT,
    IN in_Now BIGINT, IN in_SettleExtraOr BIGINT)
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
        VALUES (in_BuyerSettlementID, in_BuyerID, 2, 0, v_itemId, v_count, v_enchant, v_extra | IFNULL(in_SettleExtraOr, 0),
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

-- ── 경제 1차 제작 · 분해(Shared/Document/ECONOMY.md) ──────────────────────────
-- 칸 여럿 + 그로시를 한 트랜잭션으로(CDBConnection::SaveItemBatch). in_Slots = JSON 배열, 칸마다
--   {"slot":S, "has":0|1, "item":I, "count":N, "equipped":0|1, "enchant":E, "extra":X,          지금 — 낙관 검사(has=1이면 그 행과 같아야, 0이면 행이 없어야)
--    "keep":0|1, "newItem":I, "newCount":N, "newEquipped":0|1, "newEnchant":E, "newExtra":X}     바뀐 뒤 — keep=0이면 지운다
-- 그로시는 in_AdenaBefore ≠ in_AdenaAfter일 때만 본다: 지금 User.Adena = in_AdenaBefore여야 하고 in_AdenaAfter로 바꾼다.
-- 하나라도 어긋나면 SIGNAL(롤백 — 아무것도 바뀌지 않는다, 서버는 StorageError로 연결을 닫는다). 커밋하면 Ok = 1 한 줄.
DROP PROCEDURE IF EXISTS P_ITEM_BATCH_TRADE$$
CREATE PROCEDURE P_ITEM_BATCH_TRADE(IN in_UserID INT, IN in_Slots LONGTEXT,
    IN in_AdenaBefore INT, IN in_AdenaAfter INT)
BEGIN
    DECLARE v_i INT DEFAULT 0;
    DECLARE v_count INT;
    DECLARE v_data JSON;
    DECLARE v_rows INT;
    DECLARE v_adena INT;
    DECLARE v_slot SMALLINT;
    DECLARE v_has TINYINT UNSIGNED;
    DECLARE v_item SMALLINT;
    DECLARE v_cnt SMALLINT;
    DECLARE v_eq TINYINT UNSIGNED;
    DECLARE v_ench INT UNSIGNED;
    DECLARE v_extra BIGINT;
    DECLARE v_keep TINYINT UNSIGNED;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    IF in_Slots IS NULL OR NOT JSON_VALID(in_Slots) OR JSON_TYPE(in_Slots) <> 'ARRAY' THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid batch slots';
    END IF;
    SET v_count = JSON_LENGTH(in_Slots);
    IF v_count NOT BETWEEN 1 AND 50 OR in_AdenaBefore IS NULL OR in_AdenaAfter IS NULL
       OR in_AdenaBefore < 0 OR in_AdenaAfter < 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid batch slots';
    END IF;
    START TRANSACTION;
    IF in_AdenaBefore <> in_AdenaAfter THEN
        SET v_adena = NULL;
        SELECT Adena INTO v_adena FROM User WHERE UserID = in_UserID FOR UPDATE;
        IF v_adena IS NULL OR v_adena <> in_AdenaBefore THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale batch adena';
        END IF;
        UPDATE User SET Adena = in_AdenaAfter WHERE UserID = in_UserID;
    END IF;
    WHILE v_i < v_count DO
        SET v_data = JSON_EXTRACT(in_Slots, CONCAT('$[', v_i, ']'));
        SET v_slot = JSON_EXTRACT(v_data, '$.slot');
        SET v_has = JSON_EXTRACT(v_data, '$.has');
        IF v_slot IS NULL OR v_slot NOT BETWEEN 0 AND 49 OR v_has IS NULL THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid batch slots';
        END IF;
        IF v_has = 1 THEN
            SET v_item = JSON_EXTRACT(v_data, '$.item');
            SET v_cnt = JSON_EXTRACT(v_data, '$.count');
            SET v_eq = JSON_EXTRACT(v_data, '$.equipped');
            SET v_ench = JSON_EXTRACT(v_data, '$.enchant');
            SET v_extra = JSON_EXTRACT(v_data, '$.extra');
            SELECT COUNT(*) INTO v_rows FROM UserInventory
              WHERE UserID = in_UserID AND SlotIndex = v_slot AND ItemID = v_item AND `Count` = v_cnt
                AND Equipped = v_eq AND Enchant = v_ench AND Extra = v_extra FOR UPDATE;
        ELSE
            SELECT 1 - COUNT(*) INTO v_rows FROM UserInventory
              WHERE UserID = in_UserID AND SlotIndex = v_slot FOR UPDATE;
        END IF;
        IF v_rows <> 1 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale batch inventory'; END IF;
        SET v_keep = JSON_EXTRACT(v_data, '$.keep');
        IF v_keep = 1 THEN
            SET v_item = JSON_EXTRACT(v_data, '$.newItem');
            SET v_cnt = JSON_EXTRACT(v_data, '$.newCount');
            SET v_eq = JSON_EXTRACT(v_data, '$.newEquipped');
            SET v_ench = JSON_EXTRACT(v_data, '$.newEnchant');
            SET v_extra = JSON_EXTRACT(v_data, '$.newExtra');
            IF v_item IS NULL OR v_item <= 0 OR v_cnt IS NULL OR v_cnt <= 0 OR v_eq IS NULL OR v_ench IS NULL OR v_extra IS NULL THEN
                SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid batch slots';
            END IF;
            INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
            VALUES (in_UserID, v_slot, v_item, v_cnt, v_eq, v_ench, v_extra)
            ON DUPLICATE KEY UPDATE ItemID = v_item, `Count` = v_cnt, Equipped = v_eq, Enchant = v_ench, Extra = v_extra;
        ELSE
            DELETE FROM UserInventory WHERE UserID = in_UserID AND SlotIndex = v_slot;
        END IF;
        SET v_i = v_i + 1;
    END WHILE;
    COMMIT;
    SELECT 1 AS Ok;
END$$

-- ── 결사 성장(GUILD.md) ──────────────────────────────────────────────────────
-- 진행이 없는 결사도 한 줄(Data NULL — 서버가 기본값으로 시작한다). CreatedAt은 Guild에서.
DROP PROCEDURE IF EXISTS P_GUILD_PROGRESS_SELECT_ALL$$
CREATE PROCEDURE P_GUILD_PROGRESS_SELECT_ALL()
BEGIN
    SELECT g.GuildID, COALESCE(p.Seals, 0) AS Seals, p.Data, g.CreatedAt
    FROM Guild g LEFT JOIN GuildProgress p ON p.GuildID = g.GuildID;
END$$

-- 진행 덩어리만 바꾼다(금고는 건드리지 않는다 — 금고는 P_GUILD_SEALS_ADD · P_GUILD_SHOP_BUY의 ±로만)
DROP PROCEDURE IF EXISTS P_GUILD_PROGRESS_SAVE$$
CREATE PROCEDURE P_GUILD_PROGRESS_SAVE(IN in_GuildID INT, IN in_Data VARBINARY(16000))
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    IF EXISTS (SELECT 1 FROM Guild WHERE GuildID = in_GuildID) THEN
        INSERT INTO GuildProgress (GuildID, Seals, Data, UpdatedAt) VALUES (in_GuildID, 0, in_Data, UNIX_TIMESTAMP())
        ON DUPLICATE KEY UPDATE Data = in_Data, UpdatedAt = UNIX_TIMESTAMP();
        SET v_ok = 1;
    END IF;
    SELECT v_ok AS Ok;
END$$

-- 금고 ± + 기록 한 줄. 빼서 0보다 작아지면 아무것도 바꾸지 않는다(Ok 0).
DROP PROCEDURE IF EXISTS P_GUILD_SEALS_ADD$$
CREATE PROCEDURE P_GUILD_SEALS_ADD(IN in_GuildID INT, IN in_Delta INT,
                                   IN in_At BIGINT, IN in_Kind TINYINT UNSIGNED, IN in_Value INT,
                                   IN in_Who VARCHAR(20), IN in_What VARCHAR(30))
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF EXISTS (SELECT 1 FROM Guild WHERE GuildID = in_GuildID) THEN
        INSERT IGNORE INTO GuildProgress (GuildID, Seals, Data, UpdatedAt) VALUES (in_GuildID, 0, NULL, UNIX_TIMESTAMP());
        UPDATE GuildProgress SET Seals = Seals + in_Delta
        WHERE GuildID = in_GuildID AND (in_Delta >= 0 OR Seals >= -in_Delta);
        IF ROW_COUNT() = 1 THEN
            INSERT INTO GuildLog (GuildID, At, Kind, Value, Who, What) VALUES (in_GuildID, in_At, in_Kind, in_Value, in_Who, in_What);
            SET v_ok = 1;
        END IF;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

-- 결사 상점(사람 샤드): 금고 - in_Price(모자라면 Ok 0) + 소지품 한 칸 + 기록 한 줄 — 한 트랜잭션
DROP PROCEDURE IF EXISTS P_GUILD_SHOP_BUY$$
CREATE PROCEDURE P_GUILD_SHOP_BUY(IN in_UserID INT, IN in_GuildID INT, IN in_Price INT UNSIGNED,
                                  IN in_SlotIndex SMALLINT, IN in_SlotItemID SMALLINT, IN in_SlotCount SMALLINT,
                                  IN in_Equipped TINYINT UNSIGNED, IN in_Enchant INT UNSIGNED, IN in_Extra BIGINT,
                                  IN in_At BIGINT, IN in_Kind TINYINT UNSIGNED, IN in_Value INT,
                                  IN in_Who VARCHAR(20), IN in_What VARCHAR(30))
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF EXISTS (SELECT 1 FROM GuildMember WHERE UserID = in_UserID AND GuildID = in_GuildID) THEN
        UPDATE GuildProgress SET Seals = Seals - in_Price WHERE GuildID = in_GuildID AND Seals >= in_Price;
        IF ROW_COUNT() = 1 THEN
            INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
            VALUES (in_UserID, in_SlotIndex, in_SlotItemID, in_SlotCount, in_Equipped, in_Enchant, in_Extra)
            ON DUPLICATE KEY UPDATE ItemID = in_SlotItemID, `Count` = in_SlotCount,
                                    Equipped = in_Equipped, Enchant = in_Enchant, Extra = in_Extra;
            INSERT INTO GuildLog (GuildID, At, Kind, Value, Who, What) VALUES (in_GuildID, in_At, in_Kind, in_Value, in_Who, in_What);
            SET v_ok = 1;
        END IF;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_LOG_INSERT$$
CREATE PROCEDURE P_GUILD_LOG_INSERT(IN in_GuildID INT, IN in_At BIGINT, IN in_Kind TINYINT UNSIGNED, IN in_Value INT,
                                    IN in_Who VARCHAR(20), IN in_What VARCHAR(30))
BEGIN
    IF EXISTS (SELECT 1 FROM Guild WHERE GuildID = in_GuildID) THEN
        INSERT INTO GuildLog (GuildID, At, Kind, Value, Who, What) VALUES (in_GuildID, in_At, in_Kind, in_Value, in_Who, in_What);
    END IF;
END$$

-- 결사마다 최근 in_Keep줄(오래된 것부터). 그보다 오래된 줄은 지운다(서버를 켤 때 한 번).
DROP PROCEDURE IF EXISTS P_GUILD_LOG_SELECT_RECENT$$
CREATE PROCEDURE P_GUILD_LOG_SELECT_RECENT(IN in_Keep INT)
BEGIN
    DELETE l FROM GuildLog l
    JOIN (SELECT GuildLogID, ROW_NUMBER() OVER (PARTITION BY GuildID ORDER BY GuildLogID DESC) AS rn FROM GuildLog) r
      ON r.GuildLogID = l.GuildLogID
    WHERE r.rn > in_Keep;
    SELECT GuildID, At, Kind, Value, Who, What FROM GuildLog ORDER BY GuildID, GuildLogID;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_POST_SELECT_ALL$$
CREATE PROCEDURE P_GUILD_POST_SELECT_ALL()
BEGIN
    SELECT GuildPostID, GuildID, AuthorID, Author, Category, Pinned, Title, Body, At FROM GuildPost ORDER BY GuildID, GuildPostID;
END$$

-- 글 넣기 + in_DropID(0 = 없음) 지우기 — 한 트랜잭션(글 수 한도)
DROP PROCEDURE IF EXISTS P_GUILD_POST_INSERT$$
CREATE PROCEDURE P_GUILD_POST_INSERT(IN in_PostID BIGINT, IN in_GuildID INT, IN in_AuthorID INT, IN in_Author VARCHAR(20),
                                     IN in_Category TINYINT UNSIGNED, IN in_Pinned TINYINT UNSIGNED,
                                     IN in_Title VARCHAR(16), IN in_Body VARCHAR(60), IN in_At BIGINT, IN in_DropID BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF EXISTS (SELECT 1 FROM GuildMember WHERE UserID = in_AuthorID AND GuildID = in_GuildID)
       AND NOT EXISTS (SELECT 1 FROM GuildPost WHERE GuildPostID = in_PostID) THEN
        IF in_DropID <> 0 THEN
            DELETE FROM GuildPost WHERE GuildPostID = in_DropID AND GuildID = in_GuildID;
        END IF;
        INSERT INTO GuildPost (GuildPostID, GuildID, AuthorID, Author, Category, Pinned, Title, Body, At)
        VALUES (in_PostID, in_GuildID, in_AuthorID, in_Author, in_Category, in_Pinned, in_Title, in_Body, in_At);
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_POST_DELETE$$
CREATE PROCEDURE P_GUILD_POST_DELETE(IN in_GuildID INT, IN in_PostID BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DELETE FROM GuildPost WHERE GuildPostID = in_PostID AND GuildID = in_GuildID;
    IF ROW_COUNT() > 0 THEN SET v_ok = 1; END IF;
    SELECT v_ok AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_POST_PIN$$
CREATE PROCEDURE P_GUILD_POST_PIN(IN in_GuildID INT, IN in_PostID BIGINT, IN in_Pinned TINYINT UNSIGNED)
BEGIN
    UPDATE GuildPost SET Pinned = in_Pinned WHERE GuildPostID = in_PostID AND GuildID = in_GuildID;
    SELECT EXISTS(SELECT 1 FROM GuildPost WHERE GuildPostID = in_PostID AND GuildID = in_GuildID) AS Ok;
END$$

DROP PROCEDURE IF EXISTS P_GUILD_POST_MAX_ID$$
CREATE PROCEDURE P_GUILD_POST_MAX_ID()
BEGIN
    SELECT COALESCE(MAX(GuildPostID), 0) AS MaxID FROM GuildPost;
END$$

DELIMITER ;

-- 교환(0) · 주기(1) · 그로시 판매(2). 서버보다 먼저 적용한다. 재실행 가능.
USE GameServerDB;
CREATE TABLE IF NOT EXISTS PlayerTransfer (
    Receipt CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
    Kind TINYINT UNSIGNED NOT NULL,
    FromUserID INT NOT NULL,
    ToUserID INT NOT NULL,
    Payload JSON NOT NULL,
    CommittedAt BIGINT NOT NULL,
    INDEX IX_PlayerTransfer_From (FromUserID, CommittedAt),
    INDEX IX_PlayerTransfer_To (ToUserID, CommittedAt)
) ENGINE=InnoDB;
DELIMITER $$
DROP PROCEDURE IF EXISTS P_PLAYER_TRANSFER$$
CREATE PROCEDURE P_PLAYER_TRANSFER(IN in_Receipt CHAR(32), IN in_Data LONGTEXT)
main: BEGIN
    DECLARE v_a INT;
    DECLARE v_b INT;
    DECLARE v_low INT;
    DECLARE v_high INT;
    DECLARE v_wallet INT;
    DECLARE v_side INT DEFAULT 0;
    DECLARE v_pid INT;
    DECLARE v_before INT;
    DECLARE v_after INT;
    DECLARE v_i INT;
    DECLARE v_n INT;
    DECLARE v_slot INT;
    DECLARE v_has INT;
    DECLARE v_rows INT;
    DECLARE v_player JSON;
    DECLARE v_slots JSON;
    DECLARE v_row JSON;
    DECLARE v_previous JSON;
    DECLARE v_seen JSON;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    IF in_Receipt IS NULL OR CHAR_LENGTH(in_Receipt) <> 32 OR in_Data IS NULL OR NOT JSON_VALID(in_Data) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid player transfer';
    END IF;
    IF JSON_TYPE(JSON_EXTRACT(in_Data, '$.players')) <> 'ARRAY' OR JSON_LENGTH(JSON_EXTRACT(in_Data, '$.players')) <> 2
       OR JSON_EXTRACT(in_Data, '$.kind') NOT BETWEEN 0 AND 2 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid player transfer players';
    END IF;
    SET v_a = JSON_EXTRACT(in_Data, '$.players[0].id');
    SET v_b = JSON_EXTRACT(in_Data, '$.players[1].id');
    IF v_a IS NULL OR v_b IS NULL OR v_a <= 0 OR v_b <= 0 OR v_a = v_b THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid player transfer ids';
    END IF;
    START TRANSACTION;
    -- 언제나 UserID 오름차순: 반대 방향 거래도 같은 순서로 잠근다.
    SET v_low = LEAST(v_a, v_b); SET v_high = GREATEST(v_a, v_b);
    SET v_wallet = NULL;
    SELECT Adena INTO v_wallet FROM User WHERE UserID = v_low FOR UPDATE;
    IF v_wallet IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Missing transfer user'; END IF;
    SET v_wallet = NULL;
    SELECT Adena INTO v_wallet FROM User WHERE UserID = v_high FOR UPDATE;
    IF v_wallet IS NULL THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Missing transfer user'; END IF;
    -- COMMIT 응답 유실 재시도: 잔액/슬롯을 다시 쓰지 않고 기존 완료 기록을 반환한다.
    SET v_previous = NULL;
    SELECT Payload INTO v_previous FROM PlayerTransfer WHERE Receipt = in_Receipt FOR UPDATE;
    IF v_previous IS NOT NULL THEN
        IF v_previous <> CAST(in_Data AS JSON) THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Transfer receipt conflict'; END IF;
        COMMIT; SELECT 1 AS Ok; LEAVE main;
    END IF;
    IF CAST(JSON_EXTRACT(in_Data, '$.players[0].beforeAdena') AS SIGNED) + CAST(JSON_EXTRACT(in_Data, '$.players[1].beforeAdena') AS SIGNED)
       <> CAST(JSON_EXTRACT(in_Data, '$.players[0].afterAdena') AS SIGNED) + CAST(JSON_EXTRACT(in_Data, '$.players[1].afterAdena') AS SIGNED) THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Transfer money not conserved';
    END IF;
    WHILE v_side < 2 DO
        SET v_player = JSON_EXTRACT(in_Data, CONCAT('$.players[', v_side, ']'));
        SET v_pid = JSON_EXTRACT(v_player, '$.id');
        SET v_before = JSON_EXTRACT(v_player, '$.beforeAdena');
        SET v_after = JSON_EXTRACT(v_player, '$.afterAdena');
        SET v_slots = JSON_EXTRACT(v_player, '$.slots');
        IF v_before IS NULL OR v_after IS NULL OR v_before NOT BETWEEN 0 AND 2000000000 OR v_after NOT BETWEEN 0 AND 2000000000
           OR v_slots IS NULL OR JSON_TYPE(v_slots) <> 'ARRAY' OR JSON_LENGTH(v_slots) > 50 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid transfer wallet or slots';
        END IF;
        SELECT Adena INTO v_wallet FROM User WHERE UserID = v_pid;
        IF v_wallet <> v_before THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale transfer wallet'; END IF;
        SET v_i = 0; SET v_n = JSON_LENGTH(v_slots); SET v_seen = JSON_ARRAY();
        WHILE v_i < v_n DO
            SET v_row = JSON_EXTRACT(v_slots, CONCAT('$[', v_i, ']'));
            SET v_slot = JSON_EXTRACT(v_row, '$.slot'); SET v_has = JSON_EXTRACT(v_row, '$.has');
            IF v_slot IS NULL OR v_slot NOT BETWEEN 0 AND 49 OR v_has IS NULL OR v_has NOT IN (0,1)
               OR JSON_CONTAINS(v_seen, CAST(v_slot AS JSON)) THEN
                SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid transfer slot';
            END IF;
            SET v_seen = JSON_ARRAY_APPEND(v_seen, '$', v_slot);
            IF v_has = 1 THEN
                SELECT COUNT(*) INTO v_rows FROM UserInventory WHERE UserID = v_pid AND SlotIndex = v_slot
                  AND ItemID = JSON_EXTRACT(v_row, '$.item') AND `Count` = JSON_EXTRACT(v_row, '$.count')
                  AND Equipped = JSON_EXTRACT(v_row, '$.equipped') AND Enchant = JSON_EXTRACT(v_row, '$.enchant') AND Extra = JSON_EXTRACT(v_row, '$.extra') FOR UPDATE;
            ELSE
                SELECT 1 - COUNT(*) INTO v_rows FROM UserInventory WHERE UserID = v_pid AND SlotIndex = v_slot FOR UPDATE;
            END IF;
            IF v_rows <> 1 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale transfer inventory'; END IF;
            IF JSON_EXTRACT(v_row, '$.keep') = 1 THEN
                IF JSON_EXTRACT(v_row, '$.newItem') IS NULL OR JSON_EXTRACT(v_row, '$.newItem') <= 0
                   OR JSON_EXTRACT(v_row, '$.newCount') IS NULL OR JSON_EXTRACT(v_row, '$.newCount') <= 0
                   OR JSON_EXTRACT(v_row, '$.newEquipped') IS NULL OR JSON_EXTRACT(v_row, '$.newEnchant') IS NULL OR JSON_EXTRACT(v_row, '$.newExtra') IS NULL THEN
                    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid incoming transfer item';
                END IF;
                INSERT INTO UserInventory (UserID, SlotIndex, ItemID, `Count`, Equipped, Enchant, Extra)
                VALUES (v_pid, v_slot, JSON_EXTRACT(v_row, '$.newItem'), JSON_EXTRACT(v_row, '$.newCount'),
                    JSON_EXTRACT(v_row, '$.newEquipped'), JSON_EXTRACT(v_row, '$.newEnchant'), JSON_EXTRACT(v_row, '$.newExtra'))
                ON DUPLICATE KEY UPDATE ItemID = JSON_EXTRACT(v_row, '$.newItem'), `Count` = JSON_EXTRACT(v_row, '$.newCount'),
                    Equipped = JSON_EXTRACT(v_row, '$.newEquipped'), Enchant = JSON_EXTRACT(v_row, '$.newEnchant'), Extra = JSON_EXTRACT(v_row, '$.newExtra');
            ELSE
                DELETE FROM UserInventory WHERE UserID = v_pid AND SlotIndex = v_slot;
            END IF;
            SET v_i = v_i + 1;
        END WHILE;
        UPDATE User SET Adena = v_after WHERE UserID = v_pid;
        SET v_side = v_side + 1;
    END WHILE;
    INSERT INTO PlayerTransfer (Receipt, Kind, FromUserID, ToUserID, Payload, CommittedAt)
    VALUES (in_Receipt, JSON_EXTRACT(in_Data, '$.kind'), v_a, v_b, CAST(in_Data AS JSON), UNIX_TIMESTAMP());
    COMMIT;
    SELECT 1 AS Ok;
END$$
DELIMITER ;
