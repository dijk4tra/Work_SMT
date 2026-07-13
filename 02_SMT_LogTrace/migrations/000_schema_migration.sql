-- 记录二期数据库迁移版本及文件校验值。
SET time_zone = '+00:00';

CREATE TABLE IF NOT EXISTS schema_migration (
    version VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    checksum CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    applied_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (version)
) ENGINE = InnoDB;
