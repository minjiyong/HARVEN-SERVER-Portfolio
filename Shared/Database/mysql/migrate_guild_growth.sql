-- 결사 성장(Shared/Document/GUILD.md) — 이미 돌고 있는 DB(setup.sql 판)에 표 셋 · 프로시저 열하나를 더한다.
-- 서버를 멈추고 백업한 뒤 돌린다. 여러 번 돌려도 된다(표는 IF NOT EXISTS, 프로시저는 바꿔 넣는다). 기존 표 · 행은 건드리지 않는다.
--   mysql -h "$H" -P 3306 -u harven_admin -p --ssl-mode=VERIFY_IDENTITY --ssl-ca="$CA" < Shared/Database/mysql/migrate_guild_growth.sql
USE GameServerDB;

-- 앞 단계 확인: UserID 스키마(User · Guild)가 아니면 아무것도 바꾸지 않고 멈춘다.
-- 옛 스키마(Players)면 schema_precheck.sql의 next_step 순서대로 migrate_user_id.sql까지 먼저 돌린다.
DELIMITER $$
DROP PROCEDURE IF EXISTS P_GUILD_GROWTH_CHECK$$
CREATE PROCEDURE P_GUILD_GROWTH_CHECK()
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.TABLES
               WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'Players') THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'Old schema (Players) found: run migrate_user_id.sql first (see schema_precheck.sql next_step)';
    END IF;
    IF (SELECT COUNT(*) FROM information_schema.TABLES
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME IN ('User', 'Guild')) < 2 THEN
        SIGNAL SQLSTATE '45000'
            SET MESSAGE_TEXT = 'User/Guild tables missing: empty DB uses setup.sql, old DB uses schema_precheck.sql next_step';
    END IF;
END$$
DELIMITER ;
CALL P_GUILD_GROWTH_CHECK();
DROP PROCEDURE P_GUILD_GROWTH_CHECK;

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

DELIMITER $$

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
