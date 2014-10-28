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
			2015-01-06  phata  使用uv_walk关闭各handle,整个loop关闭回调在run返回后触发。
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

namespace uv
{
/***************************************************************Server*******************************************************************************/
class AcceptClient;
typedef struct _tcpclient_ctx {
    uv_tcp_t tcphandle;//data filed store this
    PacketSync* packet_;//userdata filed storethis
    uv_buf_t read_buf_;
    int clientid;
    void* parent_server;//tcpserver
    void* parent_acceptclient;//accept client
} TcpClientCtx;
TcpClientCtx* AllocTcpClientCtx(void* parentserver);
void FreeTcpClientCtx(TcpClientCtx* ctx);

typedef struct _write_param { //the param of uv_write
	uv_write_t write_req_;
    uv_buf_t buf_;
    int buf_truelen_;
} write_param;
write_param* AllocWriteParam(void);
void FreeWriteParam(write_param* param);

/*************************************************
Fun：TCP Server
Usage：
Start the log fun(optional): StartLog
Set the call back fun      : SetNewConnectCB/SetRecvCB/SetClosedCB
SetPortocol                : SetPortocol. The send&recv data fun all in TCPServerProtocolProcess. user must inherit it and implement the method you need. 
Start Server               : Start/Start6
SetNoDelay(optional)       : SetNoDelay
SetKeepAlive(optional)     : SetKeepAlive
Close Server               : Close. this fun only set the close command, call IsClosed to verify real closed.
                             or verify in the call back fun which SetRecvCB set.
Stop the log fun(optional) : StopLog
GetLastErrMsg(optional)    : when the above fun call failure, call this fun to get the error message.
*************************************************/
class TCPServer
{
public:
    TCPServer(char packhead, char packtail);
    virtual ~TCPServer();
	//Start/Stop the log
    static void StartLog(const char* logpath = nullptr);
	static void StopLog();
public:
    void SetNewConnectCB(NewConnectCB cb, void* userdata);//set new connect cb.
    void SetRecvCB(int clientid, ServerRecvCB cb, void* userdata); //set recv cb. call for each accept client.
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//set close cb.
	void SetPortocol(TCPServerProtocolProcess *pro);

    bool Start(const char* ip, int port);//Start the server, ipv4
    bool Start6(const char* ip, int port);//Start the server, ipv6
    void Close();//send close command. verify IsClosed for real closed
    bool IsClosed() {//verify if real closed
        return isclosed_;
    };

    //Enable or disable Nagle’s algorithm. must call after Server succeed start.
    bool SetNoDelay(bool enable);

	//Enable or disable KeepAlive. must call after Server succeed start.
	//delay is the initial delay in seconds, ignored when enable is zero
    bool SetKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

protected:
    int GetAvailaClientID()const;
    //Static callback function
    static void AfterServerClose(uv_handle_t* handle);
    static void DeleteTcpHandle(uv_handle_t* handle);//delete handle after close client
    static void RecycleTcpHandle(uv_handle_t* handle);//recycle handle after close client
    static void AcceptConnection(uv_stream_t* server, int status);
    static void SubClientClosed(int clientid, void* userdata); //AcceptClient close cb
    static void AsyncCloseCB(uv_async_t* handle);//async close
	static void CloseWalkCB(uv_handle_t* handle, void* arg);//close all handle in loop

private:
    enum {
        START_TIMEOUT,
        START_FINISH,
        START_ERROR,
        START_DIS,
    };

    bool init();
    void closeinl();//real close fun
    bool run(int status = UV_RUN_DEFAULT);
    bool bind(const char* ip, int port);
    bool bind6(const char* ip, int port);
    bool listen(int backlog = SOMAXCONN);
    bool sendinl(const std::string& senddata, TcpClientCtx* client);
    bool broadcast(const std::string& senddata, std::vector<int> excludeid);//broadcast to all clients, except the client who's id in excludeid
    uv_loop_t loop_;
    uv_tcp_t tcp_handle_;
    uv_async_t async_handle_close_;
    bool isclosed_;
    bool isuseraskforclosed_;

    std::map<int, AcceptClient*> clients_list_; //clients map
    uv_mutex_t mutex_clients_;//clients map mutex

    TCPServerProtocolProcess* protocol_;//protocol

    uv_thread_t start_threadhandle_;//start thread handle
    static void StartThread(void* arg);//start thread,run until use close the server
    int startstatus_;

    std::string errmsg_;

    NewConnectCB newconcb_;
    void* newconcb_userdata_;

    TcpCloseCB closedcb_;
    void* closedcb_userdata_;

    std::string serverip_;
    int serverport_;

    char packet_head;//protocol head
    char packet_tail;//protocol tail

    std::list<TcpClientCtx*> avai_tcphandle_;//Availa accept client data
    std::list<write_param*> writeparam_list_;//Availa write_t

public:
    friend static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    friend static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
    friend static void AfterSend(uv_write_t* req, int status);
    friend static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);
};

/***********************************************Accept client on Server**********************************************************************/
/*************************************************
Fun: The accept client on server
Usage:
Set the call back fun:      SetRecvCB/SetClosedCB
Close it             :      Close. this fun only set the close command, verify real close in the call back fun which SetRecvCB set.
GetTcpHandle         :      return the client data to server.
GetLastErrMsg        :      when the above fun call failure, call this fun to get the error message.
*************************************************/
class AcceptClient
{
public:
	//control: accept client data. handle by server
    //loop:    the loop of server
    AcceptClient(TcpClientCtx* control, int clientid, char packhead, char packtail, uv_loop_t* loop);
    virtual ~AcceptClient();

    void SetRecvCB(ServerRecvCB pfun, void* userdata);//set recv cb
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//set close cb.
    TcpClientCtx* GetTcpHandle(void) const;

    void Close();

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
private:
    bool init(char packhead, char packtail);

    uv_loop_t* loop_;
    int client_id_;

    TcpClientCtx* client_handle_;//accept client data
    bool isclosed_;
    std::string errmsg_;

    ServerRecvCB recvcb_;
    void* recvcb_userdata_;

    TcpCloseCB closedcb_;
    void* closedcb_userdata_;
private:
    static void AfterClientClose(uv_handle_t* handle);
public:
    friend static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    friend static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
    friend static void AfterSend(uv_write_t* req, int status);
    friend static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);
};

//Global Function
static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
static void AfterSend(uv_write_t* req, int status);
static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);
}


#endif // TCPSERVER_H