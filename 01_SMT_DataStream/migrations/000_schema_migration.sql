-- 迁移版本表是后续 DDL 校验的唯一记录来源。
SET time_zone = '+00:00';

CREATE TABLE IF NOT EXISTS schema_migration (
    version VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    checksum CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    applied_at DATETIME(3) NOT NULL,
    PRIMARY KEY (version)
) ENGINE = InnoDB;
