-- 仅供本地开发的设备资料，HMAC 密钥不能用于生产环境。
SET time_zone = '+00:00';

INSERT INTO production_line (line_id, line_name, enabled)
VALUES
    ('LINE-01', 'SMT 一号线', 1),
    ('LINE-02', 'SMT 二号线', 1),
    ('LINE-03', 'SMT 三号线', 1) AS new
ON DUPLICATE KEY UPDATE
    line_name = new.line_name,
    enabled = new.enabled;

INSERT INTO station (station_id, line_id, station_name, station_type, enabled)
VALUES
    ('ST-SPI-01', 'LINE-01', '焊膏检测工位', 'SPI', 1),
    ('ST-AOI-01', 'LINE-01', '自动光学检测工位', 'AOI', 1),
    ('ST-FCT-01', 'LINE-01', '功能测试工位', 'FCT', 1),
    ('ST-SPI-02', 'LINE-02', '焊膏检测工位', 'SPI', 1),
    ('ST-AOI-02', 'LINE-02', '自动光学检测工位', 'AOI', 1),
    ('ST-FCT-02', 'LINE-02', '功能测试工位', 'FCT', 1),
    ('ST-SPI-03', 'LINE-03', '焊膏检测工位', 'SPI', 1),
    ('ST-AOI-03', 'LINE-03', '自动光学检测工位', 'AOI', 1),
    ('ST-FCT-03', 'LINE-03', '功能测试工位', 'FCT', 1) AS new
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
    ('FCT-TRI-01', 'ST-FCT-01', 'TR5001 FCT', '2.6.3',
     UNHEX(SHA2('smt-dev-fct-tri-01', 256)), 1),
    ('SPI-ZM-02', 'ST-SPI-02', 'Zenith 2 Alpha', '3.8.12',
     UNHEX(SHA2('smt-dev-spi-zm-02', 256)), 1),
    ('AOI-VT-02', 'ST-AOI-02', 'VT-S730', '5.4.7',
     UNHEX(SHA2('smt-dev-aoi-vt-02', 256)), 1),
    ('FCT-TRI-02', 'ST-FCT-02', 'TR5001 FCT', '2.6.3',
     UNHEX(SHA2('smt-dev-fct-tri-02', 256)), 1),
    ('SPI-ZM-03', 'ST-SPI-03', 'Zenith 2 Alpha', '3.8.12',
     UNHEX(SHA2('smt-dev-spi-zm-03', 256)), 1),
    ('AOI-VT-03', 'ST-AOI-03', 'VT-S730', '5.4.7',
     UNHEX(SHA2('smt-dev-aoi-vt-03', 256)), 1),
    ('FCT-TRI-03', 'ST-FCT-03', 'TR5001 FCT', '2.6.3',
     UNHEX(SHA2('smt-dev-fct-tri-03', 256)), 1) AS new
ON DUPLICATE KEY UPDATE
    station_id = new.station_id,
    device_model = new.device_model,
    software_version = new.software_version,
    enabled = new.enabled;

UPDATE device SET enabled = 0 WHERE device_id = 'ICT-TRI-01';
UPDATE station SET enabled = 0 WHERE station_id = 'ST-ICT-01';

INSERT INTO collector_device_binding (collector_id, device_id, enabled)
VALUES
    ('IPC-L01-01', 'SPI-ZM-01', 1),
    ('IPC-L01-01', 'AOI-VT-01', 1),
    ('IPC-L01-01', 'FCT-TRI-01', 1),
    ('IPC-L02-01', 'SPI-ZM-02', 1),
    ('IPC-L02-01', 'AOI-VT-02', 1),
    ('IPC-L02-01', 'FCT-TRI-02', 1),
    ('IPC-L03-01', 'SPI-ZM-03', 1),
    ('IPC-L03-01', 'AOI-VT-03', 1),
    ('IPC-L03-01', 'FCT-TRI-03', 1) AS new
ON DUPLICATE KEY UPDATE enabled = new.enabled;
