# LogTrace 测试数据说明

第二阶段不提交生成后的归档副本，而是由 `tools/generate_phase2_samples.py` 确定性生成固定三线
UTF-8 运行日志、FCT CSV、一期 `archive_file` 插入语句和期望清单。生成数据用于验证解析字段、
字节 `offset/length`、SHA-256、未知解析配置和失败隔离；相同版本脚本重复执行应得到一致内容。

示例：

```bash
python3 tools/generate_phase2_samples.py --output /tmp/logtrace-phase2-samples
```

输出目录必须尚不存在，避免旧文件混入本次验收结果。

`data/index/` 是运行时可重建派生数据，已被 `.gitignore` 排除。跨项目 E2E 的原始归档事实仍由
`01_SMT_DataStream` 产生，第二阶段固定样本只验证解析契约，不能据此声称完成第六阶段的完整闭环。
