#include "tcpserver.h"
#include "log4z.h"

namespace uv
{
/*****************************************TCP Server*************************************************************/
TCPServer::TCPServer(char packhead, char packtail)
    :PACKET_HEAD(packhead), PACKET_TAIL(packtail)
    ,newconcb_(nullptr), newconcb_userdata_(nullptr),closedcb_(nullptr),closedcb_userdata_(nullptr)
    ,isclosed_(true),isuseraskforclosed_(false)
    ,startstatus_(START_DIS)
{
    int iret = uv_loop_init(&loop_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout,"init loop error: %s\n",errmsg_.c_str());
    }
    iret = uv_mutex_init(&mutex_clients_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        fprintf(stdout,"uv_mutex_init error: %s\n",errmsg_.c_str());
    }
}


TCPServer::~TCPServer()
{
    Close();
    uv_thread_join(&start_threadhandle_);//事件循环退出表明真正结束
    uv_mutex_destroy(&mutex_clients_);
    uv_loop_close(&loop_);
    LOGI("tcp server exit.");
}

//初始化与关闭--服务器与客户端一致
bool TCPServer::init()
{
    if (!isclosed_) {
        return true;
    }
    int iret = uv_prepare_init(&loop_, &prepare_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_prepare_start(&prepare_handle_, PrepareCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    prepare_handle_.data = this;

    iret = uv_check_init(&loop_, &check_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_check_start(&check_handle_, CheckCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    check_handle_.data = this;

    iret = uv_idle_init(&loop_, &idle_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_idle_start(&idle_handle_, IdleCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    idle_handle_.data = this;

    iret = uv_tcp_init(&loop_,&server_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    server_handle_.data = this;

    iret = uv_tcp_nodelay(&server_handle_,  1);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    //iret = uv_tcp_keepalive(&server_, 1, 60);//调用此函数后后续函数会调用出错
    //if (iret) {
    //	errmsg_ = GetUVError(iret);
    //	return false;
    //}
    isclosed_ = false;
    return true;
}

void TCPServer::closeinl()
{
    if (isclosed_) {
        return;
    }

    uv_mutex_lock(&mutex_clients_);
    for (auto it = clients_list_.begin(); it!=clients_list_.end(); ++it) {
        auto data = it->second;
        data->Close();
    }
    //clients_list_.clear();
    uv_mutex_unlock(&mutex_clients_);
    if (!uv_is_closing((uv_handle_t*) &server_handle_)) {
        uv_close((uv_handle_t*) &server_handle_, AfterServerClose);
    }
    if (!uv_is_closing((uv_handle_t*) &prepare_handle_)) {
        uv_close((uv_handle_t*) &prepare_handle_, AfterServerClose);
    }
    if (!uv_is_closing((uv_handle_t*) &idle_handle_)) {
        uv_close((uv_handle_t*) &idle_handle_, AfterServerClose);
    }
    if (!uv_is_closing((uv_handle_t*) &check_handle_)) {
        uv_close((uv_handle_t*) &check_handle_, AfterServerClose);
    }

    //uv_close((uv_handle_t*)&check_handle_,AfterServerClose);//发过close命令，AfterClientClose触发才真正close,可通过IsClosed判断是否关闭
    //uv_close((uv_handle_t*)&prepare_handle_,AfterServerClose);//发过close命令，AfterClientClose触发才真正close,可通过IsClosed判断是否关闭
    //uv_close((uv_handle_t*)&idle_handle_,AfterServerClose);//发过close命令，AfterClientClose触发才真正close,可通过IsClosed判断是否关闭
    LOGI("close server");
}

bool TCPServer::run(int status)
{
    LOGI("server runing.");
    int iret = uv_run(&loop_,(uv_run_mode)status);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}
//属性设置--服务器与客户端一致
bool TCPServer::setNoDelay(bool enable)
{
    int iret = uv_tcp_nodelay(&server_handle_, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

bool TCPServer::setKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&server_handle_, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//作为server时的函数
bool TCPServer::bind(const char* ip, int port)
{
    struct sockaddr_in bind_addr;
    int iret = uv_ip4_addr(ip, port, &bind_addr);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_tcp_bind(&server_handle_, (const struct sockaddr*)&bind_addr,0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    //iret = uv_tcp_keepalive(&server_, 1, 60);//调用此函数后后续函数会调用出错
    //if (iret) {
    //	errmsg_ = GetUVError(iret);
    //	return false;
    //}
    LOGI("server bind ip="<<ip<<", port="<<port);
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
    iret = uv_tcp_bind(&server_handle_, (const struct sockaddr*)&bind_addr,0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    LOGI("server bind ip="<<ip<<", port="<<port);
    return true;
}

bool TCPServer::listen(int backlog)
{
    int iret = uv_listen((uv_stream_t*) &server_handle_, backlog, AcceptConnection);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    LOGI("server Start listen. Runing.......");
    fprintf(stdout,"Server Runing.......\n");
    return true;
}

bool TCPServer::Start( const char *ip, int port )
{
    serverip_ = ip;
    serverport_ = port;
    closeinl();
    if (!init()) {
        return false;
    }
    if (!bind(serverip_.c_str(),serverport_)) {
        return false;
    }
    if (!listen(SOMAXCONN)) {
        return false;
    }
    int iret = uv_thread_create(&start_threadhandle_, StartThread, this);//触发AfterConnect才算真正连接成功，所以用线程
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while ( startstatus_ == START_DIS) {
#if defined (WIN32) || defined(_WIN32)
        Sleep(100);
#else
        usleep((100) * 1000);
#endif
        if(++wait_count > 100) {
            startstatus_ = START_TIMEOUT;
            break;
        }
    }
    return startstatus_ == START_FINISH;
}

bool TCPServer::Start6( const char *ip, int port )
{
    serverip_ = ip;
    serverport_ = port;
    closeinl();
    if (!init()) {
        return false;
    }
    if (!bind6(serverip_.c_str(),serverport_)) {
        return false;
    }
    if (!listen(SOMAXCONN)) {
        return false;
    }
    int iret = uv_thread_create(&start_threadhandle_, StartThread, this);//触发AfterConnect才算真正连接成功，所以用线程
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    int wait_count = 0;
    while ( startstatus_ == START_DIS) {
#if defined (WIN32) || defined(_WIN32)
        Sleep(100);
#else
        usleep((100) * 1000);
#endif
        if(++wait_count > 100) {
            startstatus_ = START_TIMEOUT;
            break;
        }
    }
    return startstatus_ == START_FINISH;
}

void TCPServer::StartThread( void* arg )
{
    TCPServer *theclass = (TCPServer*)arg;
    theclass->startstatus_ = START_FINISH;
    theclass->run();
}

//服务器发送函数
int TCPServer::Send(int clientid, const char* data, std::size_t len)
{
    uv_mutex_lock(&mutex_clients_);
    auto itfind = clients_list_.find(clientid);
    if (itfind == clients_list_.end()) {
        uv_mutex_unlock(&mutex_clients_);
        errmsg_ = "can't find cliendid ";
        errmsg_ += std::to_string((long long)clientid);
        LOGE(errmsg_);
        return -1;
    }
    itfind->second->Send(data,len);
    uv_mutex_unlock(&mutex_clients_);
    return 1;
}

//服务器-新客户端函数
void TCPServer::AcceptConnection(uv_stream_t *server, int status)
{
    if (!server->data) {
        return;
    }
    TCPServer *tcpsock = (TCPServer *)server->data;
    if (status) {
        tcpsock->errmsg_ = GetUVError(status);
        LOGE(tcpsock->errmsg_);
        return;
    }
    int clientid = tcpsock->GetAvailaClientID();
    AcceptClient* cdata = new AcceptClient(clientid, tcpsock->PACKET_HEAD, tcpsock->PACKET_TAIL,&tcpsock->loop_);//uv_close回调函数中释放
    uv_mutex_lock(&tcpsock->mutex_clients_);
    tcpsock->clients_list_.insert(std::make_pair(clientid,cdata));//加入到链接队列
    uv_mutex_unlock(&tcpsock->mutex_clients_);
    cdata->SetClosedCB(SubClientClosed,tcpsock);
    if(!cdata->AcceptByServer((uv_tcp_t*)server)) {
        tcpsock->errmsg_ = cdata->GetLastErrMsg();
        LOGE(tcpsock->errmsg_);
        cdata->Close();
        return;
    }
    //fprintf(stdout,"Client handle %p, client id %d\n",&cdata->client_handle,clientid);
    if (tcpsock->newconcb_) {
        tcpsock->newconcb_(clientid,tcpsock->newconcb_userdata_);
    }
    LOGI("new client id="<< clientid);
    return;
}

//服务器-接收数据回调函数
void TCPServer::SetRecvCB(int clientid, ServerRecvCB cb, void *userdata)
{
    uv_mutex_lock(&mutex_clients_);
    auto itfind = clients_list_.find(clientid);
    if (itfind != clients_list_.end()) {
        itfind->second->SetRecvCB(cb,userdata);
    }
    uv_mutex_unlock(&mutex_clients_);
}



//服务器-新链接回调函数
void TCPServer::SetNewConnectCB(NewConnectCB cb, void* userdata )
{
    newconcb_ = cb;
    newconcb_userdata_ = userdata;
}

void TCPServer::SetClosedCB( TcpCloseCB pfun, void* userdata )
{
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

void TCPServer::AfterServerClose(uv_handle_t *handle)
{
    TCPServer *theclass = (TCPServer*)handle->data;
    fprintf(stdout,"Close CB handle %p\n",handle);
    if (handle == (uv_handle_t *)&theclass->server_handle_) {
        handle->data = 0;//赋值0，用于判断是否调用过
    }
    if (handle == (uv_handle_t *)&theclass->prepare_handle_) {
        handle->data = 0;//赋值0，用于判断是否调用过
    }
    if (handle == (uv_handle_t *)&theclass->check_handle_) {
        handle->data = 0;//赋值0，用于判断是否调用过
    }
    if (handle == (uv_handle_t *)&theclass->idle_handle_) {
        handle->data = 0;//赋值0，用于判断是否调用过
    }
    if (theclass->server_handle_.data == 0
        && theclass->prepare_handle_.data == 0
        && theclass->check_handle_.data == 0
        && theclass->idle_handle_.data == 0) {
        theclass->isclosed_ = true;
        theclass->isuseraskforclosed_ = false;
        LOGI("client  had closed.");
        if (theclass->closedcb_) {//通知TCPServer此客户端已经关闭
            theclass->closedcb_(-1,theclass->closedcb_userdata_);
        }
    }
}

int TCPServer::GetAvailaClientID() const
{
    static int s_id = 0;
    return ++s_id;
}



void TCPServer::StartLog( const char* logpath /*= nullptr*/ )
{
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerMonthdir(LOG4Z_MAIN_LOGGER_ID, true);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerDisplay(LOG4Z_MAIN_LOGGER_ID,false);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLevel(LOG4Z_MAIN_LOGGER_ID,LOG_LEVEL_DEBUG);
    zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerLimitSize(LOG4Z_MAIN_LOGGER_ID,100);
    if (logpath) {
        zsummer::log4z::ILog4zManager::GetInstance()->SetLoggerPath(LOG4Z_MAIN_LOGGER_ID,logpath);
    }
    zsummer::log4z::ILog4zManager::GetInstance()->Start();
}

void TCPServer::SubClientClosed( int clientid, void* userdata )
{
    TCPServer *theclass = (TCPServer*)userdata;
    uv_mutex_lock(&theclass->mutex_clients_);
    auto itfind = theclass->clients_list_.find(clientid);
    if (itfind != theclass->clients_list_.end()) {
        if (theclass->closedcb_) {
            theclass->closedcb_(clientid,theclass->closedcb_userdata_);
        }
        delete itfind->second;
        LOGI("删除客户端:"<<itfind->first);
        fprintf(stdout,"删除客户端：%d\n",itfind->first);
        theclass->clients_list_.erase(itfind);
    }
    uv_mutex_unlock(&theclass->mutex_clients_);
}


void TCPServer::PrepareCB( uv_prepare_t* handle )
{
    /////////////////////////prepare阶段检测用户是否发送关闭命令
    TCPServer *theclass = (TCPServer*)handle->data;
    //检测是否关闭
    if (theclass->isuseraskforclosed_) {
        theclass->closeinl();
        return;
    }
}

void TCPServer::CheckCB( uv_check_t* handle )
{
    TCPServer *theclass = (TCPServer*)handle->data;
    //check阶段暂时不处理任何事情
}

void TCPServer::IdleCB( uv_idle_t* handle )
{
    TCPServer *theclass = (TCPServer*)handle->data;
    //check阶段暂时不处理任何事情
}

void TCPServer::SetProtocol( int clientid, TCPServerProtocolProcess* protocol)
{
    uv_mutex_lock(&mutex_clients_);
    auto itfind = clients_list_.find(clientid);
    if (itfind != clients_list_.end()) {
        itfind->second->SetProtocolProcess(protocol);
    }
    uv_mutex_unlock(&mutex_clients_);
}

TCPServerProtocolProcess* TCPServer::GetProtocolProcess( int clientid )
{
    uv_mutex_lock(&mutex_clients_);
    auto itfind = clients_list_.find(clientid);
    if (itfind != clients_list_.end()) {
        return itfind->second->GetProtocolProcess();
    }
    uv_mutex_unlock(&mutex_clients_);
    return NULL;
}



/*****************************************AcceptClient*************************************************************/
AcceptClient::AcceptClient( int clientid, char packhead, char packtail, uv_loop_t* loop )
    :client_id_(clientid),loop_(loop)
    ,writebuf_list_(BUFFER_SIZE),isclosed_(true),isuseraskforclosed_(false)
    ,recvcb_(nullptr),recvcb_userdata_(nullptr),closedcb_(nullptr),closedcb_userdata_(nullptr)
    ,protocolprocess_(NULL)
{
    readbuffer_ = uv_buf_init((char*)malloc(BUFFER_SIZE), BUFFER_SIZE);
    writebuffer_ = uv_buf_init((char*) malloc(BUFFER_SIZE), BUFFER_SIZE);
    //初始化write线程参数
    int iret = uv_mutex_init(&mutex_writereq_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
    }
    iret = uv_mutex_init(&mutex_writebuf_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
    }
    init(packhead,packtail);
}

AcceptClient::~AcceptClient()
{
    closeinl();
    while(!isclosed_){
		ThreadSleep(10);
	}
    if (readbuffer_.base) {
        free(readbuffer_.base);
        readbuffer_.base = NULL;
        readbuffer_.len = 0;
    }
    if (writebuffer_.base) {
        free(writebuffer_.base);
        writebuffer_.base = NULL;
        writebuffer_.len = 0;
    }
    uv_mutex_destroy(&mutex_writebuf_);
    uv_mutex_destroy(&mutex_writereq_);
}

bool AcceptClient::init( char packhead, char packtail )
{
    if (!isclosed_) {
        return true;
    }
    int iret = uv_prepare_init(loop_, &prepare_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_prepare_start(&prepare_handle_, PrepareCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    prepare_handle_.data = this;

    iret = uv_tcp_init(loop_,&client_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    client_handle_.data = this;

    //启动read封装类
    readpacket_.SetPacketCB(GetPacket,this);
    readpacket_.Start(packhead,packtail);

    isclosed_ = false;
    return true;
}

void AcceptClient::closeinl()
{
    if (isclosed_) {
        return;
    }
    //停止read封装类
    readpacket_.Stop();

    uv_mutex_lock(&mutex_writereq_);
    for (auto it= writereq_list_.begin(); it!=writereq_list_.end(); ++it) {
        free(*it);
    }
    writereq_list_.clear();
    uv_mutex_unlock(&mutex_writereq_);

    uv_close((uv_handle_t*)&client_handle_,AfterClientClose);
    uv_close((uv_handle_t*)&prepare_handle_,AfterClientClose);//发过close命令，AfterClientClose触发才真正close,可通过IsClosed判断是否关闭
    LOGI("客户端("<<this<<")close");
}

//回调一帧数据给用户
void AcceptClient::GetPacket( const NetPacket& packethead, const unsigned char* packetdata, void* userdata )
{
    if (!userdata) {
        return;
    }
    AcceptClient *theclass = (AcceptClient*)userdata;
    if (theclass->protocolprocess_) {//协议处理
        const std::string& senddata = theclass->protocolprocess_->ParsePacket(packethead,packetdata);
        theclass->Send(senddata.c_str(),senddata.length());
    }
    if (theclass->recvcb_) {//把得到的数据回调给用户
        theclass->recvcb_(theclass->client_id_,packethead,packetdata,theclass->recvcb_userdata_);
    }
}

//发送数据：把数据压入队列，由WriteThread线程负责发送
int AcceptClient::Send( const char* data, std::size_t len )
{
    if (!data || len <= 0) {
        errmsg_ = "send data is null or len less than zero.";
        LOGE(errmsg_);
        return 0;
    }
    size_t iret = 0;
    while(!isuseraskforclosed_) {
        uv_mutex_lock(&mutex_writebuf_);
        iret +=writebuf_list_.write(data+iret,len-iret);
        uv_mutex_unlock(&mutex_writebuf_);
        if (iret < len) {
            ThreadSleep(100);
            continue;
        } else {
            break;
        }
    }
    return iret;
}

void AcceptClient::AfterSend( uv_write_t *req, int status )
{
    //回收uv_write_t
    AcceptClient *theclass = (AcceptClient*)req->data;
    uv_mutex_lock(&theclass->mutex_writereq_);
    theclass->writereq_list_.push_back(req);
    uv_mutex_unlock(&theclass->mutex_writereq_);
    if (status < 0) {
        LOGE("发送数据有误:"<<GetUVError(status));
        fprintf(stderr, "Write error %s.%s\n",uv_err_name(status),uv_strerror(status));
    }
}

bool AcceptClient::AcceptByServer( uv_tcp_t* server )
{
    int iret = uv_accept((uv_stream_t*)server, (uv_stream_t*)&client_handle_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    iret = uv_read_start((uv_stream_t*)&client_handle_, AllocBufferForRecv, AfterRecv);//服务器开始接收客户端的数据
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE(errmsg_);
        return false;
    }
    return true;
}

//分配空间接收数据
void AcceptClient::AllocBufferForRecv(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    AcceptClient *client = (AcceptClient*)handle->data;
    assert(client);
    *buf = client->readbuffer_;
}
//接收数据回调
void AcceptClient::AfterRecv(uv_stream_t *handle, ssize_t nread, const uv_buf_t* buf)
{
    AcceptClient *theclass = (AcceptClient*)handle->data;//服务器的recv带的是clientdata
    assert(theclass);
    if (nread < 0) {/* Error or EOF */
        if (nread == UV_EOF) {
            fprintf(stdout,"客户端(%d)主动断开\n",theclass->client_id_);
            LOGW("客户端("<<theclass->client_id_<<")主动断开");
        } else if (nread == UV_ECONNRESET) {
            fprintf(stdout,"客户端(%d)异常断开\n",theclass->client_id_);
            LOGW("客户端("<<theclass->client_id_<<")异常断开");
        } else {
            fprintf(stdout,"%s\n",GetUVError(nread));
            LOGW("客户端("<<theclass->client_id_<<")异常断开："<<GetUVError(nread));
        }
        theclass->Close();
        return;
    } else if (0 == nread)  {/* Everything OK, but nothing read. */

    } else {
        theclass->readpacket_.recvdata((const unsigned char*)buf->base,nread);//新方式-解析完包后再回调数据
    }
}

void AcceptClient::AfterClientClose( uv_handle_t *handle )
{
    AcceptClient *theclass = (AcceptClient*)handle->data;
    assert(theclass);
    if (handle == (uv_handle_t *)&theclass->prepare_handle_) {
        handle->data = 0;//赋值0，用于判断是否调用过
    }
    if (handle == (uv_handle_t *)&theclass->client_handle_) {
        handle->data = 0;//赋值0，用于判断是否调用过
    }
    if (theclass->prepare_handle_.data == 0
        && theclass->client_handle_.data == 0) {
        theclass->isclosed_ = true;
        theclass->isuseraskforclosed_ = false;
        LOGI("client  had closed.");
        if (theclass->closedcb_) {//通知TCPServer此客户端已经关闭
            theclass->closedcb_(theclass->client_id_,theclass->closedcb_userdata_);
        }
    }
}

void AcceptClient::SetRecvCB( ServerRecvCB pfun, void* userdata )
{
    //在GetPacket触发
    recvcb_ = pfun;
    recvcb_userdata_ = userdata;
}

void AcceptClient::SetClosedCB( TcpCloseCB pfun, void* userdata )
{
    //在AfterRecv触发
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}


void AcceptClient::PrepareCB( uv_prepare_t* handle )
{
    /////////////////////////prepare阶段检测用户是否发送关闭命令，是否有数据要发送
    AcceptClient *theclass = (AcceptClient*)handle->data;
    assert(theclass);
    //检测是否关闭
    if (theclass->isuseraskforclosed_) {
        theclass->closeinl();
        return;
    }
    //检测是否有数据要发送
    uv_mutex_lock(&theclass->mutex_writebuf_);
    if(theclass->writebuf_list_.empty()) {
        uv_mutex_unlock(&theclass->mutex_writebuf_);
        return;//没有数据要发送，退出
    }
    theclass->writebuffer_.len = theclass->writebuf_list_.read(theclass->writebuffer_.base,BUFFER_SIZE);//得到要发送的数据
    uv_mutex_unlock(&theclass->mutex_writebuf_);

    //获取可用的uv_write_t
    uv_write_t *req = NULL;
    uv_mutex_lock(&theclass->mutex_writereq_);
    if (theclass->writereq_list_.empty()) {
        uv_mutex_unlock(&theclass->mutex_writereq_);
        req = (uv_write_t*)malloc(sizeof(*req));
        req->data = theclass;
    } else {
        req = theclass->writereq_list_.front();
        theclass->writereq_list_.pop_front();
        uv_mutex_unlock(&theclass->mutex_writereq_);
    }
    int iret = uv_write((uv_write_t*)req, (uv_stream_t*)&theclass->client_handle_, &theclass->writebuffer_, 1, AfterSend);//发送
    if (iret) {
        theclass->writereq_list_.push_back(req);//发送失败，不会调用AfterSend函数，所以得回收req
        LOGE("客户端("<<theclass<<") send error:"<<GetUVError(iret));
        fprintf(stdout,"send error. %s-%s\n",uv_err_name(iret),uv_strerror(iret));
    }
}

TCPServerProtocolProcess* AcceptClient::GetProtocolProcess( void ) const
{
    return protocolprocess_;
}
}