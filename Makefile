
# 使用的编译器
CC=gcc
CXX=g++
# 预处理参数
CPPLFAGS=-I./include					\
		 -I/usr/include/fastdfs			\
		 -I/usr/include/fastcommon		\
		 -I/usr/local/include/hiredis/  \
		 -I/usr/include/mysql/			\
		 -I/usr/local/include
# 选项
CFLAGS=-Wall
CXXFLAGS=-Wall -std=c++17 -Wno-write-strings
# 需要链接的动态库
LIBS=-lfdfsclient	\
	 -lfastcommon	\
	 -lhiredis		\
	 -lfcgi         \
	 -lm            \
	 -lmysqlclient
# AI 模块额外的动态库
AI_LIBS=-lfaiss -lcurl -lopenblas -lgomp -lpthread -L/usr/local/lib
# 目录路径
TEST_PATH=test
COMMON_PATH=common
CGI_BIN_PATH=bin_cgi
CGI_SRC_PATH=src_cgi

# 子目标, 因为是测试,所有需要单独生成很多子目标
# 测试用
main=main
redis=redis
# 项目用
login=$(CGI_BIN_PATH)/login
register=$(CGI_BIN_PATH)/register
upload=$(CGI_BIN_PATH)/upload
md5=$(CGI_BIN_PATH)/md5
myfiles=$(CGI_BIN_PATH)/myfiles
dealfile=$(CGI_BIN_PATH)/dealfile
sharefiles=$(CGI_BIN_PATH)/sharefiles
dealsharefile=$(CGI_BIN_PATH)/dealsharefile
sharepicture=$(CGI_BIN_PATH)/sharepicture
chunk_init=$(CGI_BIN_PATH)/chunk_init
chunk_upload=$(CGI_BIN_PATH)/chunk_upload
chunk_merge=$(CGI_BIN_PATH)/chunk_merge
ai=$(CGI_BIN_PATH)/ai

# 最终目标
target=$(login)		\
	   $(register)	\
	   $(upload)	\
	   $(md5)		\
	   $(myfiles)	\
	   $(dealfile)	\
	   $(sharefiles)	\
	   $(dealsharefile) \
	   $(sharepicture) \
	   $(chunk_init) \
	   $(chunk_upload) \
	   $(chunk_merge) \
	   $(ai)
ALL:$(target)

#######################################################################
#                        测试程序相关的规则
# 生成每一个子目标,
# main程序
$(main):$(TEST_PATH)/main.o $(TEST_PATH)/fdfs_api.o $(COMMON_PATH)/make_log.o
	$(CC) $^ $(LIBS) -o $@

# redis test 程序
$(redis):$(TEST_PATH)/myredis.o
	$(CC) $^ $(LIBS) -o $@
	#######################################################################

# =====================================================================
#							项目程序规则
# 登录
$(login):	$(CGI_SRC_PATH)/login_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/des.o \
			$(COMMON_PATH)/base64.o \
			$(COMMON_PATH)/md5.o
	$(CC) $^ -o $@ $(LIBS)
# 注册
$(register):	$(CGI_SRC_PATH)/reg_cgi.o \
				$(COMMON_PATH)/make_log.o  \
				$(COMMON_PATH)/util_cgi.o \
				$(COMMON_PATH)/cJSON.o \
				$(COMMON_PATH)/deal_mysql.o \
				$(COMMON_PATH)/redis_op.o  \
				$(COMMON_PATH)/cfg.o \
				$(COMMON_PATH)/md5.o
	$(CC) $^ -o $@ $(LIBS)
# 秒传
$(md5):		$(CGI_SRC_PATH)/md5_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)
# 上传
$(upload):$(CGI_SRC_PATH)/upload_cgi.o \
		  $(COMMON_PATH)/make_log.o  \
		  $(COMMON_PATH)/util_cgi.o \
		  $(COMMON_PATH)/cJSON.o \
		  $(COMMON_PATH)/deal_mysql.o \
		  $(COMMON_PATH)/redis_op.o  \
		  $(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)
# 用户列表展示
$(myfiles):	$(CGI_SRC_PATH)/myfiles_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)
# 分享、删除文件、pv字段处理
$(dealfile):$(CGI_SRC_PATH)/dealfile_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o \
			$(COMMON_PATH)/md5.o
	$(CC) $^ -o $@ $(LIBS)
# 共享文件列表展示
$(sharefiles):	$(CGI_SRC_PATH)/sharefiles_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)
# 共享文件pv字段处理、取消分享、转存文件
$(dealsharefile):	$(CGI_SRC_PATH)/dealsharefile_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)

# 图床分享图片功能
$(sharepicture):	$(CGI_SRC_PATH)/sharepicture_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o \
			$(COMMON_PATH)/md5.o
	$(CC) $^ -o $@ $(LIBS)
# 分片上传初始化
$(chunk_init):	$(CGI_SRC_PATH)/chunk_init_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)
# 分片上传
$(chunk_upload):	$(CGI_SRC_PATH)/chunk_upload_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)
# 分片合并
$(chunk_merge):	$(CGI_SRC_PATH)/chunk_merge_cgi.o \
			$(COMMON_PATH)/make_log.o  \
			$(COMMON_PATH)/util_cgi.o \
			$(COMMON_PATH)/cJSON.o \
			$(COMMON_PATH)/deal_mysql.o \
			$(COMMON_PATH)/redis_op.o  \
			$(COMMON_PATH)/cfg.o
	$(CC) $^ -o $@ $(LIBS)
# AI 智能检索（C++ 编译）
$(CGI_SRC_PATH)/ai_cgi.o: $(CGI_SRC_PATH)/ai_cgi.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(CPPLFAGS)

$(CGI_SRC_PATH)/dashscope_api.o: $(COMMON_PATH)/dashscope_api.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(CPPLFAGS)

$(CGI_SRC_PATH)/faiss_wrapper.o: $(COMMON_PATH)/faiss_wrapper.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(CPPLFAGS)

$(ai): $(CGI_SRC_PATH)/ai_cgi.o \
	   $(CGI_SRC_PATH)/dashscope_api.o \
	   $(CGI_SRC_PATH)/faiss_wrapper.o \
	   $(COMMON_PATH)/make_log.o \
	   $(COMMON_PATH)/util_cgi.o \
	   $(COMMON_PATH)/cJSON.o \
	   $(COMMON_PATH)/deal_mysql.o \
	   $(COMMON_PATH)/redis_op.o \
	   $(COMMON_PATH)/cfg.o \
	   $(COMMON_PATH)/md5.o
	$(CXX) $^ -o $@ $(LIBS) $(AI_LIBS)
# =====================================================================


#######################################################################
#                         所有程序都需要的规则
# 生成所有的.o 文件
%.o:%.c
	$(CC) -c $< -o $@ $(CPPLFAGS) $(CFLAGS)

# 项目清除
clean:
	-rm -rf *.o $(target) $(TEST_PATH)/*.o $(CGI_SRC_PATH)/*.o $(COMMON_PATH)/*.o

# 声明伪文件
.PHONY:clean ALL
#######################################################################
