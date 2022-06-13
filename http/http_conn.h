#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include "../lock/lock.h"
#include "../userdata/redis.h"
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace std;

class http_conn //http 连接类
{
public:
    static const int BUFF_READ_SIZE = 2048;  //读缓冲大小
    static const int BUFF_WRITE_SIZE = 2048; //写缓冲大小
    enum METHOD //报文请求方法
    {
        GET = 0,
        POST
    };
    enum MAIN_STATE //  主状态机状态
    {
        REQUESTLINE = 0,
        HEADER,
        CONTENT
    };
    enum LINE_STATE // 从状态机状态
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        POST_REQUEST,
        NO_RESOURCE,       //NOT DONE
        CLOSED_CONNECTION  //NOT DONE
    };

public:
    http_conn(){};
    ~http_conn(){};

public:
    void init(int socketfd, const sockaddr_in &addr); //初始化套接字
    void init();                                      //实现具体各个参数值的初始化
    void close_conn(string msg = "");                 //关闭连接
    void process();                                   //处理请求
    bool read();                                      //一次性调用recv读取所有数据，读取浏览器发来的全部数据，读到读缓冲区,返回调用是否称成功的信息
    bool write();

public:
    static int m_epollfd;   //http连接的epoll描述符,这个是静态的,因为epoll监听红黑树只有一个
    static int m_user_count;//用户数目
    //分配用户名
    string user_id;

private:
    HTTP_CODE process_read();          //从读缓冲区读取出来数据进行解析
    bool process_write(HTTP_CODE ret); //写入响应到写缓冲区中

    void parser_requestline(const string &text, map<string, string> &m_map); //解析请求行
    void parser_header(const string &text, map<string, string> &m_map);      //解析请求头部
    void parser_postinfo(const string &text, map<string, string> &m_map);    //解析post请求正文

    bool do_request(); //确定到底请求的是哪一个页面

    void unmap();

private:
    locker m_redis_lock;
    int m_socket;        //服务器里创建的属于这个http连接的套接字
    sockaddr_in m_addr;  //client的地址信息

    struct stat m_file_stat;
    
    struct iovec m_iovec[3];
    int m_iovec_length;
    
    string filename;
    string postmsg;  //post报文里的请求内容
    char *file_addr; //mmap指向共享内存区域的指针
    char post_temp[];
    
    char read_buff[BUFF_READ_SIZE];   //每个http连接都有一个读缓冲区
    char write_buff[BUFF_WRITE_SIZE]; //每个http连接都有一个写缓冲区
    int read_for_now = 0;
    int write_for_now = 0;

    map<string, string> m_map; //http请求报文的各项属性
    
};

#endif
