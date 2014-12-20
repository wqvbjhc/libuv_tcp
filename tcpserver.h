/***************************************
* @file     tcpserver.h
* @brief    基于libuv封装的tcp服务器与客户端,使用log4z作日志工具
* @details
* @author   phata, wqvbjhc@gmail.com
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
			2014-07-24  phata  从tcpsocket中分离出TCPServer。
							   单独线程实现libuv的run(事件循环)，任何libuv相关操作都在此线程中完成。因此TCPServer可以多线程中任意调用
							   一个client需要4倍BUFFER_SIZE(readbuffer_,writebuffer_,writebuf_list_,readpacket_),启动一个线程(readpacket_内部一个)
			2014-11-01  phata  由于运行起来CPU负荷高，决定改进：
							   1.去掉prepare,check,idle事件
							   2.prepare里的判断用户关闭tcp由uv_async_send代替
							   3.prepare里的删除多余空闲handle,write_t不需要。回收空闲handle,write_t时判断是否多出预计，多时不回收，直接释放。
							   AcceptClient也同样进行改进.AcceptClient不需要Close,直接close_inl就行
			2014-11-08  phata  加入了广播功能
							   启动一个timer检测任务的启动与停止
			2014-11-20  phata  把增删改信息广播给其他客户端，非直接广播所有信息
		    2014-12-11  phata  SendAlarm没触发，修正
****************************************/
#ifndef TCPSERVER_H
#define TCPSERVER_H
#include <string>
#include <list>
#include <map>
#include <vector>
#include "uv.h"
#include "net/packet_sync.h"
#include "tcpserverprotocolprocess.h"
#ifndef BUFFER_SIZE
#define BUFFER_SIZE (1024*10)
#endif

//#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
//
//#define container_of(ptr, type, member) \
//	((type *) ((char *) (ptr) - offsetof(type, member)))

namespace uv
{
/***************************************************************服务器*******************************************************************************/
/*************************************************
功能：TCP Server
调用方法：
设置回调函数SetNewConnectCB/SetRecvCB/SetClosedCB
调用StartLog启用日志功能(可选)
调用Start/Start6启动服务器
调用Close停止服务器，真正停止时会触发在回调SetRecvCB中所设置的函数
调用IsClosed判断客户端是否真正关闭了
*************************************************/
class AcceptClient;
typedef struct _tcpclient_ctx {
    uv_tcp_t tcphandle;//data存放this
    PacketSync* packet_;//userdata存放this
    uv_buf_t read_buf_;
    int clientid;
    void* parent_server;//所属tcpserver
    void* parent_acceptclient;//所属accept client
} TcpClientCtx;
TcpClientCtx* AllocTcpClientCtx(void* parentserver);
void FreeTcpClientCtx(TcpClientCtx* ctx);

typedef struct _write_param { //vu_write带的参数
    uv_write_t write_req_;
    uv_buf_t buf_;//需要释放
    int buf_truelen_;
} write_param;
write_param* AllocWriteParam(void);
void FreeWriteParam(write_param* param);

class TCPServer
{
public:
    TCPServer(char packhead, char packtail);
    virtual ~TCPServer();
    static void StartLog(const char* logpath = nullptr);//启动日志，必须启动才会生成日志
public:
    //基本函数
    void SetNewConnectCB(NewConnectCB cb, void* userdata);
    void SetRecvCB(int clientid, ServerRecvCB cb, void* userdata); //设置接收回调函数，每个客户端各有一个
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//设置接收关闭事件的回调函数
	void SetPortocol(TCPServerProtocolProcess *pro);

    bool Start(const char* ip, int port);//启动服务器,地址为IP4
    bool Start6(const char* ip, int port);//启动服务器，地址为IP6
    void Close();//用户触发关闭，IsClosed返回true才是真正关闭了
    bool IsClosed() {
        return isclosed_;
    };//判断客户端是否已关闭

    //是否启用Nagle算法
    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

protected:
    int GetAvailaClientID()const;//获取可用的client id
    //静态回调函数
    static void AfterServerClose(uv_handle_t* handle);
    static void DeleteTcpHandle(uv_handle_t* handle);//关闭后删除handle
    static void RecycleTcpHandle(uv_handle_t* handle);//关闭后回收handle
    static void AcceptConnection(uv_stream_t* server, int status);
    static void SubClientClosed(int clientid, void* userdata); //AcceptClient关闭后回调给TCPServer
    static void AsyncCloseCB(uv_async_t* handle);//async阶段回调,处理用户关闭tcpserver

private:
    enum {
        START_TIMEOUT,
        START_FINISH,
        START_ERROR,
        START_DIS,
    };

    bool init();
    void closeinl();//内部真正的清理函数
    bool run(int status = UV_RUN_DEFAULT);
    bool bind(const char* ip, int port);
    bool bind6(const char* ip, int port);
    bool listen(int backlog = SOMAXCONN);
    bool sendinl(const std::string& senddata, TcpClientCtx* client);
    bool broadcast(const std::string& senddata, std::vector<int> excludeid);//广播给所有客户端,excludeid中的客户端除外
    uv_loop_t loop_;
    uv_tcp_t tcp_handle_;//控制命令链接
    uv_async_t async_handle_close_;//异步handle,用于处理用户关闭命令
    bool isclosed_;//是否已初始化，用于close函数中判断
    bool isuseraskforclosed_;//用户是否发送命令关闭

    std::map<int, AcceptClient*> clients_list_; //所有子客户端链接
    uv_mutex_t mutex_clients_;//保护clients_list_

    TCPServerProtocolProcess* protocol_;//协议处理

    uv_thread_t start_threadhandle_;//启动线程
    static void StartThread(void* arg);//真正的Start线程,一直监听直到用户主动退出
    int startstatus_;//连接状态

    std::string errmsg_;//错误信息

    NewConnectCB newconcb_;//回调函数
    void* newconcb_userdata_;

    TcpCloseCB closedcb_;//关闭后回调给TCPServer
    void* closedcb_userdata_;

    std::string serverip_;//连接的IP
    int serverport_;//连接的端口号

    char packet_head;//包头
    char packet_tail;//包尾

    std::list<TcpClientCtx*> avai_tcphandle_;//可重用的tcp句柄
    std::list<write_param*> writeparam_list_;//可重用的write_t

public:
    friend static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    friend static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
    friend static void AfterSend(uv_write_t* req, int status);
    friend static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//解析得一完整数据包回调给用户
};

/***********************************************服务器带的客户端数据**********************************************************************/
/*************************************************
功能：TCP Server Accept中获取的client对象
调用方法：
设置回调函数SetRecvCB/SetClosedCB
调用AcceptByServer依附上服务器
调用Send发送数据(可选)
调用Close停止客户端，真正停止时会触发在回调closedcb_中delete对象SetClosedCB所设置的函数
调用IsClosed判断客户端是否真正关闭了
*************************************************/
class AcceptClient
{
public:
    AcceptClient(TcpClientCtx* control, int clientid, char packhead, char packtail, uv_loop_t* loop);//server的loop
    virtual ~AcceptClient();

    void SetRecvCB(ServerRecvCB pfun, void* userdata);//设置接收数据回调函数
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//设置接收关闭事件的回调函数
    TcpClientCtx* GetTcpHandle(void) const;;

    void Close();//内部真正的清理函数

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
private:
    bool init(char packhead, char packtail);

    uv_loop_t* loop_;//server所带的loop
    int client_id_;//客户端id,惟一,由TCPServer负责赋值

    TcpClientCtx* client_handle_;//控制客户端句柄
    bool isclosed_;//是否已关闭
    std::string errmsg_;//保存错误信息

    ServerRecvCB recvcb_;//接收数据回调给用户的函数
    void* recvcb_userdata_;

    TcpCloseCB closedcb_;//关闭后回调给TCPServer
    void* closedcb_userdata_;
private:
    static void AfterClientClose(uv_handle_t* handle);
public:
    friend static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    friend static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
    friend static void AfterSend(uv_write_t* req, int status);
    friend static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//解析得一完整数据包回调给用户
};

//全局函数
static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
static void AfterSend(uv_write_t* req, int status);
static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//解析得一完整数据包回调给用户

}


#endif // TCPSERVER_H