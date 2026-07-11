-- 第三阶段扩展 FCT 工位并建立采集器与设备授权绑定。
SET time_zone = '+00:00';

ALTER TABLE station DROP CHECK chk_station_type;
ALTER TABLE station ADD CONSTRAINT chk_station_type
    CHECK (station_type IN ('SPI', 'AOI', 'ICT', 'FCT'));

CREATE TABLE collector_device_binding (
    collector_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    device_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (collector_id, device_id),
    KEY idx_binding_device (device_id, enabled),
    CONSTRAINT fk_binding_device FOREIGN KEY (device_id) REFERENCES device (device_id),
    CONSTRAINT chk_binding_enabled CHECK (enabled IN (0, 1))
) ENGINE = InnoDB;
