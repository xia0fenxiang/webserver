#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include"locker.h"
#include"threadpool.h"
#include<signal.h>
#include"http_conn.h"

// 定义最大文件描述符个数
#define MAX_FD 65535
// 定义最大监听事件数量
#define MAX_EVENT_NUMBER 10000 // 监听最大数量


// 添加信号捕捉，参数：处理什么信号， 怎么处理信号
void addsig(int sig, void(handler)(int)) {

    // 创建sigaction结构体
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 将文件描述符添加到epoll对象中
extern void addfd(int epollfd, int fd, bool one_shot);

// 从epoll中删除文件描述符
extern void remvefd(int epollfd, int fd);

// 修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

// 传入参数用
int main(int argc, char* argv[]) {

    if(argc <= 1) {
        printf("按照如下格式运行：%s port_number\n", basename(argv[0]));
        exit(-1);
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 对sigpie信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，并初始化
    threadpool<http_conn>* pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...) {
        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];

    // socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1) {
        perror("socket");
        exit(-1);
    }

    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in saddr;
    saddr.sin_family    = AF_INET;
    saddr.sin_port      = htons(port);
    saddr.sin_addr.s_addr = INADDR_ANY;
    if(bind(listenfd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) {
        perror("bind");
        exit(-1);
    }

    if(listen(listenfd, 5) == -1) {
        perror("listen");
        exit(-1);
    }

    // 创建epoll对象，和事件数组，添加监听文件描述符
    epoll_event events[MAX_EVENT_NUMBER];

    // 创建epoll对象
    int epollfd = epoll_create(5);

    // 将监听的文件描述符添加到epoll中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd    = epollfd;
    
    while(true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((num < 0) && (errno != EINTR)) {
            printf("epoll failure!\n");
            break;
        }

        // 循环遍历事件数组
        for(int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd) {
                // 有客户端连接进来
                struct sockaddr_in clientaddr;
                socklen_t clientaddr_len = sizeof(clientaddr);
                
                int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &clientaddr_len);

                if(http_conn::m_user_count >= MAX_FD) {
                    // 当前连接数大于等于最大FD连接数，服务器满了
                    // 给客户端信息，服务器正忙
                    close(connfd);
                    continue;
                }
                // 将新的客户数据初始化，放入数组中
                users[connfd].init(connfd, clientaddr);

            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // printf("这里是epoll检测到客户挂起或者错误，断开连接的地方，接下来继续循环检测epoll\n");
                // 对方异常断开或者错误
                users[sockfd].close_conn();
            }else if (events[i].events & EPOLLIN) {
                // printf("这里是epoll检测到客户数据到了下面会一次性读入数据，并添加任务到线程池\n");
                if(users[sockfd].read()) {
                    // 一次性把所有数据都读出来
                    pool->append(users + sockfd);
                }else {
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT) {
                // printf("这里是检测到写操作，一次性写入所有数据\n");
                if(!users[sockfd].write()) {
                    // 一次性写完所有的数据,如果没写
                    users[sockfd].close_conn();
                }
            }

        }

    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;

    return 0;
}