/**
 * @file dealsharefile_cgi.c
 * @brief  共享文件pv字段处理、取消分享、转存文件cgi程序
 * @version 1.0
 * @date
 */

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "make_log.h" //日志头文件
#include "util_cgi.h"
#include "deal_mysql.h"
#include "cfg.h"
#include "cJSON.h"
#include "redis_keys.h"
#include "redis_op.h"
#include <sys/time.h>
#include "md5.h"    //md5
#define SHAREPIC_LOG_MODULE "cgi"
#define SHAREPICS_LOG_PROC "sharepic"

// mysql 数据库配置信息 用户名， 密码， 数据库名称
static char mysql_user[128] = {0};
static char mysql_pwd[128] = {0};
static char mysql_db[128] = {0};

// redis 服务器ip、端口
static char redis_ip[30] = {0};
static char redis_port[10] = {0};

//读取配置信息
void read_cfg()
{
    //读取mysql数据库配置信息
    get_cfg_value(CFG_PATH, "mysql", "user", mysql_user);
    get_cfg_value(CFG_PATH, "mysql", "password", mysql_pwd);
    get_cfg_value(CFG_PATH, "mysql", "database", mysql_db);
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]", mysql_user, mysql_pwd, mysql_db);

    //读取redis配置信息
    get_cfg_value(CFG_PATH, "redis", "ip", redis_ip);
    get_cfg_value(CFG_PATH, "redis", "port", redis_port);
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "redis:[ip=%s,port=%s]\n", redis_ip, redis_port);
}

//解析的json包
int get_share_picture_json_info(char *buf, char *user, char *token, char *md5, char *file_name)
{
    int ret = 0;

    /*json数据如下
    {
    "user": "milo",
    "md5": "xxx",
    "file_name": "xxx"
    }
    */

    //解析json包
    //解析一个json字符串为cJSON对象
    cJSON *root = cJSON_Parse(buf);
    if (NULL == root)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //返回指定字符串对应的json对象
     //token
    cJSON *child0 = cJSON_GetObjectItem(root, "token");
    if (NULL == child0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(token, child0->valuestring); //拷贝内容

    //用户
    cJSON *child1 = cJSON_GetObjectItem(root, "user");
    if (NULL == child1)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    // LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "child1->valuestring = %s\n", child1->valuestring);
    strcpy(user, child1->valuestring); //拷贝内容

    //文件md5码
    cJSON *child2 = cJSON_GetObjectItem(root, "md5");
    if (NULL == child2)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(md5, child2->valuestring); //拷贝内容

    //文件名字
    cJSON *child3 = cJSON_GetObjectItem(root, "filename");
    if (NULL == child3)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(file_name, child3->valuestring); //拷贝内容

END:
    if (root != NULL)
    {
        cJSON_Delete(root); //删除json对象
        root = NULL;
    }

    return ret;
}

//解析的json包
int get_pictureslist_json_info(char *buf, char *user,char *token, int *p_start, int *p_count)
{
    int ret = 0;

    /*json数据如下
    {
        "start": 0
        "count": 10
    }
    */

    //解析json包
    //解析一个json字符串为cJSON对象
    cJSON *root = cJSON_Parse(buf);
    if (NULL == root)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    cJSON *child0 = cJSON_GetObjectItem(root, "token");
    if (NULL == child0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(token, child0->valuestring);

    cJSON *child1 = cJSON_GetObjectItem(root, "user");
    if (NULL == child1)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }
    strcpy(user, child1->valuestring);


    //文件起点
    cJSON *child2 = cJSON_GetObjectItem(root, "start");
    if (NULL == child2)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    *p_start = child2->valueint;

    //文件请求个数
    cJSON *child3 = cJSON_GetObjectItem(root, "count");
    if (NULL == child3)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    *p_count = child3->valueint;

END:
    if (root != NULL)
    {
        cJSON_Delete(root); //删除json对象
        root = NULL;
    }

    return ret;
}

//解析的json包
int get_cancel_picture_json_info(char *buf, char *user, char *urlmd5)
{
    int ret = 0;

    //解析json包
    //解析一个json字符串为cJSON对象
    cJSON *root = cJSON_Parse(buf);
    if (NULL == root)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //返回指定字符串对应的json对象
    // urlmd5
    cJSON *child1 = cJSON_GetObjectItem(root, "urlmd5");
    if (NULL == child1)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    // LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "child1->valuestring = %s\n", child1->valuestring);
    strcpy(urlmd5, child1->valuestring); //拷贝内容

    // 用户名
    cJSON *child3 = cJSON_GetObjectItem(root, "user");
    if (NULL == child3)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    strcpy(user, child3->valuestring); //拷贝内容
END:
    if (root != NULL)
    {
        cJSON_Delete(root); //删除json对象
        root = NULL;
    }

    return ret;
}

int get_browse_picture_json_info(char *buf, char *urlmd5)
{
    int ret = 0;

    //解析json包
    //解析一个json字符串为cJSON对象
    cJSON *root = cJSON_Parse(buf);
    if (NULL == root)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_Parse err\n");
        ret = -1;
        goto END;
    }

    //返回指定字符串对应的json对象
    // urlmd5
    cJSON *child1 = cJSON_GetObjectItem(root, "urlmd5");
    if (NULL == child1)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cJSON_GetObjectItem err\n");
        ret = -1;
        goto END;
    }

    // LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "child1->valuestring = %s\n", child1->valuestring);
    strcpy(urlmd5, child1->valuestring); //拷贝内容
END:
    if (root != NULL)
    {
        cJSON_Delete(root); //删除json对象
        root = NULL;
    }

    return ret;
}

//获取共享图片个数
int get_share_pictures_count(const char *user, long *pcout)
{
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    long line = 0;
    int ret = 0;
    // connect the database
    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    sprintf(sql_cmd, "select count from user_file_count where user='%s%s'", user,"_share_picture_list_count");
    char tmp[512] = {0};
    ret = process_result_one(conn, sql_cmd, tmp); //指向sql语句
    if (ret != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败\n", sql_cmd);
        ret = 0;        // 有可能是没有共享的数据
        goto END;
    }

    line = atol(tmp); //字符串转长整形

END:
    if (conn != NULL)
    {
        mysql_close(conn);
    }
    *pcout = line;
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "line = %ld\n", line);
    return ret;
}

//获取共享文件列表
//获取用户文件信息 127.0.0.1:80/sharepicture&cmd=normal
int get_share_pictureslist(const char *user, int start, int count)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    cJSON *root = NULL;
    cJSON *array = NULL;
    char *out = NULL;
    MYSQL_RES *res_set = NULL;
    long total = 0;

    root = cJSON_CreateObject();
    ret = get_share_pictures_count(user, &total);
    if (ret != 0 || total == 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "get_share_files_count err\n");
        ret = -1;
        goto END;
    }
    // connect the database
    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

    // sql语句
	sprintf(sql_cmd, "select share_picture_list.user, share_picture_list.filemd5, share_picture_list.file_name,share_picture_list.urlmd5, share_picture_list.pv, share_picture_list.create_time, file_info.size from file_info, share_picture_list where share_picture_list.user = '%s' and  file_info.md5 = share_picture_list.filemd5 limit %d, %d",user, start, count);
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 在操作\n", sql_cmd);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    res_set = mysql_store_result(conn); /*生成结果集*/
    if (res_set == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "smysql_store_result error!\n");
        ret = -1;
        goto END;
    }

    int line = 0;
    // mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    line = mysql_num_rows(res_set);
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "mysql_num_rows(res_set) = %d\n", line);
    if (line == 0)
    {
        ret = 0;
        goto END;
    }

    MYSQL_ROW row;

    array = cJSON_CreateArray();
    // mysql_fetch_row从使用mysql_store_result得到的结果结构中提取一行，并把它放到一个行结构中。
    // 当数据用完或发生错误时返回NULL.
    while ((row = mysql_fetch_row(res_set)) != NULL)
    {
        // array[i]:
        cJSON *item = cJSON_CreateObject();

        // mysql_num_fields获取结果中列的个数
        /*for(i = 0; i < mysql_num_fields(res_set); i++)
        {
            if(row[i] != NULL)
            {

            }
        }*/

        /*
        {
        "user": "milo",
        "md5": "e8ea6031b779ac26c319ddf949ad9d8d",
        "create_time": "2020-06-21 21:35:25",
        "file_name": "test.mp4",
        "share_status": 1,
        "pv": 0,
        "url": "http://192.168.31.109:80/group1/M00/00/00/wKgfbViy2Z2AJ-FTAaM3As-g3Z0782.mp4",
        "size": 27473666,
         "type": "mp4"
        }
        */
		// column_index = 0;
        int column_index = 0;
        //-- user	文件所属用户
        if (row[column_index] != NULL)
        {
            cJSON_AddStringToObject(item, "user", row[column_index]);
        }

        column_index++;
        //-- md5 文件md5
        if (row[column_index] != NULL)
        {
            cJSON_AddStringToObject(item, "filemd5", row[column_index]);
        }
        column_index++;
        if (row[column_index] != NULL)
        {
            cJSON_AddStringToObject(item, "file_name", row[column_index]);
        }

        column_index++;
        if (row[column_index] != NULL)
        {
            cJSON_AddStringToObject(item, "urlmd5", row[column_index]);
        }

        column_index++;
        //-- pv 文件下载量，默认值为0，下载一次加1
        if (row[column_index] != NULL)
        {
            cJSON_AddNumberToObject(item, "pv", atol(row[column_index]));
        }

        column_index++;
        //-- create_time 文件创建时间
        if (row[column_index] != NULL)
        {
            cJSON_AddStringToObject(item, "create_time", row[column_index]);
        }

        column_index++;
        //-- size 文件大小, 以字节为单位
        if (row[column_index] != NULL)
        {
            cJSON_AddNumberToObject(item, "size", atol(row[column_index]));
        }
        cJSON_AddItemToArray(array, item);
    }

    cJSON_AddItemToObject(root, "files", array);

END:
    if (ret == 0)
    {
        cJSON_AddItemToObject(root, "code", cJSON_CreateNumber(HTTP_RESP_OK)); //
    }
    else
    {
        cJSON_AddItemToObject(root, "code", cJSON_CreateNumber(HTTP_RESP_FAIL)); //
    }
    cJSON_AddItemToObject(root, "total", cJSON_CreateNumber(total));
    cJSON_AddItemToObject(root, "count", cJSON_CreateNumber(line));
    out = cJSON_Print(root);
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s\n", out);
    printf("%s", out); //给前端反馈信息
    if (res_set != NULL)
    {
        //完成所有对数据的操作后，调用mysql_free_result来善后处理
        mysql_free_result(res_set);
    }

    if (conn != NULL)
    {
        mysql_close(conn);
    }

    if (root != NULL)
    {
        cJSON_Delete(root);
    }

    if (out != NULL)
    {
        free(out);
    }

    return ret;
}

//取消分享文件
int cancel_share_picture(const char *user, const char *urlmd5)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
	MYSQL_RES *res_set = NULL;
    char *out = NULL;
    char filemd5[MD5_LEN] = {0};
    char file_id[1024] = {0};
    int file_ref_count = 0;
    // connect the database
    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d\n", __FUNCTION__, __LINE__);
    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");

	// sql语句，查询分享图片记录并获取filemd5
	sprintf(sql_cmd, "select filemd5 from share_picture_list where user = '%s' and urlmd5 = '%s'", user, urlmd5);

	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 在操作\n", sql_cmd);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    res_set = mysql_store_result(conn); /*生成结果集*/
    if (res_set == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "smysql_store_result error!\n");
        ret = -1;
        goto END;
    }

    int line = 0;
    // mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    line = mysql_num_rows(res_set);
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "mysql_num_rows(res_set) = %d\n", line);
    if (line == 0)
    {
        ret = 0;
        goto END;
    }

    // 获取filemd5
    {
        MYSQL_ROW row = mysql_fetch_row(res_set);
        if (row != NULL && row[0] != NULL)
        {
            strcpy(filemd5, row[0]);
        }
        mysql_free_result(res_set);
        res_set = NULL;
    }

    //查询共享文件数量
    sprintf(sql_cmd, "select count from user_file_count where user = '%s%s'", user, "_share_picture_list_count");
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sql_cmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);
    int count = 0;
    char tmp[512] = {0};
    int ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if (ret2 != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败\n", sql_cmd);
        ret = -1;
        goto END;
    }

    //更新用户文件数量count字段
    if(ret2 >= 0)
		count = atoi(tmp);
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d,ret:%d, count:%d\n", __FUNCTION__, __LINE__, ret2, count);
    if (count > 0)
    {
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s%s'", count - 1, user, "_share_picture_list_count");
	    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sql_cmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败\n", sql_cmd);
        }
    }

    // 文件引用计数-1
    sprintf(sql_cmd, "select count, file_id from file_info where md5 = '%s'", filemd5);
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sql_cmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);
    {
        MYSQL_RES *res_set2 = NULL;
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }
        res_set2 = mysql_store_result(conn);
        if (res_set2 != NULL)
        {
            MYSQL_ROW row = mysql_fetch_row(res_set2);
            if (row != NULL)
            {
                file_ref_count = atoi(row[0]);
                if (row[1] != NULL)
                    strcpy(file_id, row[1]);
            }
            mysql_free_result(res_set2);
        }
    }

    if (file_ref_count > 0)
    {
        file_ref_count -= 1;
        sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", file_ref_count, filemd5);
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sql_cmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        }
    }

    //删除在共享列表的数据
    sprintf(sql_cmd, "delete from share_picture_list where user = '%s' and urlmd5 = '%s'", user, urlmd5);
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sql_cmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败\n", sql_cmd);
    }

    // 如果引用计数为0，从FastDFS删除文件
    if (file_ref_count == 0 && strlen(file_id) > 0)
    {
        //删除文件信息表中该文件的信息
        sprintf(sql_cmd, "delete from file_info where md5 = '%s'", filemd5);
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sql_cmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败\n", sql_cmd);
        }

        //从storage服务器删除此文件
        char fdfs_cli_conf_path[256] = {0};
        get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);
        char cmd_buf[1024*2] = {0};
        sprintf(cmd_buf, "fdfs_delete_file %s %s", fdfs_cli_conf_path, file_id);
        ret2 = system(cmd_buf);
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "remove_file_from_storage ret = %d\n", ret2);
    }

END:
    /*
    取消分享：
        成功：{"code": 0}
        失败：{"code": 1}
    */
    out = NULL;
    if (ret == 0)
    {
        out = return_status(HTTP_RESP_OK);
    }
    else
    {
        out = return_status(HTTP_RESP_FAIL);
    }

    if (out != NULL)
    {
        printf("%s", out); //给前端反馈信息
        free(out);
    }
	if (res_set != NULL)
    {
        //完成所有对数据的操作后，调用mysql_free_result来善后处理
        mysql_free_result(res_set);
    }
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}

//分享文件
int request_share_picture(char *user, char *filemd5, char *file_name)
{
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d\n", __FUNCTION__, __LINE__);
    /*
    1. 图床分享：
    （1）图床分享数据库，user file_name filemd5 urlmd5  pv浏览次数 create_time
    （2）生成提取码4位，
         生成要返回的url md5（根据用户名+文件md5+随机数）
         文件md5对应的文件加+1；
         插入图床表单
    （3）返回提取码和md5
    2. 我的图床：
         返回图床的信息。
    3. 浏览请求，解析参数urlmd5和提取码key，校验成功返回下载地址
     4. 取消图床
         删除对应的行信息，并将文件md5对应的文件加-1；
    */
    cJSON *root = NULL;
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    MYSQL *conn = NULL;
    char *out = NULL;
    char tmp[512] = {0};
    char rand_num[4] = {0};
    int ret2 = 0;
    char create_time[TIME_STRING_LEN];
    int i = 0;
    // 1. 生成urlmd5，生成提取码
    time_t now;
    //获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now));
    // 获取随机数
    //设置随机种子
    srand((unsigned int)time(NULL));
    for (i = 0; i < 4; ++i)
    {
        rand_num[i] = rand() % 16; //随机数
    }
    sprintf(tmp, "%s%s%d%d%d%d", filemd5, create_time, rand_num[0], rand_num[1], rand_num[2], rand_num[3]); // token唯一性
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d tmp:%s\n", __FUNCTION__, __LINE__, tmp);
	// to md5
    MD5_CTX md5;
    MD5Init(&md5);
    unsigned char decrypt[16];
    MD5Update(&md5, (unsigned char *)tmp, strlen(tmp));
    MD5Final(&md5, decrypt);
    char urlmd5[100] = {0};
    char str[16] = {0};
    for (i = 0; i < 16; i++)
    {
        sprintf(str, "%02x", decrypt[i]); // 最后token转成md5值，定长
        strcat(urlmd5, str);
    }
    srand((unsigned int)time(NULL));
    for (i = 0; i < 4; ++i)
    {
        rand_num[i] = rand() % 16; //随机数
    }
    char key[5] = {0};
    sprintf(key, "%01x%01x%01x%01x",  rand_num[0], rand_num[1], rand_num[2], rand_num[3]);
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, urlmd5:%s, key:%s\n", __FUNCTION__, __LINE__, urlmd5, key);
    // connect the database
    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }
	    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");
	// 3. 插入share_picture_list
    //图床分享图片的信息，额外保存在share_picture_list保存列表
    /*
        -- user	文件所属用户
        -- filemd5 文件本身的md5
        -- urlmd5 图床url md5，同一文件可以对应多个图床分享
        -- create_time 文件共享时间
        -- file_name 文件名字
        -- pv 文件下载量，默认值为1，下载一次加1
    */
    sprintf(sql_cmd, "insert into share_picture_list (user, filemd5, file_name, urlmd5, `key`, pv, create_time) values ('%s', '%s', '%s', '%s', '%s', %d, '%s')",
            user, filemd5, file_name, urlmd5, key, 0, create_time);
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sqlcmd:%s, conn:%p\n", __FUNCTION__, __LINE__, sql_cmd, conn);
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    // 文件引用计数+1
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", filemd5);
    ret2 = process_result_one(conn, sql_cmd, tmp);
    if (ret2 == 0)
    {
        int file_ref_count = atoi(tmp);
        sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", file_ref_count + 1, filemd5);
        if (mysql_query(conn, sql_cmd) != 0)
        {
            LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
            ret = -1;
            goto END;
        }
    }
    else
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "查询file_info count失败\n");
        ret = -1;
        goto END;
    }

    // 4 查询共享图片数量
    sprintf(sql_cmd, "select count from user_file_count where user = '%s%s'", user, "_share_picture_list_count");
    int count = 0;
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sqlcmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);

    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if (ret2 == 1)                                 //没有记录
    {
        //插入记录
        sprintf(sql_cmd, "insert into user_file_count (user, count) values('%s%s', %d)", user, "_share_picture_list_count", 1);
    }
    else if (ret2 == 0)
    {
        //更新用户文件数量count字段
        count = atoi(tmp);
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s%s'", count + 1, user, "_share_picture_list_count");
    }
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sqlcmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }
    ret = 0;
END:
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, create response\n", __FUNCTION__, __LINE__);

    // 5. 返回urlmd5 和提取码key
    root = cJSON_CreateObject(); //创建json项目
    if (ret == 0)
    {
        cJSON_AddItemToObject(root, "code", cJSON_CreateNumber(HTTP_RESP_OK));
        // 加上urlmd5 和key
        cJSON_AddItemToObject(root, "urlmd5", cJSON_CreateString(urlmd5));
    }
    else
    {
        cJSON_AddItemToObject(root, "code", cJSON_CreateNumber(HTTP_RESP_FAIL));
    }
    out = cJSON_Print(root); // cJSON to string(char *)
    cJSON_Delete(root);
    if (out != NULL)
    {
        printf("%s", out); //给前端反馈信息
        free(out);   //记得释放
    }

    if (conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}

int request_share_picture_url(const char *urlmd5)
{
	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d\n", __FUNCTION__, __LINE__);
    char *out = NULL;
    cJSON *root = NULL;
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char picture_url[256] = {0};
    char file_name[256] = {0};
    char user[256] = {0};
    char filemd5[MD5_LEN] = {0};
    char create_time[128] = {0};
	long pv = 0;
    MYSQL *conn = NULL;
    MYSQL_RES *res_set = NULL; //结果集结构的指针
    MYSQL_RES *res_set2 = NULL; //结果集结构的指针
    
	    // connect the database
    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "msql_conn err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码，主要处理中文编码问题
    mysql_query(conn, "set names utf8");
	// 1. 查询数据库匹配 urlmd5和key并返回filemd5
    // 2. 通过filemd5查询file_info获取下载地址
    // 如果查询失败发回code 1 ：提取码不匹配
    //                code 2 源文件已经被删除

    // sql 语句，获取用户名，文件md5
    sprintf(sql_cmd, "select user, filemd5, file_name, pv, create_time from share_picture_list where urlmd5 = '%s'", urlmd5);

    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 在操作\n", sql_cmd);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }
    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d\n", __FUNCTION__, __LINE__);
    res_set = mysql_store_result(conn); /*生成结果集*/
    if (res_set == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "smysql_store_result error!\n");
        ret = -1;
        goto END;
    }

    ulong line = 0;
    // mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    line = mysql_num_rows(res_set);
    if (line == 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "mysql_num_rows(res_set) failed\n");
        ret = -1;
        goto END;
    }

    MYSQL_ROW row;
    row = mysql_fetch_row(res_set);
    if(row  != NULL)
    {
        strcpy(user, row[0]);
        strcpy(filemd5, row[1]);
        strcpy(file_name, row[2]);
		pv = atol(row[3]);
        strcpy(create_time, row[4]);
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "user:%s, filemd5:%s, file_name:%s, pv:%d, create_time:%s\n", 
            user, filemd5, file_name, pv, create_time);
    }
    else
     {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "mysql_fetch_row(res_set) failed\n");
        ret = -1;
        goto END;
    }
    // 继续url
    sprintf(sql_cmd, "select url from file_info where md5 = '%s'", filemd5);

    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 在操作\n", sql_cmd);

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    res_set2 = mysql_store_result(conn); /*生成结果集*/
    if (res_set2 == NULL)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "smysql_store_result error!\n");
        ret = -1;
        goto END;
    }

    line = 0;
    // mysql_num_rows接受由mysql_store_result返回的结果结构集，并返回结构集中的行数
    line = mysql_num_rows(res_set2);
    if (line == 0)
    {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "mysql_num_rows(res_set2) failed\n");
        ret = -1;
        goto END;
    }

    row = mysql_fetch_row(res_set2);
    if(row  != NULL)
    {
        strcpy(picture_url, row[0]);
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "picture_url:%s\n", 
            picture_url);
    }
    else
     {
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "mysql_fetch_row(res_set) failed\n");
        ret = -1;
        goto END;
    }
	
	sprintf(sql_cmd, "update share_picture_list set pv = %ld where urlmd5 = '%s'", pv + 1, urlmd5);

	LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s %d, sqlcmd:%s\n", __FUNCTION__, __LINE__, sql_cmd);

	if (mysql_query(conn, sql_cmd) != 0)
	{
		LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
		ret = -1;
		goto END;
	}
	ret = 0;
END:
    // 5. 返回urlmd5 和提取码key
    root = cJSON_CreateObject(); //创建json项目
    if (ret == 0)
    {
        cJSON_AddItemToObject(root, "code", cJSON_CreateNumber(0));
		cJSON_AddItemToObject(root, "pv", cJSON_CreateNumber(pv));
        // 加上urlmd5 和key
        cJSON_AddItemToObject(root, "url", cJSON_CreateString(picture_url));
        cJSON_AddItemToObject(root, "user", cJSON_CreateString(user));  
        cJSON_AddItemToObject(root, "time", cJSON_CreateString(create_time)); 
    }
    else
    {
        cJSON_AddItemToObject(root, "code", cJSON_CreateNumber(HTTP_RESP_FAIL));
    }
    out = cJSON_Print(root); // cJSON to string(char *)
    cJSON_Delete(root);
    if (out != NULL)
    {
        printf("%s", out); //给前端反馈信息
        free(out);   //记得释放
    }

    if(res_set != NULL)
    {
        //完成所有对数据的操作后，调用mysql_free_result来善后处理
        mysql_free_result(res_set);
    }
 

    if (conn != NULL)
    {
        mysql_close(conn);
    }
    return ret;
}

int main()
{
    char cmd[20];
    char user[USER_NAME_LEN]; //用户名
    char md5[MD5_LEN];        //文件md5码
    char urlmd5[MD5_LEN];
    char file_name[128]; //文件名字
    char token[TOKEN_LEN] = {0};  

    //读取数据库配置信息
    read_cfg();

    //阻塞等待用户连接
    while (FCGI_Accept() >= 0)
    {
        // 获取URL地址 "?" 后面的内容
        char *query = getenv("QUERY_STRING");

        //解析命令
        query_parse_key_value(query, "cmd", cmd, NULL);
        LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "cmd = %s\n", cmd);

        char *contentLength = getenv("CONTENT_LENGTH");
        int len;

        printf("Content-type: text/html\r\n\r\n");

        if (contentLength == NULL)
        {
            len = 0;
        }
        else
        {
            len = atoi(contentLength); //字符串转整型
        }

        if (len <= 0)
        {
            printf("No data from standard input.<p>\n");
            LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "len = 0, No data from standard input\n");
        }
        else
        {
            char buf[4 * 1024] = {0};
            int ret = 0;
            ret = fread(buf, 1, len, stdin); //从标准输入(web服务器)读取内容
            if (ret == 0)
            {
                LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "fread(buf, 1, len, stdin) err\n");
                continue;
            }

            LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "buf = %s\n", buf);

            if (strcmp(cmd, "share") == 0) //分享文件
            {
                ret = get_share_picture_json_info(buf, user, token, md5, file_name); //解析json信息
                if (ret == 0)
                {
                    //验证登陆token，成功返回0，失败-1
                    ret = verify_token(user, token);
                    if (ret != 0)
                    {
                        char *out = return_status(HTTP_RESP_TOKEN_ERR);
                        if (out != NULL) { printf("%s", out); free(out); }
                        continue;
                    }
                    LOG(SHAREPIC_LOG_MODULE, SHAREPICS_LOG_PROC, "user = %s, md5 = %s, file_name = %s\n", user, md5, file_name);
                    request_share_picture(user, md5, file_name);
                }
                else
                {
                    // 回复请求格式错误
                }
            }
            else if (strcmp(cmd, "normal") == 0) //文件下载标志处理
            {
                int start = 0;
                int count = 0;
                ret = get_pictureslist_json_info(buf, user, token, &start, &count);
                if (ret == 0)
                {
                    //验证登陆token，成功返回0，失败-1
                    ret = verify_token(user, token);
                    if (ret != 0)
                    {
                        char *out = return_status(HTTP_RESP_TOKEN_ERR);
                        if (out != NULL) { printf("%s", out); free(out); }
                        continue;
                    }
                    get_share_pictureslist(user, start, count);
                }
                else
                {
                    // 回复请求格式错误
                }
            }
            else if (strcmp(cmd, "cancel") == 0) //取消分享文件
            {
                ret = get_cancel_picture_json_info(buf, user, urlmd5);
                if (ret == 0)
                {
                    cancel_share_picture(user, urlmd5);
                }
                else
                {
                    // 回复请求格式错误
                }
            }
            else if (strcmp(cmd, "browse") == 0) //取消分享文件
            {
                ret = get_browse_picture_json_info(buf, urlmd5);
                if (ret == 0)
                {
                    request_share_picture_url(urlmd5);
                }
                else
                {
                    // 回复请求格式错误
                }
            }
        }
    }

    return 0;
}

