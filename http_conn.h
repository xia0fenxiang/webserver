#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include<sys/epoll.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<fcntl.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<errno.h>
#include"locker.h"
#include<sys/uio.h>
#include<string.h>
#include<sys/mman.h>
#include<stdarg.h>

class http_conn{
public:

    static int m_epollfd;       // 所有socket上的事件都被注册到同一个eollfd上
    static int m_user_count;    // 统计用户的数量
    static const int FILENAME_LEN = 200;        // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区的大小
    
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    http_conn() {};
    
    ~http_conn() {};

    // 初始化新接收的连接到users数组
    void init(int sockfd, const sockaddr_in &addr);

    // 处理客户端请求，先解析后做出响应
    void process();

    // 关闭链接
    void close_conn();

    bool read();    // 非阻塞读
    bool write();   // 非阻塞写

    
private:

    int m_socketfd;                     // 该http连接的socket
    sockaddr_in m_address;              // 通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE];  // 读缓冲区
    int m_read_idx;                     // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下标
    int m_checked_idx;                  // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;                   // 当前正在解析的行的起始位置

    char *m_url;                        // 请求目标文件的文件名
    char *m_version;                    // 协议版本， 只支持http1.1
    METHOD m_method;                    // 请求方法
    char *m_host;                       // 主机名
    bool m_linger;                      // 是否保持连接
    int m_content_length;               // 请求体的长度（单位字节）

    char m_real_file[FILENAME_LEN];     // 客户请求的目标文件的完整路径 doc_root + m_url
    struct stat m_file_stat;            // 目标文件的状态
    char *m_file_address;               // 内存映射的内存起始位置
    int m_write_idx;                    // 写缓冲区中待发送的字节数
    char m_write_buf[WRITE_BUFFER_SIZE];// 写缓冲区
    struct iovec m_iv[2];               // 用writev来执行的写，1对应写缓冲区，2对应内存映射区
    int m_iv_count;                     // 被写的内存块的数量


    void init();                        // 初始化连接其余的信息
    void unmap();                       // 释放内存映射

    // 这部分都是响应相关
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);// 添加状态行
    bool add_headers(int content_length);               // 添加响应头
    bool add_content(const char *content);              // 添加响应内容
    bool add_content_length(int content_length);        // 添加响应内容长度
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();

    CHECK_STATE m_check_state;                      //主状态机当前所处的状态

    HTTP_CODE process_read();                       // 解析http请求
    HTTP_CODE parse_headers(char* text);            // 解析请求头
    HTTP_CODE parse_request_line(char* text);       // 解析请求首行 
    HTTP_CODE parse_content(char* text);            // 解析请求体 

    LINE_STATUS parse_line();                       // 解析请求头 

    bool process_write(HTTP_CODE ret);              // 填充HTTP应答 
    
    char* get_line() { return m_read_buf + m_start_line;}  // 获取一行文本
    HTTP_CODE do_request();   // do_request

};













#endif