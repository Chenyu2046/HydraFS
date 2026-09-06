USE `yuncuchu`;

CREATE TABLE IF NOT EXISTS `knowledge_document` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `md5` varchar(256) NOT NULL,
  `object_id` varchar(64) DEFAULT NULL,
  `manifest_id` bigint DEFAULT NULL,
  `current_generation` bigint NOT NULL DEFAULT 0,
  `published_generation` bigint NOT NULL DEFAULT 0,
  `evidence_state` varchar(32) NOT NULL DEFAULT 'PENDING',
  `wiki_state` varchar(32) NOT NULL DEFAULT 'PENDING',
  `parser_version` varchar(32) NOT NULL DEFAULT 'v2',
  `chunker_version` varchar(32) NOT NULL DEFAULT 'v2',
  `truncated` tinyint NOT NULL DEFAULT 0,
  `source_bytes` bigint NOT NULL DEFAULT 0,
  `last_error` text DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_knowledge_document_user_md5` (`user`,`md5`(191)),
  KEY `idx_knowledge_document_state` (`user`,`evidence_state`,`wiki_state`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='HydraStore AI source state';

CREATE TABLE IF NOT EXISTS `knowledge_chunk` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `md5` varchar(256) NOT NULL,
  `generation` bigint NOT NULL,
  `chunk_no` int NOT NULL,
  `heading` varchar(512) NOT NULL DEFAULT '',
  `content` longtext NOT NULL,
  `content_sha256` char(64) NOT NULL,
  `start_offset` bigint NOT NULL,
  `end_offset` bigint NOT NULL,
  `state` varchar(32) NOT NULL DEFAULT 'STAGING',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_knowledge_chunk_generation_no` (`user`,`md5`(191),`generation`,`chunk_no`),
  KEY `idx_knowledge_chunk_published` (`user`,`md5`(191),`generation`,`state`),
  KEY `idx_knowledge_chunk_hash` (`content_sha256`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Deterministic evidence chunks';

CREATE TABLE IF NOT EXISTS `knowledge_vector` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `source_type` varchar(32) NOT NULL,
  `source_id` bigint NOT NULL,
  `model` varchar(128) NOT NULL,
  `dimension` int NOT NULL,
  `embedding` mediumblob NOT NULL,
  `status` varchar(32) NOT NULL DEFAULT 'ACTIVE',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_knowledge_vector_source` (`user`,`source_type`,`source_id`),
  KEY `idx_knowledge_vector_active` (`user`,`status`,`source_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Stable vector IDs for FAISS snapshots';

CREATE TABLE IF NOT EXISTS `llm_wiki_page` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `page_key` char(40) NOT NULL,
  `title` varchar(512) NOT NULL,
  `current_revision_id` bigint DEFAULT NULL,
  `status` varchar(32) NOT NULL DEFAULT 'ACTIVE',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_llm_wiki_page_key` (`user`,`page_key`),
  KEY `idx_llm_wiki_page_status` (`user`,`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Stable private Wiki page identity';

CREATE TABLE IF NOT EXISTS `llm_wiki_revision` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `page_id` bigint NOT NULL,
  `base_revision_id` bigint NOT NULL DEFAULT 0,
  `summary` varchar(2048) NOT NULL DEFAULT '',
  `body_markdown` mediumtext NOT NULL,
  `compiler_version` varchar(64) NOT NULL,
  `model` varchar(128) NOT NULL,
  `status` varchar(32) NOT NULL DEFAULT 'STAGING',
  `published_at` datetime DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), KEY `idx_llm_wiki_revision_page` (`user`,`page_id`,`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='CAS-published Wiki revisions';

CREATE TABLE IF NOT EXISTS `llm_wiki_claim` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `page_id` bigint NOT NULL,
  `revision_id` bigint NOT NULL,
  `ordinal` int NOT NULL DEFAULT 0,
  `text` varchar(2048) NOT NULL,
  `confidence` decimal(5,4) NOT NULL DEFAULT 0,
  `status` varchar(32) NOT NULL DEFAULT 'ACTIVE',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_llm_wiki_claim_ordinal` (`user`,`revision_id`,`ordinal`), KEY `idx_llm_wiki_claim_revision` (`user`,`revision_id`,`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Evidence-backed Wiki claims';

CREATE TABLE IF NOT EXISTS `llm_wiki_citation` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `claim_id` bigint NOT NULL,
  `revision_id` bigint NOT NULL,
  `chunk_id` bigint NOT NULL,
  `state` varchar(32) NOT NULL DEFAULT 'ACTIVE',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_llm_wiki_citation` (`user`,`claim_id`,`chunk_id`),
  KEY `idx_llm_wiki_citation_revision` (`user`,`revision_id`,`state`),
  KEY `idx_llm_wiki_citation_chunk` (`user`,`chunk_id`,`state`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Claim to evidence references';

CREATE TABLE IF NOT EXISTS `llm_wiki_link` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `src_page_key` char(40) NOT NULL,
  `dst_page_key` char(40) NOT NULL,
  `src_md5` varchar(256) DEFAULT NULL,
  `dst_md5` varchar(256) DEFAULT NULL,
  `relation` varchar(64) NOT NULL DEFAULT 'related',
  `status` varchar(32) NOT NULL DEFAULT 'ACTIVE',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_llm_wiki_link` (`user`,`src_page_key`,`dst_page_key`,`relation`),
  KEY `idx_llm_wiki_link_src` (`user`,`src_md5`(191),`status`),
  KEY `idx_llm_wiki_link_dst` (`user`,`dst_md5`(191),`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Private Wiki graph edges';

CREATE TABLE IF NOT EXISTS `knowledge_index_state` (
  `user` varchar(32) NOT NULL,
  `dirty_generation` bigint NOT NULL DEFAULT 0,
  `published_generation` bigint NOT NULL DEFAULT 0,
  `state` varchar(32) NOT NULL DEFAULT 'READY',
  `worker_id` varchar(128) DEFAULT NULL,
  `lease_until` datetime DEFAULT NULL,
  `last_error` text DEFAULT NULL,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`user`), KEY `idx_knowledge_index_dirty` (`state`,`lease_until`,`dirty_generation`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Versioned per-user FAISS state';

DROP PROCEDURE IF EXISTS hydrastore_ai_v2_add_column;
DROP PROCEDURE IF EXISTS hydrastore_ai_v2_add_index;
DELIMITER //
CREATE PROCEDURE hydrastore_ai_v2_add_column(IN p_table VARCHAR(128), IN p_column VARCHAR(128), IN p_definition TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=p_table AND COLUMN_NAME=p_column) THEN
    SET @ai_ddl = CONCAT('ALTER TABLE `',p_table,'` ADD COLUMN `',p_column,'` ',p_definition);
    PREPARE ai_stmt FROM @ai_ddl; EXECUTE ai_stmt; DEALLOCATE PREPARE ai_stmt;
  END IF;
END //
CREATE PROCEDURE hydrastore_ai_v2_add_index(IN p_table VARCHAR(128), IN p_index VARCHAR(128), IN p_definition TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=p_table AND INDEX_NAME=p_index) THEN
    SET @ai_ddl = CONCAT('ALTER TABLE `',p_table,'` ADD ',p_definition);
    PREPARE ai_stmt FROM @ai_ddl; EXECUTE ai_stmt; DEALLOCATE PREPARE ai_stmt;
  END IF;
END //
DELIMITER ;

CALL hydrastore_ai_v2_add_column('ai_parse_task','lease_until','datetime DEFAULT NULL');
CALL hydrastore_ai_v2_add_column('ai_parse_task','lease_epoch','bigint NOT NULL DEFAULT 0');
CALL hydrastore_ai_v2_add_column('ai_parse_task','next_retry_at','datetime DEFAULT NULL');
CALL hydrastore_ai_v2_add_column('ai_parse_task','started_at','datetime DEFAULT NULL');
CALL hydrastore_ai_v2_add_column('ai_parse_task','finished_at','datetime DEFAULT NULL');
CALL hydrastore_ai_v2_add_index('ai_parse_task','idx_ai_parse_claim','INDEX `idx_ai_parse_claim` (`status`,`next_retry_at`,`lease_until`,`created_at`)');
CALL hydrastore_ai_v2_add_index('ai_parse_task','idx_ai_parse_fence','INDEX `idx_ai_parse_fence` (`id`,`status`,`worker_id`,`lease_epoch`)');

DROP PROCEDURE hydrastore_ai_v2_add_column;
DROP PROCEDURE hydrastore_ai_v2_add_index;

INSERT INTO schema_migration(name,checksum) VALUES('hydrastore_ai_v2',COALESCE(@hydrastore_ai_v2_checksum,'UNSET'))
ON DUPLICATE KEY UPDATE checksum=VALUES(checksum),applied_at=CURRENT_TIMESTAMP;
