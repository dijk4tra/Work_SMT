-- 日志索引批次、归档消费状态、解析配置和错误码知识库。
SET time_zone = '+00:00';

CREATE TABLE IF NOT EXISTS index_batch (
    batch_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    first_archive_id BIGINT UNSIGNED NOT NULL,
    last_archive_id BIGINT UNSIGNED NOT NULL,
    state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    segment_name VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NULL,
    source_file_count INT UNSIGNED NOT NULL DEFAULT 0,
    document_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    segment_sha256 BINARY(32) NULL,
    failure_code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    published_at DATETIME(3) NULL,
    PRIMARY KEY (batch_id),
    UNIQUE KEY uk_index_batch_segment (segment_name),
    KEY idx_index_batch_state (state, batch_id),
    CONSTRAINT chk_index_batch_range CHECK (first_archive_id > 0 AND last_archive_id >= first_archive_id),
    CONSTRAINT chk_index_batch_state CHECK (state IN ('BUILDING', 'READY', 'FAILED'))
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS indexed_archive (
    archive_id BIGINT UNSIGNED NOT NULL,
    batch_id BIGINT UNSIGNED NULL,
    parser_profile VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    parser_version INT UNSIGNED NOT NULL,
    state VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    document_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
    failure_code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    indexed_at DATETIME(3) NULL,
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (archive_id),
    KEY idx_indexed_archive_state (state, archive_id),
    KEY idx_indexed_archive_batch (batch_id),
    CONSTRAINT fk_indexed_archive_batch FOREIGN KEY (batch_id) REFERENCES index_batch (batch_id),
    CONSTRAINT chk_indexed_archive_state CHECK (state IN ('PENDING', 'INDEXED', 'FAILED'))
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS parser_profile (
    device_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    file_type VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    profile_name VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (device_id, file_type),
    CONSTRAINT chk_parser_profile_type CHECK (file_type IN ('RUNTIME_LOG', 'TEST_REPORT')),
    CONSTRAINT chk_parser_profile_enabled CHECK (enabled IN (0, 1))
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS error_code_catalog (
    error_code VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    module_name VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    title VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
    description VARCHAR(1024) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
    recommended_action VARCHAR(2048) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
    enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (error_code),
    KEY idx_error_code_module (module_name, enabled),
    CONSTRAINT chk_error_code_enabled CHECK (enabled IN (0, 1))
) ENGINE = InnoDB;
