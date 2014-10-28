#include "tcpserver.h"
#include <assert.h>
#include "log4z.h"
#define MAXLISTSIZE 20


namespace uv
{
/*****************************************TCP Server*************************************************************/
TCPServer::TCPServer(char packhead, char packtail)
    : packet_head(packhead), packet_tail(packtail)
    , newconcb_(nullptr), newconcb_userdata_(nullptr), closedcb_(nullptr), closedcb_userdata_(nullptr)
    , isclosed_(true), isuseraskforclosed_(false)
    , startstatus_(START_DIS), protocol_(NULL)
{
    int iret = uv_loop_init(&loop_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout, "init loop error: %s\n", errmsg_.c_str());
    }
    iret = uv_mutex_init(&mutex_clients_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout, "uv_mutex_init error: %s\n", errmsg_.c_str());
    }
}


TCPServer::~TCPServer()
{
    Close();
    uv_thread_join(&start_threadhandle_);
    uv_mutex_destroy(&mutex_clients_);
    uv_loop_close(&loop_);
    for (auto it = avai_tcphandle_.begin(); it != avai_tcphandle_.end(); ++it) {
        FreeTcpClientCtx(*it);
    }
    avai_tcphandle_.clear();

    for (auto it = writeparam_list_.begin(); it != writeparam_list_.end(); ++it) {
        FreeWriteParam(*it);
    }
    writeparam_list_.clear();
    LOGI("tcp server exit.");
}

bool TCPServer::init()
{
    if (!isclosed_) {
        return true;
    }
    int iret = uv_async_init(&loop_, &async_handle_close_, AsyncCloseCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    async_handle_close_.data = this;

    iret = uv_tcp_init(&loop_, &tcp_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    tcp_handle_.data = this;
    iret = uv_tcp_nodelay(&tcp_handle_,  1);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    isclosed_ = false;
    return true;
}

void TCPServer::closeinl()
{
    if (isclosed_) {
        return;
    }

    uv_mutex_lock(&mutex_clients_);
    for (auto it = clients_list_.begin(); it != clients_list_.end(); ++it) {
        auto data = it->second;
        data->Close();
    }
    uv_walk(&loop_, CloseWalkCB, this);//close all handle in loop
    LOGI("close server");
}

bool TCPServer::run(int status)
{
    LOGI("server runing.");
    int iret = uv_run(&loop_, (uv_run_mode)status);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

bool TCPServer::SetNoDelay(bool enable)
{
    int iret = uv_tcp_nodelay(&tcp_handle_, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

bool TCPServer::SetKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&tcp_handle_, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

bool TCPServer::bind(const char* ip, int port)
{
    struct sockaddr_in bind_addr;
    int iret = uv_ip4_addr(ip, port, &bind_addr);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_tcp_bind(&tcp_handle_, (const struct sockaddr*)&bind_addr, 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    LOGI("server bind ip=" << ip << ", port=" << port);
    return true;
}

bool TCPServer::bind6(const char* ip, int port)
{
    struct sockaddr_in6 bind_addr;
    int iret = uv_ip6_addr(ip, port, &bind_addr);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_tcp_bind(&tcp_handle_, (const struct sockaddr*)&bind_addr, 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }

    LOGI("server bind ip=" << ip << ", port=" << port);
    return true;
}

bool TCPServer::listen(int backlog)
{
    int iret = uv_listen((uv_stream_t*) &tcp_handle_, backlog, AcceptConnection);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    LOGI("server Start listen. Runing.......");
    fprintf(stdout, "Server Runing.......\n");
    return true;
}

bool TCPServer::Start(const char* ip, int port)
{
    serverip_ = ip;
    serverport_ = port;
    closeinl();
    if (!init()) {
        return false;
    }
    if (!bind(serverip_.c_str(), serverport_)) {
        return false;
    }
    if (!listen(SOMAXCONN)) {
        return false;
    }
    int iret = uv_thread_create(&start_threadhandle_, StartThread, this);//use thread to wait for start succeed.
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while (startstatus_ == START_DIS) {
        ThreadSleep(100);
        if (++wait_count > 100) {
            startstatus_ = START_TIMEOUT;
            break;
        }
    }
    return startstatus_ == START_FINISH;
}

bool TCPServer::Start6(const char* ip, int port)
{
    serverip_ = ip;
    serverport_ = port;
    closeinl();
    if (!init()) {
        return false;
    }
    if (!bind6(serverip_.c_str(), serverport_)) {
        return false;
    }
    if (!listen(SOMAXCONN)) {
        return false;
    }
    int iret = uv_thread_create(&start_threadhandle_, StartThread, this);//use thread to wait for start succeed.
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while (startstatus_ == START_DIS) {
        ThreadSleep(100);
        if (++wait_count > 100) {
            startstatus_ = START_TIMEOUT;
            break;
        }
    }
    return startstatus_ == START_FINISH;
}

void TCPServer::StartThread(void* arg)
{
    TCPServer* theclass = (TCPServer*)arg;
    theclass->startstatus_ = START_FINISH;
    theclass->run();
    //the server is close when come here
    theclass->isclosed_ = true;
    theclass->isuseraskforclosed_ = false;
    LOGI("server  had closed.");
    if (theclass->closedcb_) {//trigger the close cb
        theclass->closedcb_(-1, theclass->closedcb_userdata_);
    }
}

void TCPServer::AcceptConnection(uv_stream_t* server, int status)
{
    TCPServer* tcpsock = (TCPServer*)server->data;
    assert(tcpsock);
    if (status) {
        tcpsock->errmsg_ = GetUVError(status);
        LOGE(tcpsock->errmsg_);
        return;
    }
    TcpClientCtx* tmptcp = NULL;
    if (tcpsock->avai_tcphandle_.empty()) {
        tmptcp = AllocTcpClientCtx(tcpsock);
    } else {
        tmptcp = tcpsock->avai_tcphandle_.front();
        tcpsock->avai_tcphandle_.pop_front();
        tmptcp->parent_acceptclient = NULL;
    }
    int iret = uv_tcp_init(&tcpsock->loop_, &tmptcp->tcphandle);
    if (iret) {
        tcpsock->avai_tcphandle_.push_back(tmptcp);//Recycle
        tcpsock->errmsg_ = GetUVError(iret);
        LOGE(tcpsock->errmsg_);
        return;
    }
    tmptcp->tcphandle.data = tmptcp;

    auto clientid = tcpsock->GetAvailaClientID();
    tmptcp->clientid = clientid;
    iret = uv_accept((uv_stream_t*)server, (uv_stream_t*)&tmptcp->tcphandle);
    if (iret) {
        tcpsock->avai_tcphandle_.push_back(tmptcp);//Recycle
        tcpsock->errmsg_ = GetUVError(iret);
        LOGE(tcpsock->errmsg_);
        return;
    }
    tmptcp->packet_->SetPacketCB(GetPacket, tmptcp);
    tmptcp->packet_->Start(tcpsock->packet_head, tcpsock->packet_tail);
    iret = uv_read_start((uv_stream_t*)&tmptcp->tcphandle, AllocBufferForRecv, AfterRecv);
    if (iret) {
        uv_close((uv_handle_t*)&tmptcp->tcphandle, TCPServer::RecycleTcpHandle);
        tcpsock->errmsg_ = GetUVError(iret);
        LOGE(tcpsock->errmsg_);
        return;
    }
    AcceptClient* cdata = new AcceptClient(tmptcp, clientid, tcpsock->packet_head, tcpsock->packet_tail, &tcpsock->loop_); //delete on SubClientClosed
    cdata->SetClosedCB(TCPServer::SubClientClosed, tcpsock);
    uv_mutex_lock(&tcpsock->mutex_clients_);
    tcpsock->clients_list_.insert(std::make_pair(clientid, cdata)); //add accept client
    uv_mutex_unlock(&tcpsock->mutex_clients_);

    if (tcpsock->newconcb_) {
        tcpsock->newconcb_(clientid, tcpsock->newconcb_userdata_);
    }
    LOGI("new client id=" << clientid);
    return;
}

void TCPServer::SetRecvCB(int clientid, ServerRecvCB cb, void* userdata)
{
    uv_mutex_lock(&mutex_clients_);
    auto itfind = clients_list_.find(clientid);
    if (itfind != clients_list_.end()) {
        itfind->second->SetRecvCB(cb, userdata);
    }
    uv_mutex_unlock(&mutex_clients_);
}

void TCPServer::SetNewConnectCB(NewConnectCB cb, void* userdata)
{
    newconcb_ = cb;
    newconcb_userdata_ = userdata;
}

void TCPServer::SetClosedCB(TcpCloseCB pfun, void* userdata)
{
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

/* Fully close a loop */
void TCPServer::CloseWalkCB(uv_handle_t* handle, void* arg)
{
	TCPServer* theclass = (TCPServer*)arg;
	if (!uv_is_closing(handle)) {
		uv_close(handle, AfterServerClose);
	}
}

void TCPServer::AfterServerClose(uv_handle_t* handle)
{
    TCPServer* theclass = (TCPServer*)handle->data;
    fprintf(stdout, "Close CB handle %p\n", handle);
}

void TCPServer::DeleteTcpHandle(uv_handle_t* handle)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    FreeTcpClientCtx(theclass);
}

void TCPServer::RecycleTcpHandle(uv_handle_t* handle)
{
    //the handle on TcpClientCtx had closed.
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    assert(theclass);
    TCPServer* parent = (TCPServer*)theclass->parent_server;
    if (parent->avai_tcphandle_.size() > MAXLISTSIZE) {
        FreeTcpClientCtx(theclass);
    } else {
        parent->avai_tcphandle_.push_back(theclass);
    }
}

int TCPServer::GetAvailaClientID() const
{
    static int s_id = 0;
    return ++s_id;
}

void TCPServer::StartLog(const char* logpath /*= nullptr*/)
{
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerMonthdir(LOG4Z_MAIN_LOGGER_ID, true);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerDisplay(LOG4Z_MAIN_LOGGER_ID, true);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLevel(LOG4Z_MAIN_LOGGER_ID, LOG_LEVEL_DEBUG);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLimitSize(LOG4Z_MAIN_LOGGER_ID, 100);
    if (logpath) {
        zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerPath(LOG4Z_MAIN_LOGGER_ID, logpath);
    }
    zsummer::log4z::ILog4zManager::GetInstance()->Start();
}

void TCPServer::StopLog()
{
	zsummer::log4z::ILog4zManager::GetInstance()->Stop();
}

void TCPServer::SubClientClosed(int clientid, void* userdata)
{
    TCPServer* theclass = (TCPServer*)userdata;
    uv_mutex_lock(&theclass->mutex_clients_);
    auto itfind = theclass->clients_list_.find(clientid);
    if (itfind != theclass->clients_list_.end()) {
        if (theclass->closedcb_) {
            theclass->closedcb_(clientid, theclass->closedcb_userdata_);
        }
        if (theclass->avai_tcphandle_.size() > MAXLISTSIZE) {
            FreeTcpClientCtx(itfind->second->GetTcpHandle());
        } else {
            theclass->avai_tcphandle_.push_back(itfind->second->GetTcpHandle());
        }
        delete itfind->second;
        LOGI("delete client:" << itfind->first);
        fprintf(stdout, "delete client：%d\n", itfind->first);
        theclass->clients_list_.erase(itfind);
    }
    uv_mutex_unlock(&theclass->mutex_clients_);
}

void TCPServer::AsyncCloseCB(uv_async_t* handle)
{
    TCPServer* theclass = (TCPServer*)handle->data;
    if (theclass->isuseraskforclosed_) {
        theclass->closeinl();
    }
        return;
}

void TCPServer::Close()
{
    if (isclosed_) {
        return;
    }
    isuseraskforclosed_ = true;
    uv_async_send(&async_handle_close_);
}

bool TCPServer::broadcast(const std::string& senddata, std::vector<int> excludeid)
{
    if (senddata.empty()) {
        LOGA("broadcast data is empty.");
        return true;
    }
    uv_mutex_lock(&mutex_clients_);
    AcceptClient* pClient = NULL;
    write_param* writep = NULL;
    if (excludeid.empty()) {
        for (auto it = clients_list_.begin(); it != clients_list_.end(); ++it) {
            pClient = it->second;
            sendinl(senddata, pClient->GetTcpHandle());
        }
    } else {
        for (auto it = clients_list_.begin(); it != clients_list_.end(); ++it) {
            auto itfind = std::find(excludeid.begin(), excludeid.end(), it->first);
            if (itfind != excludeid.end()) {
                excludeid.erase(itfind);
                continue;
            }
            pClient = it->second;
            sendinl(senddata, pClient->GetTcpHandle());
        }
    }
    uv_mutex_unlock(&mutex_clients_);
    return true;
}

bool TCPServer::sendinl(const std::string& senddata, TcpClientCtx* client)
{
    if (senddata.empty()) {
        LOGA("send data is empty.");
        return true;
    }
    write_param* writep = NULL;
    if (writeparam_list_.empty()) {
        writep = AllocWriteParam();
    } else {
        writep = writeparam_list_.front();
        writeparam_list_.pop_front();
    }
    if (writep->buf_truelen_ < senddata.length()) {
        writep->buf_.base = (char*)realloc(writep->buf_.base, senddata.length());
        writep->buf_truelen_ = senddata.length();
    }
    memcpy(writep->buf_.base, senddata.data(), senddata.length());
    writep->buf_.len = senddata.length();
    writep->write_req_.data = client;
    int iret = uv_write((uv_write_t*)&writep->write_req_, (uv_stream_t*)&client->tcphandle, &writep->buf_, 1, AfterSend);//发送
    if (iret) {
        writeparam_list_.push_back(writep);
        errmsg_ = "send data error.";
        LOGE("client(" << client << ") send error:" << GetUVError(iret));
        fprintf(stdout, "send error. %s-%s\n", uv_err_name(iret), uv_strerror(iret));
        return false;
    }
    return true;
}

void TCPServer::SetPortocol(TCPServerProtocolProcess* pro)
{
    protocol_ = pro;
}

/*****************************************AcceptClient*************************************************************/
AcceptClient::AcceptClient(TcpClientCtx* control,  int clientid, char packhead, char packtail, uv_loop_t* loop)
    : client_handle_(control)
    , client_id_(clientid), loop_(loop)
    , isclosed_(true)
    , recvcb_(nullptr), recvcb_userdata_(nullptr), closedcb_(nullptr), closedcb_userdata_(nullptr)
{
    init(packhead, packtail);
}

AcceptClient::~AcceptClient()
{
    Close();
    //while will block loop.
	//the right way is new AcceptClient and delete it on SetClosedCB'cb
    while (!isclosed_) {
        ThreadSleep(10);
    }
}

bool AcceptClient::init(char packhead, char packtail)
{
    if (!isclosed_) {
        return true;
    }
    client_handle_->parent_acceptclient = this;
    isclosed_ = false;
    return true;
}

void AcceptClient::Close()
{
    if (isclosed_) {
        return;
    }
    client_handle_->tcphandle.data = this;
    //send close command
    uv_close((uv_handle_t*)&client_handle_->tcphandle, AfterClientClose);
    LOGI("client(" << this << ")close");
}

void AcceptClient::AfterClientClose(uv_handle_t* handle)
{
    AcceptClient* theclass = (AcceptClient*)handle->data;
    assert(theclass);
    if (handle == (uv_handle_t*)&theclass->client_handle_->tcphandle) {
        theclass->isclosed_ = true;
        LOGI("client  had closed.");
        if (theclass->closedcb_) {//notice tcpserver the client had closed
            theclass->closedcb_(theclass->client_id_, theclass->closedcb_userdata_);
        }
    }
}

void AcceptClient::SetRecvCB(ServerRecvCB pfun, void* userdata)
{
    //GetPacket trigger this cb
    recvcb_ = pfun;
    recvcb_userdata_ = userdata;
}

void AcceptClient::SetClosedCB(TcpCloseCB pfun, void* userdata)
{
    //AfterRecv trigger this cb
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

TcpClientCtx* AcceptClient::GetTcpHandle(void) const
{
    return client_handle_;
}

/*****************************************Global*************************************************************/
void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    assert(theclass);
    *buf = theclass->read_buf_;
}

void AfterRecv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    assert(theclass);
    if (nread < 0) {/* Error or EOF */
        if (nread == UV_EOF) {
            fprintf(stdout, "client(%d)eof\n", theclass->clientid);
            LOGW("client(" << theclass->clientid << ")eof");
        } else if (nread == UV_ECONNRESET) {
            fprintf(stdout, "client(%d)conn reset\n", theclass->clientid);
            LOGW("client(" << theclass->clientid << ")conn reset");
        } else {
            fprintf(stdout, "%s\n", GetUVError(nread));
            LOGW("client(" << theclass->clientid << ")：" << GetUVError(nread));
        }
        AcceptClient* acceptclient = (AcceptClient*)theclass->parent_acceptclient;
        acceptclient->Close();
        return;
    } else if (0 == nread)  {/* Everything OK, but nothing read. */

    } else {
        theclass->packet_->recvdata((const unsigned char*)buf->base, nread);
    }
}

void AfterSend(uv_write_t* req, int status)
{
    TcpClientCtx* theclass = (TcpClientCtx*)req->data;
    TCPServer* parent = (TCPServer*)theclass->parent_server;
    if (parent->writeparam_list_.size() > MAXLISTSIZE) {
        FreeWriteParam((write_param*)req);
    } else {
        parent->writeparam_list_.push_back((write_param*)req);
    }
    if (status < 0) {
        LOGE("send data error:" << GetUVError(status));
        fprintf(stderr, "send error %s.%s\n", uv_err_name(status), uv_strerror(status));
    }
}

void GetPacket(const NetPacket& packethead, const unsigned char* packetdata, void* userdata)
{
    fprintf(stdout, "Get control packet type %d\n", packethead.type);
    assert(userdata);
    TcpClientCtx* theclass = (TcpClientCtx*)userdata;
    TCPServer* parent = (TCPServer*)theclass->parent_server;
    const std::string& senddata = parent->protocol_->ParsePacket(packethead, packetdata);
    parent->sendinl(senddata, theclass);
    return;
}

TcpClientCtx* AllocTcpClientCtx(void* parentserver)
{
    TcpClientCtx* ctx = (TcpClientCtx*)malloc(sizeof(*ctx));
    ctx->packet_ = new PacketSync;
    ctx->read_buf_.base = (char*)malloc(BUFFER_SIZE);
    ctx->read_buf_.len = BUFFER_SIZE;
    ctx->parent_server = parentserver;
    ctx->parent_acceptclient = NULL;
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
    write_param* param = (write_param*)malloc(sizeof(write_param));
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

}