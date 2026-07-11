-- 仅供本地开发的设备资料，HMAC 密钥不能用于生产环境。
SET time_zone = '+00:00';

INSERT INTO production_line (line_id, line_name, enabled)
VALUES ('LINE-01', 'SMT 一号线', 1) AS new
ON DUPLICATE KEY UPDATE
    line_name = new.line_name,
    enabled = new.enabled;

INSERT INTO station (station_id, line_id, station_name, station_type, enabled)
VALUES
    ('ST-SPI-01', 'LINE-01', '焊膏检测工位', 'SPI', 1),
    ('ST-AOI-01', 'LINE-01', '自动光学检测工位', 'AOI', 1),
    ('ST-ICT-01', 'LINE-01', '在线测试台', 'ICT', 1) AS new
ON DUPLICATE KEY UPDATE
    line_id = new.line_id,
    station_name = new.station_name,
    station_type = new.station_type,
    enabled = new.enabled;

INSERT INTO device (
    device_id,
    station_id,
    device_model,
    software_version,
    hmac_secret,
    enabled
)
VALUES
    ('SPI-ZM-01', 'ST-SPI-01', 'Zenith 2 Alpha', '3.8.12',
     UNHEX(SHA2('smt-dev-spi-zm-01', 256)), 1),
    ('AOI-VT-01', 'ST-AOI-01', 'VT-S730', '5.4.7',
     UNHEX(SHA2('smt-dev-aoi-vt-01', 256)), 1),
    ('ICT-TRI-01', 'ST-ICT-01', 'TR5001 SII', '2.6.3',
     UNHEX(SHA2('smt-dev-ict-tri-01', 256)), 1) AS new
ON DUPLICATE KEY UPDATE
    station_id = new.station_id,
    device_model = new.device_model,
    software_version = new.software_version,
    enabled = new.enabled;
