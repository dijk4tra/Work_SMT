-- 增加解析阶段状态、解析工件定位和失败行号。
SET time_zone = '+00:00';

ALTER TABLE index_batch
    DROP CHECK chk_index_batch_state,
    ADD COLUMN parsed_path VARCHAR(512) CHARACTER SET ascii COLLATE ascii_bin NULL
        AFTER segment_sha256,
    ADD COLUMN parsed_sha256 BINARY(32) NULL AFTER parsed_path,
    ADD CONSTRAINT chk_index_batch_state
        CHECK (state IN ('PARSING', 'PARSED', 'BUILDING', 'READY', 'FAILED'));

ALTER TABLE indexed_archive
    DROP CHECK chk_indexed_archive_state,
    MODIFY parser_profile VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    MODIFY parser_version INT UNSIGNED NULL,
    ADD COLUMN failure_line BIGINT UNSIGNED NULL AFTER failure_code,
    ADD CONSTRAINT chk_indexed_archive_state
        CHECK (state IN ('PENDING', 'PARSED', 'INDEXED', 'FAILED'));
