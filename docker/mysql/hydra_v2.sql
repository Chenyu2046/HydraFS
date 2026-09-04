-- Idempotent HydraStore V2 migration for both fresh and existing databases.
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
  `sha256` char(64) NOT NULL,
  `size` bigint(20) NOT NULL,
  `backend_file_id` varchar(256) DEFAULT NULL,
  `state` varchar(16) NOT NULL DEFAULT 'UPLOADING',
  `ref_count` bigint(20) NOT NULL DEFAULT '0',
  `owner_upload_id` varchar(64) DEFAULT NULL,
  `lease_until` datetime DEFAULT NULL,
  `lease_epoch` bigint(20) NOT NULL DEFAULT '0',
  `gc_after` datetime DEFAULT NULL,
  `retry_count` int(11) NOT NULL DEFAULT '0',
  `next_retry_at` datetime DEFAULT NULL,
  `last_error` varchar(512) DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), UNIQUE KEY `uq_chunk_digest_size` (`sha256`,`size`),
  KEY `idx_chunk_gc` (`state`,`ref_count`,`gc_after`,`next_retry_at`),
  KEY `idx_chunk_owner` (`owner_upload_id`,`lease_until`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `upload_session` (
  `id` varchar(64) NOT NULL,
  `user` varchar(32) NOT NULL,
  `filename` varchar(255) NOT NULL,
  `size` bigint(20) NOT NULL,
  `content_digest` varchar(256) NOT NULL,
  `chunk_count` int(11) NOT NULL,
  `state` varchar(16) NOT NULL DEFAULT 'INIT',
  `object_id` varchar(64) DEFAULT NULL,
  `manifest_id` bigint(20) DEFAULT NULL,
  `expires_at` datetime NOT NULL DEFAULT (CURRENT_TIMESTAMP + INTERVAL 24 HOUR),
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`), KEY `idx_upload_user_digest` (`user`,`content_digest`(191)),
  KEY `idx_upload_expiry` (`state`,`expires_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `manifest_chunk` (
  `manifest_id` bigint(20) NOT NULL,
  `part_index` int(11) NOT NULL,
  `chunk_id` bigint(20) NOT NULL,
  `size` bigint(20) NOT NULL,
  PRIMARY KEY (`manifest_id`,`part_index`), KEY `idx_manifest_chunk_blob` (`chunk_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `upload_part` (
  `upload_id` varchar(64) NOT NULL,
  `part_index` int(11) NOT NULL,
  `size` bigint(20) NOT NULL,
  `sha256` char(64) NOT NULL,
  `chunk_id` bigint(20) DEFAULT NULL,
  `state` varchar(16) NOT NULL DEFAULT 'MISSING',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`upload_id`,`part_index`), KEY `idx_upload_part_chunk` (`chunk_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP PROCEDURE IF EXISTS hydrastore_v2_add_column;
DROP PROCEDURE IF EXISTS hydrastore_v2_add_index;
DROP PROCEDURE IF EXISTS hydrastore_v2_add_fk;
DROP PROCEDURE IF EXISTS hydrastore_v2_validate_legacy;
DELIMITER //
CREATE PROCEDURE hydrastore_v2_add_column(IN p_table VARCHAR(128), IN p_column VARCHAR(128), IN p_definition TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=p_table AND COLUMN_NAME=p_column) THEN
    SET @ddl = CONCAT('ALTER TABLE `', p_table, '` ADD COLUMN `', p_column, '` ', p_definition);
    PREPARE hydrastore_stmt FROM @ddl; EXECUTE hydrastore_stmt; DEALLOCATE PREPARE hydrastore_stmt;
  END IF;
END //
CREATE PROCEDURE hydrastore_v2_add_index(IN p_table VARCHAR(128), IN p_index VARCHAR(128), IN p_definition TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME=p_table AND INDEX_NAME=p_index) THEN
    SET @ddl = CONCAT('ALTER TABLE `', p_table, '` ADD ', p_definition);
    PREPARE hydrastore_stmt FROM @ddl; EXECUTE hydrastore_stmt; DEALLOCATE PREPARE hydrastore_stmt;
  END IF;
END //
CREATE PROCEDURE hydrastore_v2_add_fk(IN p_table VARCHAR(128), IN p_constraint VARCHAR(128), IN p_definition TEXT)
BEGIN
  IF NOT EXISTS (SELECT 1 FROM INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS WHERE CONSTRAINT_SCHEMA=DATABASE() AND TABLE_NAME=p_table AND CONSTRAINT_NAME=p_constraint) THEN
    SET @ddl = CONCAT('ALTER TABLE `', p_table, '` ADD CONSTRAINT `', p_constraint, '` ', p_definition);
    PREPARE hydrastore_stmt FROM @ddl; EXECUTE hydrastore_stmt; DEALLOCATE PREPARE hydrastore_stmt;
  END IF;
END //
CREATE PROCEDURE hydrastore_v2_validate_legacy()
BEGIN
  IF EXISTS (SELECT 1 FROM user_file_list GROUP BY user, md5, file_name HAVING COUNT(*) > 1 LIMIT 1) THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT = 'HydraStore migration stopped: duplicate user_file_list ownership rows require manual merge';
  END IF;
END //
DELIMITER ;

CALL hydrastore_v2_add_column('file_info','storage_mode', "varchar(16) NOT NULL DEFAULT 'legacy'");
CALL hydrastore_v2_add_column('file_info','manifest_id', 'bigint(20) DEFAULT NULL');
CALL hydrastore_v2_add_column('file_info','object_id', 'varchar(64) DEFAULT NULL');
CALL hydrastore_v2_add_column('file_info','content_digest', 'varchar(256) DEFAULT NULL');
CALL hydrastore_v2_add_column('share_file_list','share_token', 'varchar(64) DEFAULT NULL');
CALL hydrastore_v2_add_column('chunk_blob','lease_epoch', "bigint(20) NOT NULL DEFAULT '0' AFTER lease_until");

UPDATE share_file_list SET share_token=MD5(CONCAT(user, ':', md5, ':', file_name, ':', id))
WHERE share_token IS NULL OR share_token='';
CALL hydrastore_v2_add_index('share_file_list','uq_share_token','UNIQUE KEY `uq_share_token` (`share_token`)');
CALL hydrastore_v2_validate_legacy();
CALL hydrastore_v2_add_index('user_file_list','uq_user_md5_filename','UNIQUE KEY `uq_user_md5_filename` (`user`,`md5`(191),`file_name`)');

ALTER TABLE `upload_part` MODIFY COLUMN `chunk_id` bigint(20) DEFAULT NULL;
SET @has_upload_blob_fk = (
  SELECT COUNT(*) FROM INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS
  WHERE CONSTRAINT_SCHEMA=DATABASE() AND TABLE_NAME='upload_part' AND CONSTRAINT_NAME='fk_upload_part_blob'
);
SET @drop_upload_blob_fk = IF(@has_upload_blob_fk > 0,
  'ALTER TABLE `upload_part` DROP FOREIGN KEY `fk_upload_part_blob`', 'SELECT 1');
PREPARE hydrastore_stmt FROM @drop_upload_blob_fk; EXECUTE hydrastore_stmt; DEALLOCATE PREPARE hydrastore_stmt;
CALL hydrastore_v2_add_fk('upload_part','fk_upload_part_blob',
  'FOREIGN KEY (`chunk_id`) REFERENCES `chunk_blob` (`id`) ON DELETE SET NULL');
CALL hydrastore_v2_add_fk('upload_part','fk_upload_part_session',
  'FOREIGN KEY (`upload_id`) REFERENCES `upload_session` (`id`)');
CALL hydrastore_v2_add_fk('manifest_chunk','fk_manifest_chunk_manifest',
  'FOREIGN KEY (`manifest_id`) REFERENCES `object_manifest` (`id`)');
CALL hydrastore_v2_add_fk('manifest_chunk','fk_manifest_chunk_blob',
  'FOREIGN KEY (`chunk_id`) REFERENCES `chunk_blob` (`id`)');

DROP PROCEDURE hydrastore_v2_add_column;
DROP PROCEDURE hydrastore_v2_add_index;
DROP PROCEDURE hydrastore_v2_add_fk;
DROP PROCEDURE hydrastore_v2_validate_legacy;
