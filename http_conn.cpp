#include"http_conn.h"

int http_conn :: m_epollfd = 1;       // 所有socket上的事件都被注册到同一个eollfd上
int http_conn :: m_user_count = 0;


// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossble to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


// 网站的根目录
const char* doc_root = "/disk/sda/fx/linux/web_server/resources";

// 此处我使用的void 牛客是int 有返回值
// 设置文件描述符非阻塞
void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);

}

// 将文件描述符添加到epoll对象中
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd   = fd;
    event.events    = EPOLLIN | EPOLLRDHUP;     // 要检测的任务是读和挂起
    // event.events    = EPOLLIN | EPOLLRDHUP | EPOLLET;

    // one_shot机制，使文件描述符同一时间只能触发一个事件，但同时监听的文件描述符不能用oneshot
    if(one_shot) {
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中删除文件描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符, 重置socket上的EPOLLONESHOT事件，确保下一次可读时EPOLLIN事件能触发     
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd   = fd;
    event.events    = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
    // 此处多添加一个一个EPOLLET；
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 外部调用，初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_socketfd    = sockfd;
    m_address     = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_socketfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll中`
    addfd(m_epollfd, m_socketfd, true);
    m_user_count++; // 用户数+1

    init(); // 分开init是因为可能会单独初始化此部分，不初始化上上面的初始化
}

// 对http部分的初始化
void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;        // 初始化状态为解析请求首行
    m_checked_idx = 0;                              // 正在解析字符的位置
    m_start_line = 0;                               // 正在解析的行的起始位置
    m_read_idx = 0;                                 // 读缓冲区已读入数据最后一个字节数据的下标
    m_url = 0;                                      // 请求目标文件的文件名
    m_version = 0;                                  // 协议版本， 只支持http1.1
    m_method = GET;                                 // 请求方法             GET/POST
    m_linger = false;                               // 默认不保持链接，     状态有二：keep-alive; close
    m_write_idx = 0;                                // 写缓冲区中待发送的字节数
    m_content_length = 0;                           // 响应体的总大小
    m_host = 0;                                     // 客户端主机



    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);               // 文件
}

void http_conn::close_conn() {
    if(m_socketfd != -1) {
        removefd(m_epollfd, m_socketfd);
        m_socketfd = -1;
        m_user_count --;
    }
}

// 循环读取客户数据，直到无数据可读，或者对方关闭连接 
bool http_conn::read() {
    // 非阻塞读
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;       // 判断如果已读的数据超过读缓冲区大小，返回
    }

    // 已经读取到的字节
    int bytes_read = 0;
    while(true) {
        // 为了数据的连续性，数据保存在数组+序号的位置，得到的就是连续的数据；
        // recv 参数：1.连接的套接字；2.指向缓冲区的指针；3.缓冲区的长度；4.行为标识符    返回值：读出来的数据大小
        // 数组的大小就是缓冲区的大小减去已经读到的大小
        bytes_read = recv(m_socketfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据未到达，因为阻塞问题或者延时问题，不打紧继续读就行
                break;
            }
            // 真正出现错误
            return false;
        } else if(bytes_read == 0) {
            // 对方关闭连接
            return false;
        }
        // 索引向后移动
        m_read_idx += bytes_read;
    }
    printf("读取到的数据：%s\n", m_read_buf);
    return true;
}   

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read(){
    // 初始化一些状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK)) {
    // while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
    // || ((line_status = parse_line()) == LINE_OK)) {
        // 解析到了请求体，是完整的数据
        // 或者解析到了一行完整的数据
        // 下面要获取一行数据
        text = get_line();

        m_start_line = m_checked_idx;
        printf("got 1 http line : %s\n", text);

        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret == GET_REQUEST) return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                break;
            }  
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}                   


// 获取一行内容提供给后续解析，内容判断依据： \r\n
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    // checked_idx指向buf中当前正在分析的字节，read_idx指向buf中客户数据的尾部的下一字节，
    // buf中第0~checked_index字节都已分析完毕，第checked_index~(read_index-1)字节由下面的循环挨个分析
    for(; m_checked_idx < m_read_idx; ++m_checked_idx) {
        // 从0开始读
        temp = m_read_buf[m_checked_idx];
        // \r是回车  \n 是换行
        if(temp == '\r') {
            // 如果读到的\r之后，下一个字符是已读数据的末尾索引，说明数据不完整
            if((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_idx + 1] == '\n') {
                // 说明换行了,将\r\n置为\0，返回数据正常
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n') {
            // 如果前一个字符是\r，还是\r\n，将二者置\0
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] == '\0';
                m_read_buf[m_checked_idx++] == '\0';
                // 行正确
                return LINE_OK;
            }
            // \n之前不是\r，行错误
            return LINE_BAD;
        }
        
    }
    // 没读完继续读
    return LINE_OPEN;
    // // 读完了，行正确
    // return LINE_OK;
}

// 解析请求首行 获得请求方法，目标url,http版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    //  char *strpbrk(const char *str1, const char *str2) 检索字符串 str1 中第一个匹配字符串 str2 中字符的字符,返回位置
    // 检测空格 和 \t， 此时m_url位置在GET后空格的位置
    m_url = strpbrk(text, " \t"); 
    // 将空格变为'\0',并向后移动一位 :  GET\0/index.html HTTP/1.1,      此时m_url并指向-> /
    *m_url++ = '\0';

    char *method = text;
    // strcasecmp, 判断字符串是否相等的函数，忽略大小写，返回值小于0则s1小于s2，大于0则s1大于s2，等于则等于
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
        printf("The request method is GET\n");
    }else {
        return BAD_REQUEST;
    }
    // 此时m_version为： HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    // http://10.15.1.252:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0) {
        // 10.15.1.252:10000/index.html
        m_url += 7;
        // index.html
        // strchr 用于查找字符串的一个字符，返回字符在字符串中第一次出现的位置
        m_url = strchr(m_url, '/');
    }


    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    printf("此时在parse_request_line中，此时的m_url为：%s\n", m_url);
    printf("The request URL is :%s\t, the version is %s\n", m_url, m_version);

    m_check_state = CHECK_STATE_HEADER;     // 主状态机转换状态，解析请求头

    return NO_REQUEST;
}        

http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    // 遇到空行 说明头部已经解析完毕了，因为请求头和请求内容之间有一个空行
    if(text[0] == '\0') {
        // 不等于0则有请求体， 主状态机转为CHECK_STATE_CONTENT状态,请求还没读完
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到一个完整的HTTP请求了
        return GET_REQUEST;
    // 还没有解析完 继续解析
    }else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;      // 此时text指向空格
        // strspn   检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
        text += strspn(text, " \t");       // 此时得到m_host指向IP地址和端口号
        m_host = text;
        printf("The request Host is : %s\n", m_host);
    }else if(strncasecmp(text, "connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }else {
        printf("Cannot parse header%s\n", text);
    }
    return NO_REQUEST;
} 

http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 当一个完整的、正确的HTTP请求时，我们分析目标文件的属性
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存上，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
    // 服务器的资源目录为： /disk/sda/fx/linux/web_server/resources
    strcpy(m_real_file, doc_root);
    // len 是根目录的长度
    int len = strlen(doc_root);
    // 拼接，将url与资源目录拼接，得到具体的路径
    // 如 ：   /disk/sda/fx/linux/web_server/resources + /index.html
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    printf("此时请求的m_url是： %s\n", m_url);
    printf("此处是m_real_file的地址：%s\n", m_real_file);
    // stat函数： 获取文件信息，参数：1.文件路径；2.stat类的结构体
    // 获取m_real_flie文件相关的状态信息， -1 失败； 0成功
    if(stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if(! (m_file_stat.st_mode & S_IROTH)) {
        return BAD_REQUEST;
    }

    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射, 请求体内容映射到了m_file_address
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    printf("此时开辟的内存映射区地址为：%s\n", m_file_address);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行unmap操作
void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write(){
    int temp = 0;
    int bytes_have_sent = 0;            // 已经发送的字节数
    int bytes_to_send = m_write_idx;    // 将要发送的字节数  m_write_idx是写缓冲区中待发送的字节数

    if(bytes_to_send == 0) {
        // 将要发送的字节数为0， 即已经发送完了,将epoll监测事件换为EPOLLIN
        modfd(m_epollfd, m_socketfd, EPOLLIN);
        init();
        return true;
    }

    while(1) {
        // writev  参数：1.文件描述符；2.iovec结构的结构体，里面有要写的数据的地址和长度；3.指定iovec的个数  返回值：失败-1，成功返回写的字节数
        // 分散写， 将写缓冲区和内存映射地址一块写出去



        //  没有进行writev的判断，直接让temp = 0， bytes_to_send和bytes_have_sent永远不会变，所以出了问题



        temp = writev(m_socketfd, m_iv, m_iv_count);
        if(temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                modfd(m_epollfd, m_socketfd, EPOLLOUT);
                return true;
            }
            // 写失败
            printf("写失败\n");
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_sent += temp;
        if(bytes_to_send <= bytes_have_sent) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_linger) {
                init();
                modfd(m_epollfd, m_socketfd, EPOLLIN);
                return true;
            }else {
                modfd(m_epollfd, m_socketfd, EPOLLIN);
                return false;
            }
        }
    }
    return true;
}

bool http_conn::add_response(const char* format, ...) {
    // 写的数据大于写缓冲区最大值
    if(m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    /*
    （1）首先在函数里定义一具VA_LIST型的变量，这个变量是指向参数的指针
    （2）然后用VA_START宏初始化变量刚定义的VA_LIST变量，这个宏的第二个参数是第一个可变参数的前一个参数，是一个固定的参数。
    （3）然后用VA_ARG返回可变的参数，VA_ARG的第二个参数是你要返回的参数的类型。
    （4）最后用VA_END宏结束可变参数的获取。然后你就可以在函数里使用第二个参数了。如果函数有多个可变参数的，依次调用VA_ARG获取各个参数。
    */       
    va_list arg_list;
    va_start(arg_list, format);
    // vsprintf   用于向一个字符串缓冲区打印格式化字符串，且可以限定打印的格式化字符串的最大长度。  
    // 参数：1.要写入的字符数组； 2.字符个数减1； 3.格式化限定字符串 4.可变长度参数列表    返回值：成功打印到字符串中的字符个数
    // 从写缓冲区的每行地址处开始写，缓冲区剩余空间大小， 格式化限定字符串， 可变长度参数列表
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1- m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)){
        return false;
    }
    // 地址指针指向新的地址
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_length) {
    add_content_length(content_length);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger = true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}


bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(add_content(error_400_form)){
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(add_content(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(add_content(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base    = m_write_buf;
            m_iv[0].iov_len     = m_write_idx;
            m_iv[1].iov_base    = m_file_address;
            m_iv[1].iov_len     = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 由线程池的工作函数调用
void http_conn::process () {
    // 解析http请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_socketfd, EPOLLIN);
        return;
    }

    // 生成相应
    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_socketfd, EPOLLOUT);
}


