-- 하르벤의 가호 upgrade for an existing blacksmith enhancement schema.
-- Stop the server and back up the schema first; rerunning preserves all counters.
USE GameServerDB;

CREATE TABLE IF NOT EXISTS EnchantGrace (
    playerId INT NOT NULL,
    itemId SMALLINT NOT NULL,
    currentEnchant INT UNSIGNED NOT NULL,
    failures SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (playerId, itemId, currentEnchant),
    CONSTRAINT FK_EnchantGrace_Players FOREIGN KEY (playerId) REFERENCES Players(playerId) ON DELETE CASCADE
) ENGINE=InnoDB;

DELIMITER $$

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
