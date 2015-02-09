#include "tcpclient.h"
#include "log4z.h"
#define MAXLISTSIZE 20

namespace uv
{
TcpClientCtx* AllocTcpClientCtx(void* parentserver)
{
    TcpClientCtx* ctx = (TcpClientCtx*)malloc(sizeof(*ctx));
    ctx->packet_ = new PacketSync;
    ctx->read_buf_.base = (char*)malloc(BUFFER_SIZE);
    ctx->read_buf_.len = BUFFER_SIZE;
    ctx->write_req.data = ctx;//store self
    ctx->parent_server = parentserver;//store TCPClient
    return ctx;
}

void FreeTcpClientCtx(TcpClientCtx* ctx)
{
    delete ctx->packet_;
    free(ctx->read_buf_.base);
    free(ctx);
}


write_param* AllocWriteParam(void)
{
    write_param* param = (write_param*)malloc(sizeof(*param));
    param->buf_.base = (char*)malloc(BUFFER_SIZE);
    param->buf_.len = BUFFER_SIZE;
    param->buf_truelen_ = BUFFER_SIZE;
    return param;
}

void FreeWriteParam(write_param* param)
{
    free(param->buf_.base);
    free(param);
}

/*****************************************TCP Client*************************************************************/
TCPClient::TCPClient(char packhead, char packtail)
    : PACKET_HEAD(packhead), PACKET_TAIL(packtail)
    , recvcb_(nullptr), recvcb_userdata_(nullptr), closedcb_(nullptr), closedcb_userdata_(nullptr)
    , connectstatus_(CONNECT_DIS), write_circularbuf_(BUFFER_SIZE)
    , isclosed_(true), isuseraskforclosed_(false)
    , reconnectcb_(nullptr), reconnect_userdata_(nullptr)
    , isIPv6_(false), isreconnecting_(false)
{
    client_handle_ = AllocTcpClientCtx(this);
    int iret = uv_loop_init(&loop_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout, "init loop error: %s\n", errmsg_.c_str());
    }
    iret = uv_mutex_init(&mutex_writebuf_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
    }
    connect_req_.data = this;
}


TCPClient::~TCPClient()
{
    Close();
    uv_thread_join(&connect_threadhandle_);//libuv事件循环已退出
    FreeTcpClientCtx(client_handle_);
    uv_loop_close(&loop_);
    uv_mutex_destroy(&mutex_writebuf_);
    for (auto it = writeparam_list_.begin(); it != writeparam_list_.end(); ++it) {
        FreeWriteParam(*it);
    }
    writeparam_list_.clear();

    LOGI("客户端(" << this << ")退出");
}

//初始化与关闭
bool TCPClient::init()
{
    if (!isclosed_) {
        return true;
    }
    int iret = uv_async_init(&loop_, &async_handle_, AsyncCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    async_handle_.data = this;

    iret = uv_tcp_init(&loop_, &client_handle_->tcphandle);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    client_handle_->tcphandle.data = client_handle_;
    client_handle_->parent_server = this;

    client_handle_->packet_->SetPacketCB(GetPacket, client_handle_);
    client_handle_->packet_->Start(PACKET_HEAD, PACKET_TAIL);

    iret = uv_timer_init(&loop_, &reconnect_timer_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    reconnect_timer_.data = this;
    LOGI("客户端(" << this << ")Init");
    isclosed_ = false;
    return true;
}

void TCPClient::closeinl()
{
    if (isclosed_) {
        return;
    }
	StopReconnect();
    uv_walk(&loop_, CloseWalkCB, this);//替换每个handle的close
    LOGI("客户端(" << this << ")close");
}

bool TCPClient::run(int status)
{
    int iret = uv_run(&loop_, (uv_run_mode)status);
    isclosed_ = true;
    LOGI("client had closed.");
    if (closedcb_) {//通知用户客户端已经关闭
        closedcb_(-1, closedcb_userdata_); //客户端：clientid无效，永远为-1
    }
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//属性设置--服务器与客户端一致
bool TCPClient::setNoDelay(bool enable)
{
    //http://blog.csdn.net/u011133100/article/details/21485983
    int iret = uv_tcp_nodelay(&client_handle_->tcphandle, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

bool TCPClient::setKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&client_handle_->tcphandle, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//作为client的connect函数
bool TCPClient::Connect(const char* ip, int port)
{
    closeinl();
    if (!init()) {
        return false;
    }
    connectip_ = ip;
    connectport_ = port;
    isIPv6_ = false;
    struct sockaddr_in bind_addr;
    int iret = uv_ip4_addr(connectip_.c_str(), connectport_, &bind_addr);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_tcp_connect(&connect_req_, &client_handle_->tcphandle, (const sockaddr*)&bind_addr, AfterConnect);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }

    LOGI("客户端(" << this << ")start connect to server(" << ip << ":" << port << ")");
    iret = uv_thread_create(&connect_threadhandle_, ConnectThread, this);//触发AfterConnect才算真正连接成功，所以用线程
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while (connectstatus_ == CONNECT_DIS) {
        ThreadSleep(100);
        if (++wait_count > 100) {
            connectstatus_ = CONNECT_TIMEOUT;
            break;
        }
    }
    if (CONNECT_FINISH != connectstatus_) {
        errmsg_ = "connect time out";
        return false;
    } else {
        return true;
    }
}

bool TCPClient::Connect6(const char* ip, int port)
{
    closeinl();
    if (!init()) {
        return false;
    }
    connectip_ = ip;
    connectport_ = port;
    isIPv6_ = true;
    struct sockaddr_in6 bind_addr;
    int iret = uv_ip6_addr(connectip_.c_str(), connectport_, &bind_addr);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_tcp_connect(&connect_req_, &client_handle_->tcphandle, (const sockaddr*)&bind_addr, AfterConnect);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }

    LOGI("客户端(" << this << ")start connect to server(" << ip << ":" << port << ")");
    iret = uv_thread_create(&connect_threadhandle_, ConnectThread, this);//触发AfterConnect才算真正连接成功，所以用线程
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while (connectstatus_ == CONNECT_DIS) {
        ThreadSleep(100);
        if (++wait_count > 100) {
            connectstatus_ = CONNECT_TIMEOUT;
            break;
        }
    }
    if (CONNECT_FINISH != connectstatus_) {
        errmsg_ = "connect time out";
        return false;
    } else {
        return true;
    }
}

void TCPClient::ConnectThread(void* arg)
{
    TCPClient* pclient = (TCPClient*)arg;
    pclient->run();
}

void TCPClient::AfterConnect(uv_connect_t* handle, int status)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->handle->data;
    TCPClient* parent = (TCPClient*)theclass->parent_server;
    if (status) {
        parent->connectstatus_ = CONNECT_ERROR;
        parent->errmsg_ = GetUVError(status);
        LOGE("客户端(" << parent << ") connect error:" << parent->errmsg_);
        fprintf(stdout, "connect error:%s\n", parent->errmsg_.c_str());
        if (parent->isreconnecting_) {//重连失败，重启定时器重连
            uv_timer_stop(&parent->reconnect_timer_);
            parent->repeat_time_ *= 2;
            uv_timer_start(&parent->reconnect_timer_, TCPClient::ReconnectTimer, parent->repeat_time_, parent->repeat_time_);
        }
        return;
    }

    int iret = uv_read_start(handle->handle, AllocBufferForRecv, AfterRecv);//客户端开始接收服务器的数据
    if (iret) {
        parent->errmsg_ = GetUVError(status);
        LOGE("客户端(" << parent << ") uv_read_start error:" << parent->errmsg_);
        fprintf(stdout, "uv_read_start error:%s\n", parent->errmsg_.c_str());
        parent->connectstatus_ = CONNECT_ERROR;
    } else {
        parent->connectstatus_ = CONNECT_FINISH;
        LOGI("客户端(" << parent << ")run");
    }
    if (parent->isreconnecting_) {
        fprintf(stdout, "reconnect succeed\n");
        parent->StopReconnect();//重连成功
        if (parent->reconnectcb_) {
            parent->reconnectcb_(NET_EVENT_TYPE_RECONNECT, parent->reconnect_userdata_);
        }
    }
}

//客户端的发送函数
int TCPClient::Send(const char* data, std::size_t len)
{
    if (!data || len <= 0) {
        errmsg_ = "send data is null or len less than zero.";
        LOGE(errmsg_);
        return 0;
    }
    uv_async_send(&async_handle_);//触发真正发送函数
    size_t iret = 0;
    while (!isuseraskforclosed_) {
        uv_mutex_lock(&mutex_writebuf_);
        iret += write_circularbuf_.write(data + iret, len - iret);
        uv_mutex_unlock(&mutex_writebuf_);
        if (iret < len) {
            ThreadSleep(100);
            continue;
        } else {
            break;
        }
    }
    uv_async_send(&async_handle_);//触发真正发送函数
    return iret;
}

//客户端-接收数据回调函数
void TCPClient::SetRecvCB(ClientRecvCB pfun, void* userdata)
{
    recvcb_ = pfun;
    recvcb_userdata_ = userdata;
}

void TCPClient::SetClosedCB(TcpCloseCB pfun, void* userdata)
{
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

void TCPClient::SetReconnectCB(ReconnectCB pfun, void* userdata)
{
    reconnectcb_ = pfun;
    reconnect_userdata_ = userdata;
}

//客户端分析空间函数
void TCPClient::AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    assert(theclass);
    *buf = theclass->read_buf_;
}


void TCPClient::AfterRecv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    assert(theclass);
    TCPClient* parent = (TCPClient*)theclass->parent_server;
    if (nread < 0) {
        if (parent->reconnectcb_) {
            parent->reconnectcb_(NET_EVENT_TYPE_DISCONNECT, parent->reconnect_userdata_);
        }
        if (!parent->StartReconnect()) {
            fprintf(stdout, "启动断线重连失败.\n");
            return;
        }
        if (nread == UV_EOF) {
            fprintf(stdout, "服务器主动断开,Client为%p\n", handle);
            LOGW("服务器主动断开");
        } else if (nread == UV_ECONNRESET) {
            fprintf(stdout, "服务器异常断开,Client为%p\n", handle);
            LOGW("服务器异常断开");
        } else {
            fprintf(stdout, "服务器异常断开，,Client为%p:%s\n", handle, GetUVError(nread));
            LOGW("服务器异常断开" << GetUVError(nread));
        }
        uv_close((uv_handle_t*)handle, AfterClientClose);//close before reconnect
        //parent->Close();
        return;
    }
    parent->send_inl(NULL);
    if (nread > 0) {
        theclass->packet_->recvdata((const unsigned char*)buf->base, nread); //新方式-解析完包后再回调数据
    }
}

//服务器与客户端一致
void TCPClient::AfterSend(uv_write_t* req, int status)
{
    TCPClient* theclass = (TCPClient*)req->data;
    if (status < 0) {
        if (theclass->writeparam_list_.size() > MAXLISTSIZE) {
            FreeWriteParam((write_param*)req);
        } else {
            theclass->writeparam_list_.push_back((write_param*)req);
        }
        LOGE("发送数据有误:" << GetUVError(status));
        fprintf(stderr, "send error %s\n", GetUVError(status));
        return;
    }
    theclass->send_inl(req);
}

/* Fully close a loop */
void TCPClient::CloseWalkCB(uv_handle_t* handle, void* arg)
{
    TCPClient* theclass = (TCPClient*)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, AfterClientClose);
    }
}

void TCPClient::AfterClientClose(uv_handle_t* handle)
{
    TCPClient* theclass = (TCPClient*)handle->data;
    fprintf(stdout, "Close CB handle %p\n", handle);
    if (handle == (uv_handle_t*)&theclass->client_handle_->tcphandle && theclass->isreconnecting_) {
        //closed, start reconnect timer
        int iret = 0;
        iret = uv_timer_start(&theclass->reconnect_timer_, TCPClient::ReconnectTimer, theclass->repeat_time_, theclass->repeat_time_);
        if (iret) {
            uv_close((uv_handle_t*)&theclass->reconnect_timer_, TCPClient::AfterClientClose);
            LOGE(GetUVError(iret));
            return;
        }
    }
    //if (handle == (uv_handle_t*)&theclass->client_handle_->tcphandle) {
    //    handle->data = 0;//赋值0，用于判断是否调用过
    //}
    //if (handle == (uv_handle_t*)&theclass->async_handle_) {
    //    handle->data = 0;//赋值0，用于判断是否调用过
    //}
    //if (theclass->async_handle_.data == 0
    //    && theclass->client_handle_->tcphandle.data == 0) {
    //    theclass->isclosed_ = true;
    //    LOGI("client  had closed.");
    //    if (theclass->closedcb_) {//通知TCPServer此客户端已经关闭
    //        theclass->closedcb_(-1, theclass->closedcb_userdata_);
    //    }
    //}
}

void TCPClient::StartLog(const char* logpath /*= nullptr*/)
{
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerMonthdir(LOG4Z_MAIN_LOGGER_ID, true);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerDisplay(LOG4Z_MAIN_LOGGER_ID, false);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLevel(LOG4Z_MAIN_LOGGER_ID, LOG_LEVEL_DEBUG);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLimitSize(LOG4Z_MAIN_LOGGER_ID, 100);
    if (logpath) {
        zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerPath(LOG4Z_MAIN_LOGGER_ID, logpath);
    }
    zsummer::log4z::ILog4zManager::GetInstance()->Start();
}

void TCPClient::StopLog()
{
    zsummer::log4z::ILog4zManager::GetInstance()->Stop();
}

void TCPClient::GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata)
{
    assert(userdata);
    TcpClientCtx* theclass = (TcpClientCtx*)userdata;//alone_data_tcphandle_里的值
    TCPClient* parent = (TCPClient*)theclass->parent_server;
    if (parent->recvcb_) {//把得到的数据回调给用户
        parent->recvcb_(packethead, packetdata, parent->recvcb_userdata_);
    }
}

void TCPClient::AsyncCB(uv_async_t* handle)
{
    //处理用户关闭命令
    TCPClient* theclass = (TCPClient*)handle->data;
    if (theclass->isuseraskforclosed_) {
        theclass->closeinl();
        return;
    }
    //检测是否有数据要发送
    theclass->send_inl(NULL);
}

void TCPClient::send_inl(uv_write_t* req /*= NULL*/)
{
    write_param* writep = (write_param*)req;
    write_param* once_writep = writep;
    while (true) {//发送到完
        uv_mutex_lock(&mutex_writebuf_);
        if (write_circularbuf_.empty()) {
            uv_mutex_unlock(&mutex_writebuf_);
            break;
        }
        if (NULL == once_writep) {
            if (writeparam_list_.empty()) {
                writep = AllocWriteParam();
                writep->write_req_.data = this;
            } else {
                writep = writeparam_list_.front();
                writeparam_list_.pop_front();
            }
        } else {
            once_writep = NULL;//保证只使用一次
        }
        writep->buf_.len = write_circularbuf_.read(writep->buf_.base, writep->buf_truelen_); //得到要发送的数据
        uv_mutex_unlock(&mutex_writebuf_);
        int iret = uv_write((uv_write_t*)&writep->write_req_, (uv_stream_t*)&client_handle_->tcphandle, &writep->buf_, 1, AfterSend);//发送
        if (iret) {
            writeparam_list_.push_back(writep);//发送失败，不会调用AfterSend函数，所以得回收req
            LOGE("客户端(" << this << ") send error:" << GetUVError(iret));
            fprintf(stdout, "send error. %s-%s\n", uv_err_name(iret), uv_strerror(iret));
        }
    }
}

void TCPClient::Close()
{
    if (isclosed_) {
        return;
    }
    isuseraskforclosed_ = true;   //用户关闭客户端，IsClosed返回true才是真正关闭了
    uv_async_send(&async_handle_);//触发真正关闭函数
}

bool TCPClient::StartReconnect(void)
{
    isreconnecting_ = true;
    client_handle_->tcphandle.data = this;
    repeat_time_ = 1e3;//1秒
    return true;
}

void TCPClient::StopReconnect(void)
{
    isreconnecting_ = false;
    client_handle_->tcphandle.data = client_handle_;
    repeat_time_ = 1e3;//1秒
	uv_timer_stop(&reconnect_timer_);
}
void TCPClient::ReconnectTimer(uv_timer_t* handle)
{
    TCPClient* theclass = (TCPClient*)handle->data;
    if (!theclass->isreconnecting_) {
        return;
    }
	LOGI("start reconnect...\n");
    do {
        int iret = uv_tcp_init(&theclass->loop_, &theclass->client_handle_->tcphandle);
        if (iret) {
            LOGE(GetUVError(iret));
            break;
        }
        theclass->client_handle_->tcphandle.data = theclass->client_handle_;
        theclass->client_handle_->parent_server = theclass;
        struct sockaddr* pAddr;
        if (theclass->isIPv6_) {
            struct sockaddr_in6 bind_addr;
            int iret = uv_ip6_addr(theclass->connectip_.c_str(), theclass->connectport_, &bind_addr);
            if (iret) {
                LOGE(GetUVError(iret));
                uv_close((uv_handle_t*)&theclass->client_handle_->tcphandle, NULL);
                break;
            }
            pAddr = (struct sockaddr*)&bind_addr;
        } else {
            struct sockaddr_in bind_addr;
            int iret = uv_ip4_addr(theclass->connectip_.c_str(), theclass->connectport_, &bind_addr);
            if (iret) {
                LOGE(GetUVError(iret));
                uv_close((uv_handle_t*)&theclass->client_handle_->tcphandle, NULL);
                break;
            }
            pAddr = (struct sockaddr*)&bind_addr;
        }
        iret = uv_tcp_connect(&theclass->connect_req_, &theclass->client_handle_->tcphandle, (const sockaddr*)pAddr, AfterConnect);
        if (iret) {
            LOGE(GetUVError(iret));
            uv_close((uv_handle_t*)&theclass->client_handle_->tcphandle, NULL);
            break;
        }
        return;
    } while (0);
    //重连失败，重启定时器重连
    uv_timer_stop(handle);
    theclass->repeat_time_ *= 2;
    uv_timer_start(handle, TCPClient::ReconnectTimer, theclass->repeat_time_, theclass->repeat_time_);
}
}