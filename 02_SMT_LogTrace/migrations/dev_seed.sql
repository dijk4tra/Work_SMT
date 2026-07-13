-- 三线九设备的固定解析配置和仿真错误码知识库。
SET time_zone = '+00:00';

DELETE FROM parser_profile
WHERE device_id IN ('FCT-TRI-01', 'FCT-TRI-02', 'FCT-TRI-03');

INSERT INTO parser_profile (device_id, file_type, profile_name, enabled) VALUES
    ('SPI-ZM-01', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('SPI-ZM-02', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('SPI-ZM-03', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('AOI-VT-01', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('AOI-VT-02', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('AOI-VT-03', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('ICT-TRI-01', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('ICT-TRI-02', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('ICT-TRI-03', 'RUNTIME_LOG', 'kv_runtime_v1', 1),
    ('ICT-TRI-01', 'TEST_REPORT', 'fct_csv_v1', 1),
    ('ICT-TRI-02', 'TEST_REPORT', 'fct_csv_v1', 1),
    ('ICT-TRI-03', 'TEST_REPORT', 'fct_csv_v1', 1)
AS new
ON DUPLICATE KEY UPDATE profile_name = new.profile_name, enabled = new.enabled;

INSERT INTO error_code_catalog
    (error_code, module_name, title, description, recommended_action, enabled) VALUES
    ('INSPECTION_NG', 'inspection', '检测结果异常',
     '仿真检测记录中的综合结果为 NG。',
     '结合产品 SN 查询同工位检测结果和测试记录，确认缺陷项后复判。', 1),
    ('FCT_LIMIT_FAIL', 'fct', '功能测试测量值越界',
     '仿真 FCT 测试点的实测值超出上下限。',
     '核对治具接触、供电、测点限值和同批次产品结果。', 1)
AS new
ON DUPLICATE KEY UPDATE
    module_name = new.module_name,
    title = new.title,
    description = new.description,
    recommended_action = new.recommended_action,
    enabled = new.enabled;
