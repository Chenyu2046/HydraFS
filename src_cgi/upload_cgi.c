/**
 * @file upload_cgi.c
 * @brief   上传文件后台CGI程序
 * @version 1.0
 * @date
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include "deal_mysql.h"
#include "fdfs_api.h"
#include "fcgi_stdio.h"
#include "fdfs_client.h"
#include "make_log.h" //日志头文件
#include "cfg.h"
#include "util_cgi.h" //cgi后台通用接口，trim_space(), memstr()

#define UPLOAD_LOG_MODULE "cgi"
#define UPLOAD_LOG_PROC   "upload"

//mysql 数据库配置信息 用户名， 密码， 数据库名称
static char mysql_user[128] = {0};
static char mysql_pwd[128] = {0};
static char mysql_db[128] = {0};

//redis 服务器ip、端口
//static char redis_ip[30] = {0};
//static char redis_port[10] = {0};

//读取配置信息
void read_cfg()
{
    //读取mysql数据库配置信息
    get_cfg_value(CFG_PATH, "mysql", "user", mysql_user);
    get_cfg_value(CFG_PATH, "mysql", "password", mysql_pwd);
    get_cfg_value(CFG_PATH, "mysql", "database", mysql_db);
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "mysql:[user=%s,pwd=%s,database=%s]", mysql_user, mysql_pwd, mysql_db);

    //读取redis配置信息
    //get_cfg_value(CFG_PATH, "redis", "ip", redis_ip);
    //get_cfg_value(CFG_PATH, "redis", "port", redis_port);
    //LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "redis:[ip=%s,port=%s]\n", redis_ip, redis_port);
}

/*
 * 复用已存在的MD5文件。
 * 返回值：
 *   0  已复用成功
 *   1  服务器不存在该MD5，继续物理上传
 *   2  当前用户已拥有该文件
 *  -1  数据库操作失败
 */
int bind_existing_file_to_user(char *user, char *filename, char *md5)
{
    int ret = 1;
    int ret2 = 0;
    int count = 0;
    char tmp[512] = {0};
    char sql_cmd[SQL_MAX_LEN] = {0};
    char create_time[TIME_STRING_LEN] = {0};
    MYSQL *conn = NULL;
    time_t now;

    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "msql_conn connect err\n");
        return -1;
    }

    mysql_query(conn, "set names utf8");

    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    ret2 = process_result_one(conn, sql_cmd, tmp);
    if (ret2 == 1)
    {
        ret = 1;
        goto END;
    }
    if (ret2 != 0)
    {
        ret = -1;
        goto END;
    }

    count = atoi(tmp);

    sprintf(sql_cmd, "select pv from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'",
            user, md5, filename);
    memset(tmp, 0, sizeof(tmp));
    ret2 = process_result_one(conn, sql_cmd, tmp);
    if (ret2 == 0)
    {
        ret = 2;
        goto END;
    }
    if (ret2 != 1)
    {
        ret = -1;
        goto END;
    }

    sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", count + 1, md5);
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now));

    sprintf(sql_cmd, "insert into user_file_list(user, md5, create_time, file_name, shared_status, pv) "
                     "values ('%s', '%s', '%s', '%s', %d, %d)",
            user, md5, create_time, filename, 0, 0);
    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
    memset(tmp, 0, sizeof(tmp));
    ret2 = process_result_one(conn, sql_cmd, tmp);
    if (ret2 == 1)
    {
        sprintf(sql_cmd, "insert into user_file_count (user, count) values('%s', %d)", user, 1);
    }
    else if (ret2 == 0)
    {
        count = atoi(tmp);
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count + 1, user);
    }
    else
    {
        ret = -1;
        goto END;
    }

    if (mysql_query(conn, sql_cmd) != 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    ret = 0;

END:
    if (conn != NULL)
    {
        mysql_close(conn);
    }

    return ret;
}
// 跳过空格、换行符号
char *trim_space_and_around(char *begin, char *end)
{
    char *p = begin;
    while (1)
    {
        if((!isspace(*p)) && (*p!='\r') && (*p!='\n'))
        {
            return p;       // 返回对应的位置
        }
        p++;
        if(p >=end) {
            break;
        }
    }
    return NULL;
}

char * buffer_search(char *buf, int total_len, const char* sep, const int seplen) {
    if (total_len == 0 || seplen == 0)
        return 0;
    int from = 0;
    int i;
    for (i = 0; i <= total_len - seplen; i++) 
    {
        if (memcmp(buf + from , sep, seplen) == 0)
            break;
        ++from;
    }
    if(i <= total_len - seplen)
     return  buf + i;
     else 
     return NULL;
}


/* -------------------------------------------*/
/**
 * @brief  解析上传的post数据 保存到本地临时路径
 *         同时得到文件上传者、文件名称、文件大小
 *
 * @param len       (in)    post数据的长度
 * @param user      (out)   文件上传者
 * @param file_name (out)   文件的文件名
 * @param md5       (out)   文件的MD5码
 * @param p_size    (out)   文件大小
 *
 * @returns
 *          0 succ, -1 fail
 */
/* -------------------------------------------*/
int recv_save_file(long len, char *user, char *filename, char *md5, long *p_size)
{
    int ret = 0;
    char *file_buf = NULL;
    char *buf_end = NULL;
    char *begin = NULL;
    char *p1, *p2, *p3, *p4, *p5, *p6, *end;
    char *p, *q, *k;
    char *file_start = NULL;    // 文件内容的起始位置
    char *file_end = NULL;       // 文件内容的结束位置
    char size_text[64+1] = {0}; //文件头部信息
    char boundary[TEMP_BUF_MAX_LEN] = {0};     //分界线信息

    //==========> 开辟存放文件的 内存 <===========
    file_buf = (char *)malloc(len);
    if (file_buf == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "malloc error! file size is to big!!!!\n");
        return -1;
    }

    int ret2 = fread(file_buf, 1, len, stdin); //从标准输入(web服务器)读取内容
    if(ret2 == 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fread(file_buf, 1, len, stdin) err\n");
        ret = -1;
        goto END;
    }

    //===========> 开始处理前端发送过来的post数据格式 <============
    begin = file_buf;    //内存起点
    buf_end = file_buf + len;
    p = begin;

    /*
       ------WebKitFormBoundary88asdgewtgewx\r\n
       Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
       Content-Type: application/octet-stream\r\n
       \r\n
       真正的文件内容\r\n
       ------WebKitFormBoundary88asdgewtgewx
       */

    //get boundary 得到分界线, ------WebKitFormBoundary88asdgewtgewx
    // 1. 跳过分界线
    p1 = strstr(begin, "\r\n");  // 作用是返回字符串中首次出现子串的地址
    if (p1 == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"wrong no boundary!\n");
        ret = -1;
        goto END;
    }

    //拷贝分界线
    strncpy(boundary, begin, p1-begin);      // 缓存分界线, 比如：WebKitFormBoundary88asdgewtgewx
    boundary[p1-begin] = '\0';   //字符串结束符
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"boundary: [%s]\n", boundary);

    // 文件内容在Content-Type: image/jpeg 之后
    file_start = strstr(begin, "Content-Type:");
    if(!file_start)  {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"wrong no Content-Type!\n");
        ret = -1;
        goto END;
    }
    file_start = strstr(file_start, "\r\n");        /// 得到文件的起始位置
    file_start += strlen("\r\n");       // 跳过Content-Type:所在行
    file_start += strlen("\r\n");        // 文件内容前还有空白换行
    file_end = strstr(file_start, boundary);      // 得到文件的结束位置, 文件太大遍历有问题？
    file_end = buffer_search(file_start, buf_end - file_start, boundary, strlen(boundary));
    // str = file_start + 16243;
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"file_end:%p, p:%s\n", file_end, file_end);   

    file_end -= strlen("\r\n"); // 要减去换行的长度
    p = file_start;
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"2 ->%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            p[0], p[1],  p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12]);   
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"file_end - file_start:%d\n", file_end - file_start);    
    //temp = file_buf[300];
    //file_buf[300] = '\0';
    //LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"4 ->%s\n", file_buf);
    //file_buf[300] = temp;
    //2. 匹配filename= 位置，查找到filename
	p2 = strstr(p1, "filename=");  // 查找到filename=起始位置
    p2 += strlen("filename=\"");    // 跳过filename=" ，获取到p2的起始位置。
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    end = strstr(p2, "\"");         // 查到filename="test-animal1.jpg"的"的结束位置
    strncpy(filename, p2, end-p2); 
     filename[end-p2] = '\0';
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"filename[%d]: [%s]\n",end-p2, filename);
    
    // 3. 匹配name="user"位置，查找用户名起始位置 非空格、非\r\n 以\r\n结束
     LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, file_end:%s\n", __FUNCTION__, __LINE__, file_end + strlen(boundary));
    p3 = strstr(file_end + strlen(boundary), "name=\"user\"");  // 查找到name="user"起始位置
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, p3:%s\n", __FUNCTION__, __LINE__, p3);
	p3 += strlen("name=\"user\"");    // 跳过name="user"获取到p3的起始位置。
     LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, p3:%s\n", __FUNCTION__, __LINE__, p3);
    p3 = trim_space_and_around(p3, buf_end);
    if(!p3) {
        ret = -1;
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, p3:NULL\n", __FUNCTION__, __LINE__);
        goto END;
    }
	end = strstr(p3, "\r\n");         // 以\r\n结束
    strncpy(user, p3, end-p3);      // 拷贝用户名
    user[end-p3] = '\0';
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"user[%d]: [%s]\n",end-p3, user);

    // 4. 匹配name="md5"位置，查找用户名起始位置 非空格、非\r\n 以\r\n结束
    p4 = strstr(end, "name=\"md5\"");  // 查找到name="md5"起始位置
    p4 += strlen("name=\"md5\"");    // 跳过name="md5"获取到p4的起始位置。
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    p4 = trim_space_and_around(p4, buf_end);
    end = strstr(p4, "\r\n");         // 以\r\n结束
    strncpy(md5, p4, end-p4);      // 拷贝用户名
    md5[end-p4] = '\0';
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"md5[%d]: [%s]\n",end-p4, md5);
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, p5:%s\n", __FUNCTION__, __LINE__,end);

    // 4. 匹配name="size"位置，查找用户名起始位置 非空格、非\r\n 以\r\n结束
    p5 = strstr(end, "name=\"size\"");  // 查找到name="size"起始位置
	if(!p5) {
		ret = -1;
		LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
		goto END;
	}
    p5 += strlen("name=\"size\"");    // 跳过name="size"获取到p5的起始位置。
    p5 = trim_space_and_around(p5, buf_end);
	 if(!p5) {
        ret = -1;
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
        goto END;
    }

    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    end = strstr(p5, "\r\n");         // 以\r\n结束
    strncpy(size_text, p5, end-p5);      // 拷贝用户名
    size_text[end-p5] = '\0';
    *p_size = strtol(size_text, NULL, 10); //字符串转long
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"size[%d]: [%s]\n", end-p5, size_text);
    // trim_space(filename);

    //下面才是文件的真正内容

    /*
       ------WebKitFormBoundary88asdgewtgewx\r\n
       Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
       Content-Type: application/octet-stream\r\n
       \r\n
       真正的文件内容\r\n
       ------WebKitFormBoundary88asdgewtgewx
       */
    // 指向数据起始位置
    p6 = strstr(end, boundary); 
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, p6:%s\n", __FUNCTION__, __LINE__,p6);
    p6 += strlen(boundary);     // 跳过边界
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, p6:%s\n", __FUNCTION__, __LINE__,p6);
    p6 += strlen("--");
    p6 += strlen("\r\n");       // 跳过结束符号     真正的数据起点
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d, buf_end-p6:%d p6:%s\n", __FUNCTION__, __LINE__, buf_end -p6, p6);
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"file size[%d]\n",file_end - file_start);    
    if((file_end - file_start)!= (*p_size))
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"file size not match:%d, %d\n", file_end - file_start, (*p_size));  
        ret = -1;  
        goto END;
    } 
    

    //begin---> file_len = (p-begin)

    //=====> 此时begin-->p两个指针的区间就是post的文件二进制数据
    //======>将数据写入文件中,其中文件名也是从post数据解析得来  <===========
    ret = 0;
    int fd = 0;
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"start open %s\n", filename);
    fd = open(filename, O_CREAT|O_WRONLY, 0644);
    if (fd < 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"open %s error\n", filename);
        ret = -1;
        goto END;
    }

    //ftruncate会将参数fd指定的文件大小改为参数length指定的大小
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"write %s len %d\n", filename, file_end - file_start);
    
    ftruncate(fd, file_end - file_start);
    write(fd, file_start, file_end - file_start);
    close(fd);

END:
    free(file_buf);
    return ret;
}
/* -------------------------------------------*/
/**
 * @brief  解析上传的post数据 保存到本地临时路径
 *         同时得到文件上传者、文件名称、文件大小
 *
 * @param len       (in)    post数据的长度
 * @param user      (out)   文件上传者
 * @param file_name (out)   文件的文件名
 * @param md5       (out)   文件的MD5码
 * @param p_size    (out)   文件大小
 *
 * @returns
 *          0 succ, -1 fail
 */
/* -------------------------------------------*/
int recv_save_file1(long len, char *user, char *filename, char *md5, long *p_size)
{
    int ret = 0;
    char *file_buf = NULL;
    char *begin = NULL;
    char *p, *q, *k;

    char content_text[TEMP_BUF_MAX_LEN] = {0}; //文件头部信息
    char boundary[TEMP_BUF_MAX_LEN] = {0};     //分界线信息

    //==========> 开辟存放文件的 内存 <===========
    file_buf = (char *)malloc(len);
    if (file_buf == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "malloc error! file size is to big!!!!\n");
        return -1;
    }

    int ret2 = fread(file_buf, 1, len, stdin); //从标准输入(web服务器)读取内容
    if(ret2 == 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fread(file_buf, 1, len, stdin) err\n");
        ret = -1;
        goto END;
    }

    //===========> 开始处理前端发送过来的post数据格式 <============
    begin = file_buf;    //内存起点
    p = begin;

    /*
       ------WebKitFormBoundary88asdgewtgewx\r\n
       Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
       Content-Type: application/octet-stream\r\n
       \r\n
       真正的文件内容\r\n
       ------WebKitFormBoundary88asdgewtgewx
       */

    //get boundary 得到分界线, ------WebKitFormBoundary88asdgewtgewx
    // 1. 跳过分界线
    p = strstr(begin, "\r\n");  // 作用是返回字符串中首次出现子串的地址
    if (p == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"wrong no boundary!\n");
        ret = -1;
        goto END;
    }

    //拷贝分界线
    strncpy(boundary, begin, p-begin);      // 缓存分界线
    boundary[p-begin] = '\0';   //字符串结束符
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"boundary: [%s]\n", boundary);

    p += 2;//\r\n
    //已经处理了p-begin的长度
    len -= (p-begin);

    //get content text head
    begin = p;

    //Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    // 2. 跳过Content-Disposition
    p = strstr(begin, "\r\n");
    if(p == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"ERROR: get context text error, no filename?\n");
        ret = -1;
        goto END;
    }
    strncpy(content_text, begin, p-begin);
    content_text[p-begin] = '\0';   // 保存
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"content_text: [%s]\n", content_text);

    p += 2;//\r\n
    len -= (p-begin);
 	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //========================================获取文件上传者
    //Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    //                                ↑
    q = begin;
    q = strstr(begin, "user=");
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    //                                      ↑
    q += strlen("user=");
    q++;    //跳过第一个"
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    //                                          ↑
    k = strchr(q, '"');
    strncpy(user, q, k-q);  //拷贝用户名
    user[k-q] = '\0';

    //去掉一个字符串两边的空白字符
    trim_space(user);   //util_cgi.h
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //========================================获取文件名字
    //"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    //   ↑
    begin = k;
    q = begin;
    q = strstr(begin, "filename=");

    //"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    //             ↑
    q += strlen("filename=");
    q++;    //跳过第一个"
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
    //                    ↑
    k = strchr(q, '"');
    strncpy(filename, q, k-q);  //拷贝文件名
    filename[k-q] = '\0';

    trim_space(filename);   //util_cgi.h
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //========================================获取文件MD5码
    //"; md5="xxxx"; size=10240\r\n
    //   ↑
    begin = k;
    q = begin;
    q = strstr(begin, "md5=");

    //"; md5="xxxx"; size=10240\r\n
    //        ↑
    q += strlen("md5=");
    q++;    //跳过第一个"

    //"; md5="xxxx"; size=10240\r\n
    //            ↑
    k = strchr(q, '"');
    strncpy(md5, q, k-q);   //拷贝文件名
    md5[k-q] = '\0';
	
    trim_space(md5);    //util_cgi.h
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //========================================获取文件大小
    //"; size=10240\r\n
    //   ↑
    begin = k;
    q = begin;
    q = strstr(begin, "size=");

    //"; size=10240\r\n
    //        ↑
    q += strlen("size=");

    //"; size=10240\r\n
    //             ↑
    k = strstr(q, "\r\n");
    char tmp[256] = {0};
    strncpy(tmp, q, k-q);   //内容
    tmp[k-q] = '\0';

    *p_size = strtol(tmp, NULL, 10); //字符串转long

    begin = p;
    p = strstr(begin, "\r\n");
    p += 4;//\r\n\r\n
    len -= (p-begin);
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //下面才是文件的真正内容

    /*
       ------WebKitFormBoundary88asdgewtgewx\r\n
       Content-Disposition: form-data; user="milo"; filename="xxx.jpg"; md5="xxxx"; size=10240\r\n
       Content-Type: application/octet-stream\r\n
       \r\n
       真正的文件内容\r\n
       ------WebKitFormBoundary88asdgewtgewx
       */
    // 指向数据起始位置
    begin = p;      
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    //find file's end
    p = memstr(begin, len, boundary);//util_cgi.h， 找文件结尾 在字符串中中查找字符串substr第一次出现的位置
    if (p == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "memstr(begin, len, boundary) error\n");
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "begin:%s\n", begin);
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "boundary:%s\n", boundary);
        ret = -1;
        goto END;
    }
    else
    {
        p = p - 2;//\r\n
    }

    //begin---> file_len = (p-begin)

    //=====> 此时begin-->p两个指针的区间就是post的文件二进制数据
    //======>将数据写入文件中,其中文件名也是从post数据解析得来  <===========
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
    int fd = 0;
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"start open %s\n", filename);
    fd = open(filename, O_CREAT|O_WRONLY, 0644);
    if (fd < 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"open %s error\n", filename);
        ret = -1;
        goto END;
    }

    //ftruncate会将参数fd指定的文件大小改为参数length指定的大小
    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"write %s len %d\n", filename, (p-begin));
    
    ftruncate(fd, (p-begin));
    write(fd, begin, (p-begin));
    close(fd);
	LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"%s %d\n", __FUNCTION__, __LINE__);
END:
    free(file_buf);
    return ret;
}

//上传到fastdfs中
int upload_to_dstorage_1(char *filename, char *confpath, char *fileid)
{
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	ConnectionInfo *pTrackerServer;
	int result;
	int store_path_index;
	ConnectionInfo storageServer;
	
	log_init();
	g_log_context.log_level = LOG_ERR;
	ignore_signal_pipe();

    // 加载配置文件, 并初始化
    const char* conf_file = confpath;
	if ((result=fdfs_client_init(conf_file)) != 0)
	{
		return result;
	}

    // 通过配置文件信息连接tracker, 并得到一个可以访问tracker的句柄
	pTrackerServer = tracker_get_connection();
	if (pTrackerServer == NULL)
	{
		fdfs_client_destroy();
		return errno != 0 ? errno : ECONNREFUSED;
	}

	*group_name = '\0';

    // 通过tracker句柄得到一个可以访问的storage句柄
	if ((result=tracker_query_storage_store(pTrackerServer, \
	                &storageServer, group_name, &store_path_index)) != 0)
	{
		fdfs_client_destroy();
		LOG("fastDFS", "upload_file", "tracker_query_storage fail, error no: %d, error info: %s",
			result, STRERROR(result));
		return result;
	}


    // 通过得到的storage句柄, 上传本地文件
	result = storage_upload_by_filename1(pTrackerServer, \
			&storageServer, store_path_index, \
			filename, NULL, \
			NULL, 0, group_name, fileid);
	if (result == 0)
	{
        LOG("fastDFS", "upload_file", "fileID = %s", fileid);
	}
	else
	{
        LOG("fastDFS", "upload_file", "upload file fail, error no: %d, error info: %s",
			result, STRERROR(result));
	}

    // 断开连接, 释放资源
	tracker_close_connection_ex(pTrackerServer, true);
	fdfs_client_destroy();

	return result;
}

/* -------------------------------------------*/
/**
 * @brief  将一个本地文件上传到 后台分布式文件系统中
 *
 * @param filename  (in) 本地文件的路径
 * @param fileid    (out)得到上传之后的文件ID路径
 *
 * @returns
 *      0 succ, -1 fail
 */
/* -------------------------------------------*/
int upload_to_dstorage(char *filename, char *fileid)
{
    int ret = 0;

    pid_t pid;
    int fd[2];

    //无名管道的创建
    if (pipe(fd) < 0) // fd[0] → r； fd[1] → w
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"pip error\n");
        ret = -1;
        goto END;
    }

    //创建进程
    pid = fork(); //
    if (pid < 0)//进程创建失败
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"fork error\n");
        ret = -1;
        goto END;
    }

    if(pid == 0) //子进程
    {
        //关闭读端
        close(fd[0]);

        //将标准输出 重定向 写管道
        dup2(fd[1], STDOUT_FILENO); // 往标准输出写的东西都会重定向到fd所指向的文件, 当fileid产生时输出到管道fd[1]

        //读取fdfs client 配置文件的路径
        char fdfs_cli_conf_path[256] = {0};
        get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);
        // fdfs_upload_file /etc/fdfs/client.conf 123.txt
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "fdfs_upload_file %s %s\n", fdfs_cli_conf_path, filename);
        //通过execlp执行fdfs_upload_file  如果函数调用成功,进程自己的执行代码就会变成加载程序的代码,execlp()后边的代码也就不会执行了.
        execlp("fdfs_upload_file", "fdfs_upload_file", fdfs_cli_conf_path, filename, NULL);
        //或者使用下面这种方式条用
        //int temp = upload_to_dstorage_1(filename, fdfs_cli_conf_path, fileid);
        //LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "ret:%d!\n", temp);

        //执行失败
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "execlp fdfs_upload_file error\n");

        close(fd[1]);
    }
    else //父进程
    {
        //关闭写端
        close(fd[1]);

        //从管道中去读数据
        read(fd[0], fileid, TEMP_BUF_MAX_LEN);  // 等待管道写入然后读取

        //去掉一个字符串两边的空白字符
        trim_space(fileid);

        if (strlen(fileid) == 0)
        {
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"[upload FAILED!]\n");
            ret = -1;
            goto END;
        }

        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "get [%s] succ!\n", fileid);

        wait(NULL); //等待子进程结束，回收其资源
        close(fd[0]);
    }

END:
    return ret;
}





/* -------------------------------------------*/
/**
 * @brief  封装文件存储在分布式系统中的 完整 url
 *
 * @param fileid        (in)    文件分布式id路径
 * @param fdfs_file_url (out)   文件的完整url地址
 *
 * @returns
 *      0 succ, -1 fail
 */
/* -------------------------------------------*/
int make_file_url(char *fileid, char *fdfs_file_url)
{
    int ret = 0;

    char *p = NULL;
    char *q = NULL;
    char *k = NULL;

    char fdfs_file_stat_buf[TEMP_BUF_MAX_LEN] = {0};
    char fdfs_file_host_name[HOST_NAME_LEN] = {0};  //storage所在服务器ip地址

    pid_t pid;
    int fd[2];

    //无名管道的创建
    if (pipe(fd) < 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "pip error\n");
        ret = -1;
        goto END;
    }

    //创建进程
    pid = fork();
    if (pid < 0)//进程创建失败
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC,"fork error\n");
        ret = -1;
        goto END;
    }

    if(pid == 0) //子进程
    {
        //关闭读端
        close(fd[0]);

        //将标准输出 重定向 写管道
        dup2(fd[1], STDOUT_FILENO); //dup2(fd[1], 1);

        //读取fdfs client 配置文件的路径
        char fdfs_cli_conf_path[256] = {0};
        get_cfg_value(CFG_PATH, "dfs_path", "client", fdfs_cli_conf_path);

        execlp("fdfs_file_info", "fdfs_file_info", fdfs_cli_conf_path, fileid, NULL);

        //执行失败
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "execlp fdfs_file_info error\n");

        close(fd[1]);
    }
    else //父进程
    {
        //关闭写端
        close(fd[1]);

        //从管道中去读数据
        read(fd[0], fdfs_file_stat_buf, TEMP_BUF_MAX_LEN);
        //LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "get file_ip [%s] succ\n", fdfs_file_stat_buf);

        wait(NULL); //等待子进程结束，回收其资源
        close(fd[0]);

        //拼接上传文件的完整url地址--->http://host_name/group1/M00/00/00/D12313123232312.png
        p = strstr(fdfs_file_stat_buf, "source ip address: ");

        q = p + strlen("source ip address: ");
        k = strstr(q, "\n");

        strncpy(fdfs_file_host_name, q, k-q);
        fdfs_file_host_name[k-q] = '\0';

        //printf("host_name:[%s]\n", fdfs_file_host_name);

        //读取storage_web_server服务器的端口
        char storage_web_server_port[20] = {0};
        get_cfg_value(CFG_PATH, "storage_web_server", "port", storage_web_server_port);
        get_cfg_value(CFG_PATH, "storage_web_server", "ip", fdfs_file_host_name);
		strcat(fdfs_file_url, "http://");
        strcat(fdfs_file_url, fdfs_file_host_name);
        strcat(fdfs_file_url, ":");
        strcat(fdfs_file_url, storage_web_server_port);
        strcat(fdfs_file_url, "/");
        strcat(fdfs_file_url, fileid);

        //printf("[%s]\n", fdfs_file_url);
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "file url is: %s\n", fdfs_file_url);
    }

END:
    return ret;
}


int store_fileinfo_to_mysql(char *user, char *filename, char *md5, long size, char *fileid, char *fdfs_file_url)
{
    int ret = 0;
    int ret2 = 0;
    char tmp[512] = {0};
    int count = 0;
    MYSQL *conn = NULL; //数据库连接句柄

    time_t now;;
    char create_time[TIME_STRING_LEN];
    char suffix[SUFFIX_LEN];
    char sql_cmd[SQL_MAX_LEN] = {0};

    //连接 mysql 数据库
    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (conn == NULL)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "msql_conn connect err\n");
        ret = -1;
        goto END;
    }

    //设置数据库编码
    mysql_query(conn, "set names utf8");

    //得到文件后缀字符串 如果非法文件后缀,返回"null"
    get_file_suffix(filename, suffix); //mp4, jpg, png

    /*
       -- =============================================== 用户文件列表
       -- user 文件所属用户
       -- md5 文件md5
       -- create_time 文件创建时间
       -- file_name 文件名字
       -- shared_status 共享状态, 0为没有共享， 1为共享
       -- pv 文件下载量，默认值为0，下载一次加1
       */
    // 先检查user_file_list表中该用户是否已拥有该md5文件，防止重复插入和重复累计引用计数
    sprintf(sql_cmd, "select pv from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);
    memset(tmp, 0, sizeof(tmp));
    ret2 = process_result_one(conn, sql_cmd, tmp);
    if(ret2 == 0) //已存在，跳过插入
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "user [%s] already has file [%s], skip insert\n", user, filename);
        goto END;
    }

    //sql 语句
    /*
       -- =============================================== 文件信息表
       -- md5 文件md5
       -- file_id 文件id
       -- url 文件url
       -- size 文件大小, 以字节为单位
       -- type 文件类型： png, zip, mp4……
       -- count 文件引用计数， 默认为1， 每增加一个用户拥有此文件，此计数器+1
       */
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    ret2 = process_result_one(conn, sql_cmd, tmp);
    if(ret2 == 0) //已存在，更新count（引用计数+1）
    {
        count = atoi(tmp);
        sprintf(sql_cmd, "update file_info set count = %d where md5 = '%s'", count + 1, md5);
    }
    else //不存在，插入新记录
    {
        sprintf(sql_cmd, "insert into file_info (md5, file_id, url, size, type, count) values ('%s', '%s', '%s', '%ld', '%s', %d)",
                md5, fileid, fdfs_file_url, size, suffix, 1);
    }

    if (mysql_query(conn, sql_cmd) != 0) //执行sql语句
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 文件信息操作成功\n", sql_cmd);

    //获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN-1, "%Y-%m-%d %H:%M:%S", localtime(&now));

    //sql语句
    sprintf(sql_cmd, "insert into user_file_list(user, md5, create_time, file_name, shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)", user, md5, create_time, filename, 0, 0);
    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

    //查询用户文件数量
    sprintf(sql_cmd, "select count from user_file_count where user = '%s'", user);
    memset(tmp, 0, sizeof(tmp));
    //返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
    ret2 = process_result_one(conn, sql_cmd, tmp); //执行sql语句
    if(ret2 == 1) //没有记录
    {
        //插入记录
        sprintf(sql_cmd, " insert into user_file_count (user, count) values('%s', %d)", user, 1);
    }
    else if(ret2 == 0)
    {
        //更新用户文件数量count字段
        count = atoi(tmp);
        sprintf(sql_cmd, "update user_file_count set count = %d where user = '%s'", count+1, user);
    }

    if(mysql_query(conn, sql_cmd) != 0)
    {
        LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s 操作失败: %s\n", sql_cmd, mysql_error(conn));
        ret = -1;
        goto END;
    }

END:
    if (conn != NULL)
    {
        mysql_close(conn); //断开数据库连接
    }

    return ret;
}

//===============> 将该文件的FastDFS相关信息存入mysql中 <======
int main()
{
    char filename[FILE_NAME_LEN] = {0}; //文件名
    char user[USER_NAME_LEN] = {0};   //文件上传者
    char md5[MD5_LEN] = {0};    //文件md5码
    long size;  //文件大小
    char fileid[TEMP_BUF_MAX_LEN] = {0};    //文件上传到fastDFS后的文件id
    char fdfs_file_url[FILE_URL_LEN] = {0}; //文件所存放storage的host_name

    //读取数据库配置信息
    read_cfg();

    while (FCGI_Accept() >= 0)
    {
        char *contentLength = getenv("CONTENT_LENGTH");
        long len;
        int ret = HTTP_RESP_OK;

        printf("Content-type: text/html\r\n\r\n");

        if (contentLength != NULL)
        {
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "CONTENT_LENGTH:%s\n", contentLength);
            len = strtol(contentLength, NULL, 10); //字符串转long， 或者atol
        }
        else
        {
            len = 0;
        }

        if (len <= 0)
        {
            printf("No data from standard input\n");
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "len = 0, No data from standard input\n");
            ret = -1;
        }
        else
        {
            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s开始上传[%s, 大小：%ld]到本地\n", user, filename, len);
            //===============> 得到上传文件  <============
            if (recv_save_file(len, user, filename, md5, &size) < 0)
            {
                LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "recv_save_file failed\n");
                ret = HTTP_RESP_FAIL;
                goto END;
            }

            LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "%s成功上传[%s, 大小：%ld, md5码：%s]到本地\n", user, filename, size, md5);

            {
                int dedupe_ret = bind_existing_file_to_user(user, filename, md5);
                if (dedupe_ret == 0 || dedupe_ret == 2)
                {
                    unlink(filename);
                    LOG(UPLOAD_LOG_MODULE, UPLOAD_LOG_PROC, "reuse existing file for md5=%s\n", md5);
                    ret = HTTP_RESP_OK;
                    goto END;
                }
                if (dedupe_ret < 0)
                {
                    unlink(filename);
                    ret = HTTP_RESP_FAIL;
                    goto END;
                }
            }

            //===============> 将该文件存入fastDFS中,并得到文件的file_id <============
            if (upload_to_dstorage(filename, fileid) < 0)
            {
                ret = HTTP_RESP_FAIL;
                goto END;
            }

            //================> 删除本地临时存放的上传文件 <===============
            unlink(filename);

            //================> 得到文件所存放storage的host_name <=================
            // 拼接出完整的http地址
            if (make_file_url(fileid, fdfs_file_url) < 0)
            {
                ret = HTTP_RESP_FAIL;
                goto END;
            }

            //===============> 将该文件的FastDFS相关信息存入mysql中 <======
            // 把文件写入file_info
            if (store_fileinfo_to_mysql(user, filename, md5, size, fileid, fdfs_file_url) < 0)
            {
                ret = HTTP_RESP_FAIL;
                goto END;
            }


END:
            memset(filename, 0, FILE_NAME_LEN);
            memset(user, 0, USER_NAME_LEN);
            memset(md5, 0, MD5_LEN);
            memset(fileid, 0, TEMP_BUF_MAX_LEN);
            memset(fdfs_file_url, 0, FILE_URL_LEN);

            char *out = NULL;
            //给前端返回，上传情况
            /*
               上传文件：
               成功：{"code":"008"}
               失败：{"code":"009"}
               */
            if(ret == HTTP_RESP_OK) //成功上传
            {
                out = return_status(HTTP_RESP_OK);//common.h
            }
            else//上传失败
            {
                out = return_status(HTTP_RESP_FAIL);//common.h
            }

            if(out != NULL)
            {
                printf("%s", out); //给前端反馈信息
                free(out);   //记得释放
            }

        }

    } /* while */

    return 0;
}
