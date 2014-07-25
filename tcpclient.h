/***************************************
* @file     tcpclient.h
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
		    2014-07-24  phata  从tcpsocket中分离出TCPClient。
			                   单独线程实现libuv的run(事件循环)，任何libuv相关操作都在此线程中完成。因此TCPClient可以多线程中任意调用
							   一个client需要4倍BUFFER_SIZE(readbuffer_,writebuffer_,writebuf_list_,readpacket_),启动两个线程(readpacket_内部一个，Connect启动一个)
****************************************/
#ifndef TCPCLIENT_H
#define TCPCLIENT_H
#include <string>
#include <list>
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
/**********************************************客户端****************************************************/
/*************************************************
功能：TCP 客户端对象
调用方法：
设置回调函数SetRecvCB/SetClosedCB
调用Connect/Connect6函数启动客户端
调用Send发送数据(可选)
调用Close停止客户端，真正停止时会触发在回调SetRecvCB中所设置的函数
调用IsClosed判断客户端是否真正关闭了
*************************************************/
class TCPClient
{
public:
    TCPClient(char packhead, char packtail);
    virtual ~TCPClient();
    static void StartLog(const char* logpath = nullptr);//启动日志，必须启动才会生成日志
public:
    //基本函数
    void SetRecvCB(ClientRecvCB pfun, void* userdata);////设置接收回调函数，只有一个
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//设置接收关闭事件的回调函数
    bool Connect(const char* ip, int port);//启动connect线程，循环等待直到connect完成
    bool Connect6(const char* ip, int port);//启动connect线程，循环等待直到connect完成
    int  Send(const char* data, std::size_t len);
	void Close(){ isuseraskforclosed_ = true;}//用户关闭客户端，IsClosed返回true才是真正关闭了
	bool IsClosed(){return isclosed_;};//判断客户端是否已关闭
    //是否启用Nagle算法
    bool setNoDelay(bool enable);
    bool setKeepAlive(int enable, unsigned int delay);

    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
protected:
    bool init();//初始化参数
	void closeinl();//内部真正的清理函数
    bool run(int status = UV_RUN_DEFAULT);//启动事件循环
    static void ConnectThread(void* arg);//真正的connect线程

    static void AfterConnect(uv_connect_t* handle, int status);
    static void AfterRecv(uv_stream_t *client, ssize_t nread, const uv_buf_t* buf);
    static void AfterSend(uv_write_t *req, int status);
    static void AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void AfterClientClose(uv_handle_t *handle);
    static void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);//解析完一帧后的回调函数
	static void PrepareCB(uv_prepare_t* handle);//prepare阶段回调
	static void CheckCB(uv_check_t* handle);//check阶段回调
	static void IdleCB(uv_idle_t* handle);//idle阶段回调

private:
    enum {
        CONNECT_TIMEOUT,
        CONNECT_FINISH,
        CONNECT_ERROR,
        CONNECT_DIS,
    };
    uv_tcp_t client_handle_;//客户端连接
	uv_idle_t idle_handle_;//空闲阶段handle,暂不使用
	uv_prepare_t prepare_handle_;//prepare阶段handle,用于检测关闭与是否需要发送数据
	uv_check_t check_handle_;//check阶段handle,暂不使用
    uv_loop_t loop_;
    bool isclosed_;//是否已关闭
	bool isuseraskforclosed_;//用户是否发送命令关闭

	uv_thread_t connect_threadhandle_;//连接线程
    uv_connect_t connect_req_;//连接时请求

    int connectstatus_;//连接状态

    //接收数据参数
    uv_buf_t readbuffer_;//接送数据的buf
    Packet readpacket_;//接受buf数据解析成包

    //发送数据参数
	uv_buf_t writebuffer_;//发送数据的buf
    uv_mutex_t mutex_writebuf_;//控制writebuf_list_
    std::list<uv_write_t*> writereq_list_;//可用的uv_write_t
    uv_mutex_t mutex_writereq_;//控制writereq_list_
    PodCircularBuffer<char> writebuf_list_;//发送数据队列

    ClientRecvCB recvcb_;//回调函数
    void* recvcb_userdata_;//回调函数的用户数据

    TcpCloseCB closedcb_;//关闭后回调给TCPServer
    void *closedcb_userdata_;

    std::string connectip_;//连接的服务器IP
    int connectport_;//连接的服务器端口号

    std::string errmsg_;//错误信息

    char PACKET_HEAD;//包头
    char PACKET_TAIL;//包尾
};
}


#endif // TCPCLIENT_H