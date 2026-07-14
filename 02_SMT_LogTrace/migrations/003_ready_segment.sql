-- 约束 READY 与构建中批次必须保留可验证的解析和 Segment 定位事实。
SET time_zone = '+00:00';

ALTER TABLE index_batch
    ADD CONSTRAINT chk_index_batch_parsed_artifact
        CHECK (
            state NOT IN ('PARSED', 'BUILDING', 'READY')
            OR (parsed_path IS NOT NULL AND parsed_sha256 IS NOT NULL)
        ),
    ADD CONSTRAINT chk_index_batch_ready_segment
        CHECK (
            state <> 'READY'
            OR (
                segment_name IS NOT NULL
                AND segment_sha256 IS NOT NULL
                AND published_at IS NOT NULL
            )
        );
