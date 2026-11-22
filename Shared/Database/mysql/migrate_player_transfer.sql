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
