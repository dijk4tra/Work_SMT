# LogTrace 测试数据说明

当前第一阶段不提交运行时索引。第二阶段将在本目录加入固定 UTF-8 运行日志、FCT CSV 和期望
文档清单，用于验证解析字段、字节 offset/length、SHA-256 和失败行处理。

`data/index/` 是运行时可重建派生数据，已被 `.gitignore` 排除。跨项目 E2E 的原始归档事实仍由
`01_SMT_DataStream` 产生，二期测试数据不能绕过一期契约后声称完成完整闭环。
