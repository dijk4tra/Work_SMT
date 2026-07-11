-- 第一阶段生产线、工位、设备和归档元数据结构。
SET time_zone = '+00:00';

CREATE TABLE IF NOT EXISTS production_line (
    line_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    line_name VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
    enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (line_id),
    CONSTRAINT chk_production_line_enabled CHECK (enabled IN (0, 1))
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS station (
    station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    line_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    station_name VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
    station_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (station_id),
    KEY idx_station_line (line_id, enabled),
    CONSTRAINT fk_station_line FOREIGN KEY (line_id) REFERENCES production_line (line_id),
    CONSTRAINT chk_station_type CHECK (station_type IN ('SPI', 'AOI', 'ICT')),
    CONSTRAINT chk_station_enabled CHECK (enabled IN (0, 1))
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS device (
    device_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    device_model VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
    software_version VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    hmac_secret VARBINARY(64) NOT NULL,
    enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,
    last_seen_at DATETIME(3) NULL,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (device_id),
    KEY idx_device_station (station_id, enabled),
    CONSTRAINT fk_device_station FOREIGN KEY (station_id) REFERENCES station (station_id),
    CONSTRAINT chk_device_enabled CHECK (enabled IN (0, 1))
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS archive_file (
    archive_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    upload_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    line_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    device_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    collector_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    work_order VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    product_sn VARCHAR(96) CHARACTER SET ascii COLLATE ascii_bin NULL,
    file_type VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    result VARCHAR(8) CHARACTER SET ascii COLLATE ascii_bin NULL,
    original_filename VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci NOT NULL,
    relative_path VARCHAR(512) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    file_size BIGINT UNSIGNED NOT NULL,
    file_sha256 BINARY(32) NOT NULL,
    produced_at DATETIME(3) NOT NULL,
    archived_at DATETIME(3) NOT NULL,
    PRIMARY KEY (archive_id),
    UNIQUE KEY uk_archive_upload (upload_id),
    UNIQUE KEY uk_archive_path (relative_path),
    KEY idx_archive_device_time (device_id, archived_at, archive_id),
    KEY idx_archive_station_time (station_id, archived_at, archive_id),
    KEY idx_archive_trace (work_order, product_sn, archived_at, archive_id),
    CONSTRAINT fk_archive_line FOREIGN KEY (line_id) REFERENCES production_line (line_id),
    CONSTRAINT fk_archive_station FOREIGN KEY (station_id) REFERENCES station (station_id),
    CONSTRAINT fk_archive_device FOREIGN KEY (device_id) REFERENCES device (device_id),
    CONSTRAINT chk_archive_file_size CHECK (file_size > 0),
    CONSTRAINT chk_archive_file_type CHECK (
        file_type IN (
            'DETECTION_RESULT',
            'TEST_REPORT',
            'NG_IMAGE',
            'DEVICE_EXPORT',
            'RUNTIME_LOG'
        )
    ),
    CONSTRAINT chk_archive_result CHECK (result IS NULL OR result IN ('PASS', 'NG'))
) ENGINE = InnoDB;
