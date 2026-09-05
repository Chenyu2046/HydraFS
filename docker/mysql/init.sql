-- 创建数据库
CREATE DATABASE IF NOT EXISTS `yuncunchu` DEFAULT CHARACTER SET utf8 COLLATE utf8_general_ci;

-- 授权 (MySQL 8.0 需要先 CREATE USER 再 GRANT)
CREATE USER IF NOT EXISTS 'yuncunchu'@'%' IDENTIFIED BY '123456';
GRANT ALL PRIVILEGES ON yuncunchu.* TO 'yuncunchu'@'%';
GRANT ALL PRIVILEGES ON yuncunchu.* TO 'root'@'%';
FLUSH PRIVILEGES;

USE `yuncunchu`;

CREATE TABLE IF NOT EXISTS `schema_migration` (
  `name` varchar(128) NOT NULL,
  `checksum` char(64) NOT NULL,
  `applied_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Applied schema migration checksums';

DROP TABLE IF EXISTS `file_info`;
CREATE TABLE `file_info` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '文件序号，自动递增，主键',
  `md5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_id` varchar(256) NOT NULL COMMENT '文件id:/group1/M00/00/00/xxx.png',
  `url` varchar(512) NOT NULL COMMENT '文件url 192.168.52.139:80/group1/M00/00/00/xxx.png',
  `size` bigint(20) DEFAULT '0' COMMENT '文件大小, 以字节为单位',
  `type` varchar(32) DEFAULT '' COMMENT '文件类型： png, zip, mp4……',
  `count` int(11) DEFAULT '0' COMMENT '文件引用计数,默认为1。每增加一个用户拥有此文件，此计数器+1',
  `storage_mode` varchar(16) NOT NULL DEFAULT 'legacy' COMMENT 'legacy or manifest',
  `manifest_id` bigint(20) DEFAULT NULL,
  `object_id` varchar(64) DEFAULT NULL,
  `content_digest` varchar(256) DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_md5` (`md5`(191))
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='文件信息表';

DROP TABLE IF EXISTS `share_file_list`;
CREATE TABLE `share_file_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `user` varchar(32) NOT NULL COMMENT '文件所属用户',
  `md5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_name` varchar(128) DEFAULT NULL COMMENT '文件名字',
  `pv` int(11) DEFAULT '1' COMMENT '文件下载量，默认值为1，下载一次加1',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '文件共享时间',
  `share_token` varchar(64) DEFAULT NULL COMMENT '独立分享能力令牌',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_share_token` (`share_token`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='共享文件列表';

DROP TABLE IF EXISTS `share_picture_list`;
CREATE TABLE `share_picture_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `user` varchar(32) NOT NULL COMMENT '文件所属用户',
  `filemd5` varchar(256) NOT NULL COMMENT '文件md5',
  `file_name` varchar(128) DEFAULT NULL COMMENT '文件名字',
  `urlmd5` varchar(256) NOT NULL COMMENT '图床urlmd5',
  `key` varchar(8) NOT NULL COMMENT '提取码',
  `pv` int(11) DEFAULT '1' COMMENT '文件下载量，默认值为1，下载一次加1',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '文件创建时间',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='图床文件列表';

DROP TABLE IF EXISTS `user_file_count`;
CREATE TABLE `user_file_count` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `user` varchar(128) NOT NULL COMMENT '文件所属用户',
  `count` int(11) DEFAULT NULL COMMENT '拥有文件的数量',
  PRIMARY KEY (`id`),
  UNIQUE KEY `user_UNIQUE` (`user`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='用户文件数量表';

DROP TABLE IF EXISTS `user_file_list`;
CREATE TABLE `user_file_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '编号',
  `user` varchar(32) NOT NULL COMMENT '文件所属用户',
  `md5` varchar(256) NOT NULL COMMENT '文件md5',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '文件创建时间',
  `file_name` varchar(128) DEFAULT NULL COMMENT '文件名字',
  `shared_status` int(11) DEFAULT NULL COMMENT '共享状态, 0为没有共享， 1为共享',
  `pv` int(11) DEFAULT NULL COMMENT '文件下载量，默认值为0，下载一次加1',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_user_md5_filename` (`user`, `md5`(191), `file_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='用户文件列表';

-- HydraStore V2 durable metadata. FastDFS IDs remain opaque backend values.
CREATE TABLE IF NOT EXISTS `object_manifest` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `object_id` varchar(64) NOT NULL,
  `total_size` bigint(20) NOT NULL,
  `content_digest` varchar(256) NOT NULL,
  `chunk_count` int(11) NOT NULL DEFAULT '0',
  `state` varchar(16) NOT NULL DEFAULT 'COMMITTED',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_object_id` (`object_id`),
  KEY `idx_manifest_digest` (`content_digest`(191))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='HydraStore ordered object manifest';

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
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_chunk_digest_size` (`sha256`,`size`),
  KEY `idx_chunk_gc` (`state`,`ref_count`,`gc_after`,`next_retry_at`),
  KEY `idx_chunk_owner` (`owner_upload_id`,`lease_until`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='HydraStore immutable CAS chunk';

CREATE TABLE IF NOT EXISTS `manifest_chunk` (
  `manifest_id` bigint(20) NOT NULL,
  `part_index` int(11) NOT NULL,
  `chunk_id` bigint(20) NOT NULL,
  `size` bigint(20) NOT NULL,
  PRIMARY KEY (`manifest_id`,`part_index`),
  KEY `idx_manifest_chunk_blob` (`chunk_id`),
  CONSTRAINT `fk_manifest_chunk_manifest` FOREIGN KEY (`manifest_id`) REFERENCES `object_manifest` (`id`),
  CONSTRAINT `fk_manifest_chunk_blob` FOREIGN KEY (`chunk_id`) REFERENCES `chunk_blob` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='HydraStore ordered manifest references';

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
  PRIMARY KEY (`id`),
  KEY `idx_upload_user_digest` (`user`,`content_digest`(191)),
  KEY `idx_upload_expiry` (`state`,`expires_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='HydraStore durable upload session';

CREATE TABLE IF NOT EXISTS `upload_part` (
  `upload_id` varchar(64) NOT NULL,
  `part_index` int(11) NOT NULL,
  `size` bigint(20) NOT NULL,
  `sha256` char(64) NOT NULL,
  `chunk_id` bigint(20) DEFAULT NULL,
  `state` varchar(16) NOT NULL DEFAULT 'MISSING',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`upload_id`,`part_index`),
  KEY `idx_upload_part_chunk` (`chunk_id`),
  CONSTRAINT `fk_upload_part_session` FOREIGN KEY (`upload_id`) REFERENCES `upload_session` (`id`),
  CONSTRAINT `fk_upload_part_blob` FOREIGN KEY (`chunk_id`) REFERENCES `chunk_blob` (`id`) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='HydraStore upload part declarations';

DROP TABLE IF EXISTS `user_info`;
CREATE TABLE `user_info` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT COMMENT '用户序号，自动递增，主键',
  `user_name` varchar(32) NOT NULL DEFAULT '' COMMENT '用户名称',
  `nick_name` varchar(32) CHARACTER SET utf8mb4 NOT NULL DEFAULT '' COMMENT '用户昵称',
  `password` varchar(32) NOT NULL DEFAULT '' COMMENT '密码',
  `salt` varchar(32) NOT NULL DEFAULT '' COMMENT '密码盐值',
  `phone` varchar(16) NOT NULL DEFAULT '' COMMENT '手机号码',
  `email` varchar(64) DEFAULT '' COMMENT '邮箱',
  `api_key` varchar(256) DEFAULT '' COMMENT '用户的 DashScope API Key',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP COMMENT '时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_nick_name` (`nick_name`),
  UNIQUE KEY `uq_user_name` (`user_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='用户信息表';

-- AI 文件描述表（用于向量检索·全局缓存，按 md5 去重）
CREATE TABLE IF NOT EXISTS `file_ai_desc` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `md5` varchar(256) NOT NULL COMMENT '对应 file_info.md5',
  `description` text NOT NULL COMMENT 'AI 生成的文件内容描述',
  `embedding` mediumblob DEFAULT NULL COMMENT '向量序列化 float[1024]，重建索引用',
  `faiss_id` int DEFAULT -1 COMMENT 'FAISS 索引中的 ID',
  `model` varchar(64) DEFAULT '' COMMENT '使用的模型名',
  `status` tinyint DEFAULT 0 COMMENT '0=待处理 1=完成 2=失败',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP,
  `summary` text DEFAULT NULL COMMENT 'AI 生成的摘要（100-300 字）',
  `tags_json` text DEFAULT NULL COMMENT '标签 JSON 数组，如 ["FastDFS","分布式存储"]',
  `outline_json` longtext DEFAULT NULL COMMENT '内容大纲 JSON 数组',
  `parser_version` varchar(32) DEFAULT 'v1' COMMENT '解析器版本',
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_md5` (`md5`(191))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='AI文件描述与向量表（全局缓存）';

-- 存量数据库兼容：为 file_ai_desc 补充知识层字段
-- 使用存储过程安全添加列（列已存在时跳过，不报错）
DELIMITER //
CREATE PROCEDURE safe_add_column(
    IN tbl VARCHAR(128),
    IN col VARCHAR(128),
    IN col_def TEXT
)
BEGIN
    DECLARE cnt INT DEFAULT 0;
    SELECT COUNT(*) INTO cnt FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = 'yuncunchu' AND TABLE_NAME = tbl AND COLUMN_NAME = col;
    IF cnt = 0 THEN
        SET @ddl = CONCAT('ALTER TABLE ', tbl, ' ADD COLUMN ', col_def);
        PREPARE stmt FROM @ddl;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;
    END IF;
END //
DELIMITER ;

CALL safe_add_column('file_ai_desc', 'summary', 'text DEFAULT NULL COMMENT ''AI 生成的摘要（100-300 字）''');
CALL safe_add_column('file_ai_desc', 'tags_json', 'text DEFAULT NULL COMMENT ''标签 JSON 数组''');
CALL safe_add_column('file_ai_desc', 'outline_json', 'longtext DEFAULT NULL COMMENT ''内容大纲 JSON 数组''');
CALL safe_add_column('file_ai_desc', 'parser_version', 'varchar(32) DEFAULT ''v1'' COMMENT ''解析器版本''');
CALL safe_add_column('file_ai_desc', 'updated_at', 'datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT ''最后更新时间''');

CREATE TABLE IF NOT EXISTS `user_file_ai_desc` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL COMMENT '所属用户',
  `md5` varchar(256) NOT NULL COMMENT '对应文件md5',
  `cache_id` bigint DEFAULT NULL COMMENT '关联 file_ai_desc.id 的缓存记录',
  `description` text NOT NULL COMMENT '用户侧可检索的文件描述',
  `embedding` mediumblob DEFAULT NULL COMMENT '向量序列化 float[1024]，重建用户索引用',
  `faiss_id` int DEFAULT -1 COMMENT '用户私有 FAISS 索引中的 ID',
  `model` varchar(64) DEFAULT '' COMMENT '使用的模型名',
  `status` tinyint DEFAULT 0 COMMENT '0=待处理 1=完成 2=失败',
  `create_time` timestamp NULL DEFAULT CURRENT_TIMESTAMP,
  `summary` text DEFAULT NULL COMMENT 'AI 生成的摘要',
  `tags_json` text DEFAULT NULL COMMENT '标签 JSON 数组',
  `parse_status` varchar(32) DEFAULT 'pending' COMMENT '解析状态：pending/running/success/failed/skipped',
  `error_msg` text DEFAULT NULL COMMENT '解析失败原因',
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后更新时间',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_user_md5` (`user`, `md5`(191)),
  KEY `idx_user_status` (`user`, `status`),
  KEY `idx_user_faiss` (`user`, `faiss_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户级AI文件描述与向量表';

-- 存量数据库兼容：为 user_file_ai_desc 补充知识层字段
CALL safe_add_column('user_file_ai_desc', 'summary', 'text DEFAULT NULL COMMENT ''AI 生成的摘要''');
CALL safe_add_column('user_file_ai_desc', 'tags_json', 'text DEFAULT NULL COMMENT ''标签 JSON 数组''');
CALL safe_add_column('user_file_ai_desc', 'parse_status', 'varchar(32) DEFAULT ''pending'' COMMENT ''解析状态：pending/running/success/failed/skipped''');
CALL safe_add_column('user_file_ai_desc', 'error_msg', 'text DEFAULT NULL COMMENT ''解析失败原因''');
CALL safe_add_column('user_file_ai_desc', 'updated_at', 'datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT ''最后更新时间''');

-- =============================================
-- 知识层新增表（双链知识云存储 MVP）
-- =============================================

-- 异步解析任务表
CREATE TABLE IF NOT EXISTS `ai_parse_task` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `md5` varchar(256) NOT NULL,
  `task_type` varchar(32) NOT NULL DEFAULT 'parse_file' COMMENT '任务类型：parse_file',
  `source` varchar(32) NOT NULL DEFAULT 'upload' COMMENT '触发来源：upload/md5_hit',
  `status` varchar(32) NOT NULL DEFAULT 'pending' COMMENT 'pending/running/success/failed/skipped',
  `retry_count` int NOT NULL DEFAULT 0 COMMENT '已重试次数',
  `error_msg` text DEFAULT NULL COMMENT '失败原因',
  `worker_id` varchar(64) DEFAULT NULL COMMENT '执行 worker 标识',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_user_status` (`user`, `status`),
  KEY `idx_md5_status` (`md5`(191), `status`),
  KEY `idx_status_created` (`status`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='AI 解析任务队列';

-- 文件级 Wiki 页面表（用户私有）
CREATE TABLE IF NOT EXISTS `wiki_page` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `md5` varchar(256) NOT NULL,
  `title` varchar(255) NOT NULL COMMENT 'Wiki 标题',
  `summary` text DEFAULT NULL COMMENT 'AI 摘要',
  `tags_json` text DEFAULT NULL COMMENT '标签 JSON 数组',
  `outline_json` longtext DEFAULT NULL COMMENT '内容大纲 JSON 数组',
  `status` varchar(32) NOT NULL DEFAULT 'active' COMMENT 'active/deleted',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_user_md5` (`user`, `md5`(191)),
  KEY `idx_user_status` (`user`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户文件级 Wiki 页面';

-- 显式 + 隐式双链关系表（用户私有）
-- explicit: 来自文档内 [[link]]，dst_name 必填，dst_md5 可空
-- implicit: 来自 worker 自动向量相似度，dst_md5 + score 必填，dst_name 为对方文件名兜底
CREATE TABLE IF NOT EXISTS `wiki_link` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `user` varchar(32) NOT NULL,
  `src_md5` varchar(256) NOT NULL COMMENT '源文件 MD5',
  `dst_md5` varchar(256) DEFAULT NULL COMMENT '目标文件 MD5（implicit 必填）',
  `dst_name` varchar(255) DEFAULT NULL COMMENT '目标概念名 / 文件名兜底',
  `link_type` varchar(32) NOT NULL DEFAULT 'explicit' COMMENT 'explicit / implicit',
  `score` float DEFAULT NULL COMMENT 'implicit 边的相似度分数',
  `anchor_text` varchar(255) DEFAULT NULL COMMENT '锚文本',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_user_src_dst_type` (`user`, `src_md5`(191), `dst_name`, `link_type`),
  UNIQUE KEY `uq_user_src_dstmd5_type` (`user`, `src_md5`(191), `dst_md5`(191), `link_type`),
  KEY `idx_user_dst` (`user`, `dst_name`),
  KEY `idx_user_dst_md5` (`user`, `dst_md5`(191)),
  KEY `idx_user_src` (`user`, `src_md5`(191))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Wiki 双链关系表（显式 + 自动隐式）';

-- 存量数据库兼容：补充 implicit 双链所需字段
CALL safe_add_column('wiki_link', 'dst_md5', 'varchar(256) DEFAULT NULL COMMENT ''目标文件 MD5''');
CALL safe_add_column('wiki_link', 'score', 'float DEFAULT NULL COMMENT ''相似度分数''');
-- 旧版 dst_name 为 NOT NULL；存量库需放松约束（忽略错误）
ALTER TABLE `wiki_link` MODIFY COLUMN `dst_name` varchar(255) DEFAULT NULL;

DROP PROCEDURE IF EXISTS safe_add_column;
