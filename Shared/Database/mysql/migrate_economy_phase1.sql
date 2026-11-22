-- 경제 1차(Shared/Document/ECONOMY.md) — 이미 돌고 있는 DB(setup.sql 판 · migrate_guild_growth.sql 뒤)에 제작 · 분해 프로시저 하나를 더하고
-- 경매 구매 프로시저(P_AUCTION_BUY)에 거래 이력 비트 인자(in_SettleExtraOr — 9번째)를 붙인다(서버가 9개로 부른다 — 서버보다 먼저 돌린다).
-- 표는 바뀌지 않는다(가방 50칸은 UserInventory.SlotIndex SMALLINT 그대로 — 칸 30~49는 새 행일 뿐). 기존 행은 건드리지 않는다.
-- 서버를 멈추고 백업한 뒤 돌린다. 여러 번 돌려도 된다(프로시저를 바꿔 넣는다). 그다음 schema_verify.sql이 ok = 1이어야 한다.
--   mysql -h "$H" -P 3306 -u harven_admin -p --ssl-mode=VERIFY_IDENTITY --ssl-ca="$CA" < Shared/Database/mysql/migrate_economy_phase1.sql
USE GameServerDB;

DELIMITER $$
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


-- ── 경제 1차 로직 2부: 경매로 산 장비의 거래 이력(Shared/Document/ECONOMY.md 「귀속 · 거래 이력」) ─────────────
-- setup.sql과 같은 본문 — 구매자 정산 줄 Extra |= in_SettleExtraOr(장비면 bit 9 = 512, 아니면 0). 받기(P_AUCTION_SETTLEMENT_CLAIM)는 Extra를 그대로 칸에 옮긴다.
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

DELIMITER ;
