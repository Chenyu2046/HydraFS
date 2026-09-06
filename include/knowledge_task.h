/**
 * @file knowledge_task.h
 * @brief 知识层异步解析任务公共接口
 */

#ifndef KNOWLEDGE_TASK_H
#define KNOWLEDGE_TASK_H

#include <mysql/mysql.h>

/**
 * @brief 创建异步解析任务，写入 ai_parse_task 表
 *
 * @param conn   已连接的 MySQL 句柄
 * @param user   用户名
 * @param md5    文件 MD5
 * @param type   文件类型后缀（如 "md", "pdf", "png"）
 * @param source 触发来源："upload" 或 "md5_hit"
 * @return 0 成功, -1 失败
 *
 * 内部逻辑：
 *   - 检查是否已存在 pending/running 任务，存在则跳过（防重）
 *   - 文件类型不支持解析 → 仍创建任务，由 worker 在领取后标记为 'skipped'
 *   - 否则写入 pending 状态任务
 */
int enqueue_parse_task(MYSQL *conn, const char *user, const char *md5,
                       const char *type, const char *source);

int enqueue_knowledge_task(MYSQL *conn, const char *user, const char *md5,
                           const char *task_type, const char *source, int force);

/**
 * @brief 判断文件类型是否由 AI Knowledge Worker 支持解析
 */
int is_parseable_type(const char *type);

#endif
