/***************************************
* @file     tcpsocket.h
* @brief    基于libuv封装的tcp服务器与客户端,使用log4z作日志工具
* @details
* @author   陈吉宏, wqvbjhc@gmail.com
* @date     2014-05-13
* @mod      2014-05-13  phata  修正服务器与客户端的错误.现服务器支持多客户端连接
                               修改客户端测试代码，支持并发多客户端测试
			2014-05-23  phata  原服务器与客户端只接收裸数据，现改为接收NetPacket(定义net_base.h)封装的数据。接收回调为解析后的数据，但发送需要用户自己封闭成NetPacket后发送
			                   修改server_recvcb的定义，添加NetPacket参数
							   修改client_recvcb的定义，添加NetPacket参数
							   申请uv_write_t列表空间用于send
			2014-05-27  phata  clientdata更名为AcceptClient，并丰富了其功能.
			                   使用异步发送机制，可以在其他线程中调用服务器与客户端的send函数
							   修改之前测试发现崩溃的情况
							   BUFFER_SIZE由1M改为10K，一个client需要6倍BUFFER_SIZE.一个client内部会启动2个线程
****************************************/
#ifndef TCPSocket_H
#define TCPSocket_H
#include <string>
#include <list>
#include <map>
#include "uv.h"
#include "net/packet.h"
#ifndef BUFFER_SIZE
#define BUFFER_SIZE (1024*10)
#endif

//#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
//
//#define container_of(ptr, type, member) \
//	((type *) ((char *) (ptr) - offsetof(type, member)))

namespace uv
{
class TCPServer;
class AcceptClient;

typedef void (*tcp_closed)(int clientid, void* userdata);//tcp handle关闭后回调给上层
typedef void (*newconnect)(int clientid);//TCPServer接收到新客户端回调给用户
typedef void (*server_recvcb)(int clientid, const NetPacket& packethead, const unsigned char* buf, void* userdata);//TCPServer接收到客户端数据回调给用户
typedef void (*client_recvcb)(const NetPacket& packethead, const unsigned char* buf, void* userdata);//TCPClient接收到服务器数据回调给用户

typedef struct _sendparam_ {
    uv_async_t async;
    uv_sem_t semt;
    char* data;
    int len;
} sendparam;

/************************************************************服务器带的客户端数据***************************************************************************/
/*************************************************
功能：TCP Server Accept中获取的client对象
调用方法：
设置回调函数SetRecvCB/SetClosedCB
new一对象(不能使用直接定义对象的方法)
调用AcceptByServer
调用send发送数据(可选)
调用run启动(可选，若loop在其他地方已经run过，则不需要调用)
调用Stop停止类中线程
在回调closedcb_中delete对象
*************************************************/
class AcceptClient
{
public:
    AcceptClient(int clientid, char packhead, char packtail, uv_loop_t* loop = uv_default_loop());
    virtual ~AcceptClient();
    bool Start(char packhead, char packtail);
    void Stop();
    bool AcceptByServer(uv_tcp_t* server);

    void SetRecvCB(server_recvcb pfun, void* userdata);//设置接收数据回调函数
    void SetClosedCB(tcp_closed pfun, void* userdata);//设置接收关闭事件的回调函数

    int Send(const char* data, std::size_t len);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
private:
    uv_loop_t* loop_;

    bool isinit_;
    int client_id_;//客户端id,惟一,由TCPServer负责赋值
    uv_tcp_t client_handle_;//客户端句柄

    server_recvcb recvcb_;//接收数据回调给用户的函数
    void *recvcb_userdata_;

    tcp_closed closedcb_;//关闭后回调给TCPServer
    void *closedcb_userdata_;

    //接收数据参数
    uv_buf_t readbuffer_;//接受数据的buf
    Packet readpacket_;//接受buf数据解析成包

    //发送数据参数
    uv_thread_t writethread_handle_;//发送线程句柄
    bool is_writethread_stop_;//控制发送线程退出
    uv_mutex_t mutex_writebuf_;//控制writebuf_list_
    PodCircularBuffer<char> writebuf_list_;//发送数据队列
    sendparam sendparam_;
    std::list<uv_write_t*> writereq_list_;//可用的uv_write_t
    uv_mutex_t mutex_writereq_;//控制writereq_list_

    std::string errmsg_;//保存错误信息
private:
    static void WriteThread(void *arg);//发送数据线程
    static void AsyncSendCB(uv_async_t* handle);//异步发送数据
    static void AfterSend(uv_write_t *req, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
    static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//解析得一完整数据包回调给用户
};



/****************************************************************服务器**********************************************************************************/
/*************************************************
功能：TCP Server
调用方法：
设置回调函数setnewconnectcb/setrecvcb

调用StartLog启用日志功能(可选)
调用Start/Start6启动服务器
调用send发送数据(可选)
调用close停止类中线程
*************************************************/
class TCPServer
{
public:
    TCPServer(char packhead, char packtail, uv_loop_t* loop = uv_default_loop());
    virtual ~TCPServer();
    static void StartLog(const char* logpath = nullptr);//启动日志，必须启动才会生成日志
public:
    //基本函数
    bool Start(const char *ip, int port);//启动服务器,地址为IP4
    bool Start6(const char *ip, int port);//启动服务器，地址为IP6
    void Close();

    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

    int  Send(int clientid, const char* data, std::size_t len);
    void SetNewConnectCB(newconnect cb);
    void SetRecvCB(int clientid,server_recvcb cb, void *userdata);//设置接收回调函数，每个客户端各有一个
protected:
    int GetAvailaClientID()const;//获取可用的client id
    //静态回调函数
    static void AfterServerClose(uv_handle_t *handle);
    static void AcceptConnection(uv_stream_t *server, int status);
    static void SubClientClosed(int clientid,void* userdata);//AcceptClient关闭后回调给TCPServer

private:
    bool init();
    bool run(int status = UV_RUN_DEFAULT);
    bool bind(const char* ip, int port);
    bool bind6(const char* ip, int port);
    bool listen(int backlog = SOMAXCONN);

    uv_loop_t *loop_;
    uv_tcp_t server_;//服务器链接
    bool isinit_;//是否已初始化，用于close函数中判断
    std::map<int,AcceptClient*> clients_list_;//子客户端链接
    uv_mutex_t mutex_clients_;//保护clients_list_

    newconnect newconcb_;//回调函数
    std::string errmsg_;//错误信息

    char PACKET_HEAD;//包头
    char PACKET_TAIL;//包尾
};


/***********************************************************************客户端***************************************************************************/
/*************************************************
功能：TCP Server Accept中获取的client对象
调用方法：
设置回调函数SetRecvCB/SetClosedCB
new一对象(不能使用直接定义对象的方法)
调用Connect/Connect6函数
调用send发送数据(可选)
调用Close停止类中线程
在回调closedcb_中delete对象
*************************************************/
class TCPClient
{
public:
    TCPClient(char packhead, char packtail, uv_loop_t* loop = uv_default_loop());
    virtual ~TCPClient();
    static void StartLog(const char* logpath = nullptr);//启动日志，必须启动才会生成日志
public:
    //基本函数
    bool Connect(const char* ip, int port);//启动connect线程，循环等待直到connect完成
    bool Connect6(const char* ip, int port);//启动connect线程，循环等待直到connect完成
    int  Send(const char* data, std::size_t len);
    void SetRecvCB(client_recvcb pfun, void* userdata);////设置接收回调函数，只有一个
	void SetClosedCB(tcp_closed pfun, void* userdata);//设置接收关闭事件的回调函数
    void Close();

    //是否启用Nagle算法
    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
protected:
    bool init();
    bool run(int status = UV_RUN_DEFAULT);
    static void ConnectThread(void* arg);//真正的connect线程
    static void ConnectThread6(void* arg);//真正的connect线程
    
	static void WriteThread(void *arg);//发送数据线程
	static void AsyncSendCB(uv_async_t* handle);//异步发送数据
    static void AfterConnect(uv_connect_t* handle, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AfterSend(uv_write_t *req, int status);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
	static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//解析完一帧后的回调函数

private:
    enum {
        CONNECT_TIMEOUT,
        CONNECT_FINISH,
        CONNECT_ERROR,
        CONNECT_DIS,
    };
    uv_tcp_t client_handle_;//客户端连接
    uv_loop_t *loop_;
    bool isinit_;//是否已初始化，用于close函数中判断
	uv_thread_t connect_threadhandle_;

    uv_connect_t connect_req_;//连接时请求

    int connectstatus_;//连接状态

    //接收数据参数
    uv_buf_t readbuffer_;//接受数据的buf
    Packet readpacket_;//接受buf数据解析成包

    //发送数据参数
    uv_thread_t writethread_handle_;//发送线程句柄
    bool is_writethread_stop_;//控制发送线程退出
    uv_mutex_t mutex_writebuf_;//控制writebuf_list_
    PodCircularBuffer<char> writebuf_list_;//发送数据队列
    sendparam sendparam_;
    std::list<uv_write_t*> writereq_list_;//可用的uv_write_t
    uv_mutex_t mutex_writereq_;//控制writereq_list_

    client_recvcb recvcb_;//回调函数
    void* recvcb_userdata_;//回调函数的用户数据

	tcp_closed closedcb_;//关闭后回调给TCPServer
	void *closedcb_userdata_;

    std::string connectip_;//连接的服务器IP
    int connectport_;//连接的服务器端口号

    std::string errmsg_;//错误信息

    char PACKET_HEAD;//包头
    char PACKET_TAIL;//包尾
};

/***********************************************************************辅助函数***************************************************************************/
/*****************************
* @brief   把数据组合成NetPacket格式的二进制流，可直接发送。
* @param   packet --NetPacket包，里面的version,header,tail,type,datalen,reserve必须提前赋值，该函数会计算check的值。然后组合成二进制流返回
	       data   --要发送的实际数据
* @return  std::string --返回的二进制流。地址：&string[0],长度：string.length()
******************************/
std::string PacketData(NetPacket& packet, const unsigned char* data);
}


#endif // TCPSocket_H