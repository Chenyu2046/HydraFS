-- Repeatable additive migration for an existing HydraFS database.
USE `yuncunchu`;

CREATE TABLE IF NOT EXISTS `object_manifest` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `object_id` varchar(64) NOT NULL,
  `total_size` bigint(20) NOT NULL,
  `content_digest` varchar(256) NOT NULL,
  `chunk_count` int(11) NOT NULL DEFAULT '0',
  `state` varchar(16) NOT NULL DEFAULT 'COMMITTED',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_object_id` (`object_id`),
  KEY `idx_manifest_digest` (`content_digest`(191))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `chunk_blob` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `sha256` char(64) NOT NULL, `size` bigint(20) NOT NULL,
  `backend_file_id` varchar(256) DEFAULT NULL,
  `state` varchar(16) NOT NULL DEFAULT 'UPLOADING',
  `ref_count` bigint(20) NOT NULL DEFAULT '0',
  `owner_upload_id` varchar(64) DEFAULT NULL, `lease_until` datetime DEFAULT NULL,
  `gc_after` datetime DEFAULT NULL, `retry_count` int(11) NOT NULL DEFAULT '0',
  `next_retry_at` datetime DEFAULT NULL, `last_error` varchar(512) DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_chunk_digest_size` (`sha256`,`size`),
  KEY `idx_chunk_gc` (`state`,`ref_count`,`gc_after`,`next_retry_at`),
  KEY `idx_chunk_owner` (`owner_upload_id`,`lease_until`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `manifest_chunk` (
  `manifest_id` bigint(20) NOT NULL, `part_index` int(11) NOT NULL,
  `chunk_id` bigint(20) NOT NULL, `size` bigint(20) NOT NULL,
  PRIMARY KEY (`manifest_id`,`part_index`), KEY `idx_manifest_chunk_blob` (`chunk_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `upload_session` (
  `id` varchar(64) NOT NULL, `user` varchar(32) NOT NULL,
  `filename` varchar(255) NOT NULL, `size` bigint(20) NOT NULL,
  `content_digest` varchar(256) NOT NULL, `chunk_count` int(11) NOT NULL,
  `state` varchar(16) NOT NULL DEFAULT 'INIT', `object_id` varchar(64) DEFAULT NULL,
  `manifest_id` bigint(20) DEFAULT NULL,
  `expires_at` datetime NOT NULL DEFAULT (CURRENT_TIMESTAMP + INTERVAL 24 HOUR),
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), KEY `idx_upload_user_digest` (`user`,`content_digest`(191)),
  KEY `idx_upload_expiry` (`state`,`expires_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `upload_part` (
  `upload_id` varchar(64) NOT NULL, `part_index` int(11) NOT NULL,
  `size` bigint(20) NOT NULL, `sha256` char(64) NOT NULL,
  `chunk_id` bigint(20) DEFAULT NULL, `state` varchar(16) NOT NULL DEFAULT 'MISSING',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`upload_id`,`part_index`), KEY `idx_upload_part_chunk` (`chunk_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP PROCEDURE IF EXISTS hydrastore_v2_add_column;
DELIMITER //
CREATE PROCEDURE hydrastore_v2_add_column(IN table_name VARCHAR(128), IN column_name VARCHAR(128), IN definition TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=table_name AND COLUMN_NAME=column_name) THEN
    SET @ddl = CONCAT('ALTER TABLE `', table_name, '` ADD COLUMN ', definition);
    PREPARE hydrastore_stmt FROM @ddl; EXECUTE hydrastore_stmt; DEALLOCATE PREPARE hydrastore_stmt;
  END IF;
END //
DELIMITER ;
CALL hydrastore_v2_add_column('file_info','storage_mode', "varchar(16) NOT NULL DEFAULT 'legacy'");
CALL hydrastore_v2_add_column('file_info','manifest_id', 'bigint(20) DEFAULT NULL');
CALL hydrastore_v2_add_column('file_info','object_id', 'varchar(64) DEFAULT NULL');
CALL hydrastore_v2_add_column('file_info','content_digest', 'varchar(256) DEFAULT NULL');
DROP PROCEDURE hydrastore_v2_add_column;
