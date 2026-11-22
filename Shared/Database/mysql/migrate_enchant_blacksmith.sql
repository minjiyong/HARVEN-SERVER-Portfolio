-- Stop the server and take a backup before migrating. Existing item levels are preserved.
USE GameServerDB;
ALTER TABLE Inventory MODIFY enchant INT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE AuctionListings MODIFY enchant INT UNSIGNED NOT NULL DEFAULT 0;
ALTER TABLE AuctionSettlements MODIFY enchant INT UNSIGNED NOT NULL DEFAULT 0;
CREATE TABLE IF NOT EXISTS EnchantFirstAchievements (
    itemId SMALLINT NOT NULL,
    enchant INT UNSIGNED NOT NULL,
    playerId INT NOT NULL,
    name VARCHAR(20) NOT NULL,
    achievedAt DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    claimToken CHAR(36) NOT NULL,
    PRIMARY KEY (itemId, enchant)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS EnchantGrace (
    playerId INT NOT NULL,
    itemId SMALLINT NOT NULL,
    currentEnchant INT UNSIGNED NOT NULL,
    failures SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (playerId, itemId, currentEnchant),
    CONSTRAINT FK_EnchantGrace_Players FOREIGN KEY (playerId) REFERENCES Players(playerId) ON DELETE CASCADE
) ENGINE=InnoDB;

DELIMITER $$

DROP PROCEDURE IF EXISTS GetInventory$$
CREATE PROCEDURE GetInventory(IN p_playerId INT)
BEGIN
    SELECT slotIndex, itemId, `count`, equipped, enchant, extra
    FROM Inventory WHERE playerId = p_playerId;
END$$

DROP PROCEDURE IF EXISTS SaveInventorySlot$$
CREATE PROCEDURE SaveInventorySlot(IN p_playerId INT, IN p_slotIndex SMALLINT, IN p_itemId SMALLINT,
                                   IN p_count SMALLINT, IN p_equipped TINYINT UNSIGNED,
                                   IN p_enchant INT UNSIGNED, IN p_extra BIGINT)
BEGIN
    INSERT INTO Inventory (playerId, slotIndex, itemId, `count`, equipped, enchant, extra)
    VALUES (p_playerId, p_slotIndex, p_itemId, p_count, p_equipped, p_enchant, p_extra)
    ON DUPLICATE KEY UPDATE itemId = p_itemId, `count` = p_count, equipped = p_equipped,
                            enchant = p_enchant, extra = p_extra;
END$$

DROP PROCEDURE IF EXISTS SaveTwoInventorySlots$$
CREATE PROCEDURE SaveTwoInventorySlots(
    IN p_playerId INT,
    IN p_slotA SMALLINT, IN p_hasA TINYINT UNSIGNED, IN p_itemA SMALLINT,
    IN p_countA SMALLINT, IN p_equippedA TINYINT UNSIGNED,
    IN p_slotB SMALLINT, IN p_hasB TINYINT UNSIGNED, IN p_itemB SMALLINT,
    IN p_countB SMALLINT, IN p_equippedB TINYINT UNSIGNED,
    IN p_enchantA INT UNSIGNED, IN p_enchantB INT UNSIGNED,
    IN p_extraA BIGINT, IN p_extraB BIGINT)
BEGIN
    DECLARE v_slot SMALLINT;
    DECLARE v_has TINYINT UNSIGNED;
    DECLARE v_item SMALLINT;
    DECLARE v_count SMALLINT;
    DECLARE v_equipped TINYINT UNSIGNED;
    DECLARE v_enchant INT UNSIGNED;
    DECLARE v_extra BIGINT;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    IF p_slotA > p_slotB THEN
        SET v_slot = p_slotA, v_has = p_hasA, v_item = p_itemA, v_count = p_countA,
            v_equipped = p_equippedA, v_enchant = p_enchantA, v_extra = p_extraA;
        SET p_slotA = p_slotB, p_hasA = p_hasB, p_itemA = p_itemB, p_countA = p_countB,
            p_equippedA = p_equippedB, p_enchantA = p_enchantB, p_extraA = p_extraB;
        SET p_slotB = v_slot, p_hasB = v_has, p_itemB = v_item, p_countB = v_count,
            p_equippedB = v_equipped, p_enchantB = v_enchant, p_extraB = v_extra;
    END IF;
    START TRANSACTION;
    IF p_hasA = 1 THEN
        INSERT INTO Inventory (playerId, slotIndex, itemId, `count`, equipped, enchant, extra)
        VALUES (p_playerId, p_slotA, p_itemA, p_countA, p_equippedA, p_enchantA, p_extraA)
        ON DUPLICATE KEY UPDATE itemId = p_itemA, `count` = p_countA, equipped = p_equippedA,
                                enchant = p_enchantA, extra = p_extraA;
    ELSE
        DELETE FROM Inventory WHERE playerId = p_playerId AND slotIndex = p_slotA;
    END IF;
    IF p_hasB = 1 THEN
        INSERT INTO Inventory (playerId, slotIndex, itemId, `count`, equipped, enchant, extra)
        VALUES (p_playerId, p_slotB, p_itemB, p_countB, p_equippedB, p_enchantB, p_extraB)
        ON DUPLICATE KEY UPDATE itemId = p_itemB, `count` = p_countB, equipped = p_equippedB,
                                enchant = p_enchantB, extra = p_extraB;
    ELSE
        DELETE FROM Inventory WHERE playerId = p_playerId AND slotIndex = p_slotB;
    END IF;
    COMMIT;
END$$

DROP PROCEDURE IF EXISTS ShopTransaction$$
CREATE PROCEDURE ShopTransaction(IN p_playerId INT, IN p_adena INT, IN p_slotIndex SMALLINT,
                                 IN p_hasItem TINYINT UNSIGNED, IN p_itemId SMALLINT,
                                 IN p_count SMALLINT, IN p_equipped TINYINT UNSIGNED,
                                 IN p_enchant INT UNSIGNED)
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    UPDATE Players SET adena = p_adena WHERE playerId = p_playerId;
    IF p_hasItem = 1 THEN
        INSERT INTO Inventory (playerId, slotIndex, itemId, `count`, equipped, enchant, extra)
        VALUES (p_playerId, p_slotIndex, p_itemId, p_count, p_equipped, p_enchant, 0)
        ON DUPLICATE KEY UPDATE itemId = p_itemId, `count` = p_count, equipped = p_equipped,
                                enchant = p_enchant, extra = 0;
    ELSE
        DELETE FROM Inventory WHERE playerId = p_playerId AND slotIndex = p_slotIndex;
    END IF;
    COMMIT;
END$$

DROP PROCEDURE IF EXISTS LoadAuctionListings$$
CREATE PROCEDURE LoadAuctionListings()
BEGIN
    SELECT listingId, sellerId, itemId, `count`, enchant, extra, price, deposit, createdAt, expiresAt
    FROM AuctionListings WHERE status = 0;
END$$

DROP PROCEDURE IF EXISTS LoadAuctionSettlements$$
CREATE PROCEDURE LoadAuctionSettlements()
BEGIN
    SELECT settlementId, playerId, kind, adena, itemId, `count`, enchant, extra,
           listingId, reason, createdAt
    FROM AuctionSettlements WHERE claimedAt IS NULL;
END$$

DROP PROCEDURE IF EXISTS AuctionRegister$$
CREATE PROCEDURE AuctionRegister(
    IN p_sellerId INT, IN p_adena INT, IN p_slotIndex SMALLINT,
    IN p_hasItem TINYINT UNSIGNED, IN p_slotItem SMALLINT, IN p_slotCount SMALLINT,
    IN p_equipped TINYINT UNSIGNED, IN p_enchant INT UNSIGNED, IN p_extra BIGINT,
    IN p_listingId BIGINT, IN p_itemId SMALLINT, IN p_count SMALLINT,
    IN p_lEnchant INT UNSIGNED, IN p_lExtra BIGINT, IN p_price INT,
    IN p_deposit INT, IN p_createdAt BIGINT, IN p_expiresAt BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF NOT EXISTS (SELECT 1 FROM AuctionListings WHERE listingId = p_listingId) THEN
        UPDATE Players SET adena = p_adena WHERE playerId = p_sellerId;
        IF p_hasItem = 1 THEN
            UPDATE Inventory SET itemId = p_slotItem, `count` = p_slotCount,
                equipped = p_equipped, enchant = p_enchant, extra = p_extra
            WHERE playerId = p_sellerId AND slotIndex = p_slotIndex;
        ELSE
            DELETE FROM Inventory WHERE playerId = p_sellerId AND slotIndex = p_slotIndex;
        END IF;
        INSERT INTO AuctionListings
            (listingId, sellerId, itemId, `count`, enchant, extra, price, deposit,
             createdAt, expiresAt, status)
        VALUES (p_listingId, p_sellerId, p_itemId, p_count, p_lEnchant, p_lExtra,
                p_price, p_deposit, p_createdAt, p_expiresAt, 0);
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS ok;
END$$

DROP PROCEDURE IF EXISTS AuctionBuy$$
CREATE PROCEDURE AuctionBuy(
    IN p_buyerId INT, IN p_adena INT, IN p_listingId BIGINT, IN p_price INT,
    IN p_buyerSettle BIGINT, IN p_sellerSettle BIGINT, IN p_sellerGets INT, IN p_now BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE v_seller INT;
    DECLARE v_itemId SMALLINT;
    DECLARE v_count SMALLINT;
    DECLARE v_enchant INT UNSIGNED;
    DECLARE v_extra BIGINT;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    UPDATE AuctionListings SET status = 1, buyerId = p_buyerId, closedAt = p_now
    WHERE listingId = p_listingId AND status = 0 AND price = p_price;
    IF ROW_COUNT() = 1 THEN
        SELECT sellerId, itemId, `count`, enchant, extra
        INTO v_seller, v_itemId, v_count, v_enchant, v_extra
        FROM AuctionListings WHERE listingId = p_listingId;
        UPDATE Players SET adena = p_adena WHERE playerId = p_buyerId;
        INSERT INTO AuctionSettlements
            (settlementId, playerId, kind, adena, itemId, `count`, enchant, extra,
             listingId, reason, createdAt)
        VALUES (p_buyerSettle, p_buyerId, 2, 0, v_itemId, v_count, v_enchant, v_extra,
                p_listingId, 1, p_now);
        INSERT INTO AuctionSettlements
            (settlementId, playerId, kind, adena, itemId, `count`, enchant, extra,
             listingId, reason, createdAt)
        VALUES (p_sellerSettle, v_seller, 1, p_sellerGets, 0, 0, 0, 0,
                p_listingId, 0, p_now);
        INSERT INTO AuctionPriceHistory (itemId, price, soldAt)
        VALUES (v_itemId, p_price DIV CASE WHEN v_count > 0 THEN v_count ELSE 1 END, p_now);
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS ok;
END$$

DROP PROCEDURE IF EXISTS AuctionClose$$
CREATE PROCEDURE AuctionClose(IN p_listingId BIGINT, IN p_status TINYINT UNSIGNED,
                              IN p_settleId BIGINT, IN p_now BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    IF p_status IN (2, 3) THEN
        UPDATE AuctionListings SET status = p_status, closedAt = p_now
        WHERE listingId = p_listingId AND status = 0;
        IF ROW_COUNT() = 1 THEN
            INSERT INTO AuctionSettlements
                (settlementId, playerId, kind, adena, itemId, `count`, enchant,
                 extra, listingId, reason, createdAt)
            SELECT p_settleId, sellerId, 2, 0, itemId, `count`, enchant,
                   extra, listingId, p_status, p_now
            FROM AuctionListings WHERE listingId = p_listingId;
            SET v_ok = 1;
        END IF;
    END IF;
    COMMIT;
    SELECT v_ok AS ok;
END$$

DROP PROCEDURE IF EXISTS SettlementClaim$$
CREATE PROCEDURE SettlementClaim(
    IN p_playerId INT, IN p_settlementId BIGINT, IN p_adena INT,
    IN p_slotIndex SMALLINT, IN p_hasItem TINYINT UNSIGNED,
    IN p_slotItem SMALLINT, IN p_slotCount SMALLINT, IN p_equipped TINYINT UNSIGNED,
    IN p_enchant INT UNSIGNED, IN p_extra BIGINT, IN p_now BIGINT)
BEGIN
    DECLARE v_ok TINYINT UNSIGNED DEFAULT 0;
    DECLARE v_kind TINYINT UNSIGNED;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN ROLLBACK; RESIGNAL; END;
    START TRANSACTION;
    UPDATE AuctionSettlements SET claimedAt = p_now
    WHERE settlementId = p_settlementId AND playerId = p_playerId AND claimedAt IS NULL;
    IF ROW_COUNT() = 1 THEN
        SELECT kind INTO v_kind FROM AuctionSettlements WHERE settlementId = p_settlementId;
        UPDATE Players SET adena = p_adena WHERE playerId = p_playerId;
        IF v_kind = 2 AND p_hasItem = 1 THEN
            INSERT INTO Inventory (playerId, slotIndex, itemId, `count`, equipped, enchant, extra)
            VALUES (p_playerId, p_slotIndex, p_slotItem, p_slotCount, p_equipped, p_enchant, p_extra)
            ON DUPLICATE KEY UPDATE itemId = p_slotItem, `count` = p_slotCount,
                                    equipped = p_equipped, enchant = p_enchant, extra = p_extra;
        END IF;
        SET v_ok = 1;
    END IF;
    COMMIT;
    SELECT v_ok AS ok;
END$$

DROP PROCEDURE IF EXISTS LoadEnchantGrace$$
CREATE PROCEDURE LoadEnchantGrace(IN p_playerId INT)
BEGIN
    SELECT itemId, currentEnchant, failures FROM EnchantGrace WHERE playerId = p_playerId;
END$$

DROP PROCEDURE IF EXISTS EnchantTransaction$$
CREATE PROCEDURE EnchantTransaction(IN p_playerId INT, IN p_slots LONGTEXT,
    IN p_itemId SMALLINT, IN p_level INT UNSIGNED, IN p_name VARCHAR(20))
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
    SET v_count = JSON_LENGTH(p_slots);
    IF v_count NOT BETWEEN 2 AND 3 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid enchant slots'; END IF;
    START TRANSACTION;
    SET v_data = JSON_EXTRACT(p_slots, '$[1]');
    SET v_graceLevel = JSON_EXTRACT(v_data, '$.level');
    SET v_graceLimit = COALESCE(JSON_EXTRACT(v_data, '$.graceLimit'), 0);
    IF JSON_EXTRACT(v_data, '$.item') <> p_itemId OR
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
        INSERT INTO EnchantGrace(playerId, itemId, currentEnchant, failures)
          VALUES(p_playerId, p_itemId, v_graceLevel, 0) ON DUPLICATE KEY UPDATE failures = failures;
        SELECT failures INTO v_lockedFailures FROM EnchantGrace
          WHERE playerId = p_playerId AND itemId = p_itemId AND currentEnchant = v_graceLevel FOR UPDATE;
        IF v_lockedFailures <> v_graceBefore THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale enchant grace'; END IF;
        UPDATE EnchantGrace SET failures = v_graceAfter
          WHERE playerId = p_playerId AND itemId = p_itemId AND currentEnchant = v_graceLevel;
    END IF;
    WHILE v_i < v_count DO
        SET v_path = CONCAT('$[', v_i, ']');
        SET v_data = JSON_EXTRACT(p_slots, v_path);
        SET v_slot = JSON_EXTRACT(v_data, '$.slot');
        SET v_matches = 1;
        SELECT slotIndex INTO v_lockedSlot FROM Inventory
          WHERE playerId = p_playerId AND slotIndex = v_slot
            AND itemId = JSON_EXTRACT(v_data, '$.item') AND `count` = JSON_EXTRACT(v_data, '$.count')
            AND equipped = JSON_EXTRACT(v_data, '$.equipped') AND enchant = JSON_EXTRACT(v_data, '$.level')
            AND extra = JSON_EXTRACT(v_data, '$.extra') FOR UPDATE;
        IF v_matches <> 1 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Stale enchant inventory'; END IF;
        IF JSON_EXTRACT(v_data, '$.keep') = 0 THEN
            DELETE FROM Inventory WHERE playerId = p_playerId AND slotIndex = v_slot;
        ELSE
            UPDATE Inventory SET `count` = JSON_EXTRACT(v_data, '$.newCount'), enchant = JSON_EXTRACT(v_data, '$.newLevel')
              WHERE playerId = p_playerId AND slotIndex = v_slot;
        END IF;
        SET v_i = v_i + 1;
    END WHILE;
    IF p_level >= 11 THEN
        SET v_token = UUID();
        INSERT INTO EnchantFirstAchievements(itemId, enchant, playerId, name, claimToken)
          VALUES(p_itemId, p_level, p_playerId, p_name, v_token)
          ON DUPLICATE KEY UPDATE claimToken = claimToken;
        SELECT claimToken = v_token INTO v_first FROM EnchantFirstAchievements WHERE itemId = p_itemId AND enchant = p_level;
    END IF;
    COMMIT;
    SELECT 1 AS ok, v_first AS isFirst;
END$$

DELIMITER ;
