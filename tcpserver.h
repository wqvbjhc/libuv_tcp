/***************************************
* @file     tcpserver.h
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
			2014-07-24  phata  从tcpsocket中分离出TCPServer。
							   单独线程实现libuv的run(事件循环)，任何libuv相关操作都在此线程中完成。因此TCPServer可以多线程中任意调用
							   一个client需要4倍BUFFER_SIZE(readbuffer_,writebuffer_,writebuf_list_,readpacket_),启动一个线程(readpacket_内部一个)
****************************************/
#ifndef TCPSERVER_H
#define TCPSERVER_H
#include <string>
#include <list>
#include <map>
#include "uv.h"
#include "net/packet.h"
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
	AcceptClient(int clientid, char packhead, char packtail, uv_loop_t* loop);//server的loop
    virtual ~AcceptClient();

    void SetRecvCB(ServerRecvCB pfun, void* userdata);//设置接收数据回调函数
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//设置接收关闭事件的回调函数
	void SetProtocolProcess(TCPServerProtocolProcess *protocol){protocolprocess_ = protocol;}
	TCPServerProtocolProcess* GetProtocolProcess(void)const;

	int Send(const char* data, std::size_t len);
	void Close(){ isuseraskforclosed_ = true;}//用户关闭客户端，IsClosed返回true才是真正关闭了
	bool IsClosed(){return isclosed_;};//判断客户端是否已关闭
    bool AcceptByServer(uv_tcp_t* server);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
private:
	bool init(char packhead, char packtail);
	void closeinl();//内部真正的清理函数

	uv_loop_t* loop_;//server所带的loop
    int client_id_;//客户端id,惟一,由TCPServer负责赋值
    uv_tcp_t client_handle_;//客户端句柄
	uv_prepare_t prepare_handle_;//prepare阶段handle,用于检测关闭与是否需要发送数据
	bool isclosed_;//是否已关闭
	bool isuseraskforclosed_;//用户是否发送命令关闭

    //接收数据参数
    uv_buf_t readbuffer_;//接受数据的buf
    Packet readpacket_;//接受buf数据解析成包

	//发送数据参数
	uv_buf_t writebuffer_;//发送数据的buf
	uv_mutex_t mutex_writebuf_;//控制writebuf_list_
	std::list<uv_write_t*> writereq_list_;//可用的uv_write_t
	uv_mutex_t mutex_writereq_;//控制writereq_list_
	PodCircularBuffer<char> writebuf_list_;//发送数据队列

	TCPServerProtocolProcess *protocolprocess_;//类型处理类

    std::string errmsg_;//保存错误信息

    ServerRecvCB recvcb_;//接收数据回调给用户的函数
    void *recvcb_userdata_;

    TcpCloseCB closedcb_;//关闭后回调给TCPServer
    void *closedcb_userdata_;
private:
    static void AfterSend(uv_write_t *req, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
    static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//解析得一完整数据包回调给用户
	static void PrepareCB(uv_prepare_t* handle);//prepare阶段回调
};



/***************************************************************服务器*******************************************************************************/
/*************************************************
功能：TCP Server
调用方法：
设置回调函数SetNewConnectCB/SetRecvCB/SetClosedCB
调用StartLog启用日志功能(可选)
调用Start/Start6启动服务器
调用Send发送数据(可选)
调用Close停止服务器，真正停止时会触发在回调SetRecvCB中所设置的函数
调用IsClosed判断客户端是否真正关闭了
*************************************************/
class TCPServer
{
public:
    TCPServer(char packhead, char packtail);
    virtual ~TCPServer();
    static void StartLog(const char* logpath = nullptr);//启动日志，必须启动才会生成日志
public:
    //基本函数
    void SetNewConnectCB(NewConnectCB cb, void *userdata);
    void SetRecvCB(int clientid,ServerRecvCB cb, void *userdata);//设置接收回调函数，每个客户端各有一个
	void SetClosedCB(TcpCloseCB pfun, void* userdata);//设置接收关闭事件的回调函数
	void SetProtocol(int clientid, TCPServerProtocolProcess* protocol);
	TCPServerProtocolProcess* GetProtocolProcess(int clientid);

    bool Start(const char *ip, int port);//启动服务器,地址为IP4
    bool Start6(const char *ip, int port);//启动服务器，地址为IP6
	int  Send(int clientid, const char* data, std::size_t len);
	void Close(){ isuseraskforclosed_ = true;}//用户关闭客户端，IsClosed返回true才是真正关闭了
	bool IsClosed(){return isclosed_;};//判断客户端是否已关闭

	//是否启用Nagle算法
    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

protected:
    int GetAvailaClientID()const;//获取可用的client id
    //静态回调函数
    static void AfterServerClose(uv_handle_t *handle);
    static void AcceptConnection(uv_stream_t *server, int status);
    static void SubClientClosed(int clientid,void* userdata);//AcceptClient关闭后回调给TCPServer
	static void PrepareCB(uv_prepare_t* handle);//prepare阶段回调
	static void CheckCB(uv_check_t* handle);//check阶段回调
	static void IdleCB(uv_idle_t* handle);//idle阶段回调

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

    uv_loop_t loop_;
    uv_tcp_t server_handle_;//服务器链接
	uv_idle_t idle_handle_;//空闲阶段handle,暂不使用
	uv_prepare_t prepare_handle_;//prepare阶段handle,用于检测关闭与是否需要发送数据
	uv_check_t check_handle_;//check阶段handle,暂不使用
	bool isclosed_;//是否已初始化，用于close函数中判断
	bool isuseraskforclosed_;//用户是否发送命令关闭

    std::map<int,AcceptClient*> clients_list_;//子客户端链接
    uv_mutex_t mutex_clients_;//保护clients_list_

	uv_thread_t start_threadhandle_;//启动线程
	static void StartThread(void* arg);//真正的Start线程,一直监听直到用户主动退出
	int startstatus_;//连接状态

    std::string errmsg_;//错误信息

    NewConnectCB newconcb_;//回调函数
	void *newconcb_userdata_;

	TcpCloseCB closedcb_;//关闭后回调给TCPServer
	void *closedcb_userdata_;

	std::string serverip_;//连接的IP
	int serverport_;//连接的端口号

    char PACKET_HEAD;//包头
    char PACKET_TAIL;//包尾
};
}


#endif // TCPSERVER_H