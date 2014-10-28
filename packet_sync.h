/***************************************
* @file     packet_sync.h
* @brief    TCP 数据包封装.依赖libuv,openssl.功能：接收数据，解析得到一帧后回调给用户。同步处理，接收到马上解析
* @details  根据net_base.h中NetPacket的定义，对数据包进行封装。
			md5校验码使用openssl函数
			同一线程中实时解码
			长度为0的md5为：d41d8cd98f00b204e9800998ecf8427e，改为全0. 编解码时修改。
//调用方法
Packet packet;
packet.SetPacketCB(GetPacket,&serpac);
packet.Start(0x01,0x02);
//socket有数据到达时，调用packet.recvdata((const unsigned char*)buf,bufsize); 只要足够一帧它会触发GetFullPacket

* @author   phata, wqvbjhc@gmail.com
* @date     2014-05-21
* @mod      2014-08-04 phata 修复解析一帧数据有误的bug
            2014-11-12 phata GetUVError冲突，改为使用thread_uv.h中的
****************************************/
#ifndef PACKET_SYNC_H
#define PACKET_SYNC_H
#include <algorithm>
#include <openssl/md5.h>
#include "net/net_base.h"
#include "sys/thread_uv.h"//for GetUVError
#if defined (WIN32) || defined(_WIN32)
#include <windows.h>
#define ThreadSleep(ms) Sleep(ms);//睡眠ms毫秒
#elif defined __linux__
#include <unistd.h>
#define ThreadSleep(ms) usleep((ms) * 1000)//睡眠ms毫秒
#endif

#ifdef _MSC_VER
#ifdef NDEBUG
#pragma comment(lib,"libeay32.lib")
#else
#pragma comment(lib,"libeay32d.lib")
#endif
#endif
typedef void (*GetFullPacket)(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);

#ifndef BUFFER_SIZE
#define BUFFER_SIZE (1024*10)
#endif

class PacketSync
{
public:
    PacketSync(): packet_cb_(NULL), packetcb_userdata_(NULL) {
        thread_readdata = uv_buf_init((char*)malloc(BUFFER_SIZE), BUFFER_SIZE); //负责从circulebuffer_读取数据
        thread_packetdata = uv_buf_init((char*)malloc(BUFFER_SIZE), BUFFER_SIZE); //负责从circulebuffer_读取packet 中data部分
        truepacketlen = 0;//readdata有效数据长度
        headpos = -1;//找到头位置
        headpt = NULL;//找到头位置
        parsetype = PARSE_NOTHING;
        getdatalen = 0;
    }
    virtual ~PacketSync() {
        free(thread_readdata.base);
        free(thread_packetdata.base);
    }

    bool Start(char packhead, char packtail) {
        HEAD = packhead;
        TAIL = packtail;
        return true;
    }

public:
    void recvdata(const unsigned char* data, int len) { //接收到数据，把数据保存在circulebuffer_
        int iret = 0;
        while (iret < len || truepacketlen >= NET_PACKAGE_HEADLEN + 2) {
            if (PARSE_NOTHING == parsetype) {//未解析出head
                if (thread_readdata.len - truepacketlen >= len - iret) {
                    memcpy(thread_readdata.base + truepacketlen, data + iret, len - iret);
                    truepacketlen += len - iret;
                    iret  = len;
                } else {
                    memcpy(thread_readdata.base + truepacketlen, data + iret, thread_readdata.len - truepacketlen);
                    iret += thread_readdata.len - truepacketlen;
                    truepacketlen = thread_readdata.len;
                }
                headpt = (char*)memchr(thread_readdata.base, HEAD, truepacketlen);
                if (!headpt) {//1
                    fprintf(stdout, "读取%d数据，找不到包头\n", truepacketlen);
                    truepacketlen = 0;//标记thread_readdata里的数据为无效
                    continue;
                }
                headpos = headpt - thread_readdata.base;
                if (truepacketlen - headpos - 1 < NET_PACKAGE_HEADLEN) { //2.2
                    if (headpos != 0) {
                        fprintf(stdout, "读取%d数据，找到包头,位于%d,数据不够解析帧头，先缓存\n", truepacketlen, headpos);
                        memmove(thread_readdata.base, thread_readdata.base + headpos, truepacketlen - headpos);
                        truepacketlen -= headpos;
                    }
                    continue;
                }
                //得帧头
                headpt = &thread_readdata.base[headpos + 1];
                //fprintf(stdout,"recv netpacketchar:\n");;
                //for (int i=0; i<NET_PACKAGE_HEADLEN; ++i) {
                //    fprintf(stdout,"%02X",(unsigned char)(headpt)[i]);
                //}
                //fprintf(stdout,"\n");
                CharToNetPacket((const unsigned char*)(headpt), theNexPacket);
                if (theNexPacket.header != HEAD || theNexPacket.tail != TAIL || theNexPacket.datalen < 0) {//帧头数据不合法(帧长允许为0)
                    fprintf(stdout, "读取%d数据,包头位于%d. 帧数据不合法(head:%02x,tail:%02x,datalen:%d)\n",
                            truepacketlen, headpos, theNexPacket.header, theNexPacket.tail, theNexPacket.datalen);
                    memmove(thread_readdata.base, thread_readdata.base + headpos + 1, truepacketlen - headpos - 1); //2.4
                    truepacketlen -= headpos + 1;
                    continue;
                }
                parsetype = PARSE_HEAD;
                //得帧数据
                if (thread_packetdata.len < (size_t)theNexPacket.datalen + 1) { //包含最后的tail
                    thread_packetdata.base = (char*)realloc(thread_packetdata.base, theNexPacket.datalen + 1);
                    thread_packetdata.len = theNexPacket.datalen + 1;
                }
                getdatalen = (std::min)((int)(truepacketlen - headpos - 1 - NET_PACKAGE_HEADLEN), (int)(theNexPacket.datalen + 1));
                //先从thread_readdata中取
                if (getdatalen > 0) {
                    memcpy(thread_packetdata.base, thread_readdata.base + headpos + 1 + NET_PACKAGE_HEADLEN, getdatalen);
                }
            }

            //解析出head,在接收data
            if (getdatalen < theNexPacket.datalen + 1) {
                if (getdatalen + (len - iret) < theNexPacket.datalen + 1) {
                    memcpy(thread_packetdata.base + getdatalen, data + iret, len - iret);
                    getdatalen += len - iret;
                    iret = len;
                    return;//等待下一轮的读取
                } else {
                    memcpy(thread_packetdata.base + getdatalen, data + iret, theNexPacket.datalen + 1 - getdatalen);
                    iret += theNexPacket.datalen + 1 - getdatalen;
                    getdatalen = theNexPacket.datalen + 1;
                }
            }
            //检测校验码与最后一位
            if (thread_packetdata.base[theNexPacket.datalen] != TAIL) {
                fprintf(stdout, "包数据长%d, 包尾数据不合法(tail:%02x)\n", theNexPacket.datalen,
                        (unsigned char)(thread_packetdata.base[theNexPacket.datalen]));
                if (truepacketlen - headpos - 1 - NET_PACKAGE_HEADLEN >= theNexPacket.datalen + 1) {//thread_readdata数据足够
                    memmove(thread_readdata.base, thread_readdata.base + headpos + 1, truepacketlen - headpos - 1); //2.4
                    truepacketlen -= headpos + 1;
                } else {//thread_readdata数据不足
                    if (thread_readdata.len < NET_PACKAGE_HEADLEN + theNexPacket.datalen + 1) {//包含最后的tail
                        thread_readdata.base = (char*)realloc(thread_readdata.base, NET_PACKAGE_HEADLEN + theNexPacket.datalen + 1);
                        thread_readdata.len = NET_PACKAGE_HEADLEN + theNexPacket.datalen + 1;
                    }
                    memmove(thread_readdata.base, thread_readdata.base + headpos + 1, NET_PACKAGE_HEADLEN); //2.4
                    truepacketlen = NET_PACKAGE_HEADLEN;
                    memcpy(thread_readdata.base + truepacketlen, thread_packetdata.base, theNexPacket.datalen + 1);
                    truepacketlen += theNexPacket.datalen + 1;
                }
                parsetype = PARSE_NOTHING;//重头再来
                continue;
            }
            if (0 == theNexPacket.datalen) { //长度为0的md5为：d41d8cd98f00b204e9800998ecf8427e，改为全0
                memset(md5str, 0, sizeof(md5str));
            } else {
                MD5_CTX md5;
                MD5_Init(&md5);
                MD5_Update(&md5, thread_packetdata.base, theNexPacket.datalen); //包数据的校验值
                MD5_Final(md5str, &md5);
            }
            if (memcmp(theNexPacket.check, md5str, MD5_DIGEST_LENGTH) != 0) {
                fprintf(stdout, "读取%d数据, 校验码不合法\n", NET_PACKAGE_HEADLEN + theNexPacket.datalen + 2);
                if (truepacketlen - headpos - 1 - NET_PACKAGE_HEADLEN >= theNexPacket.datalen + 1) {//thread_readdata数据足够
                    memmove(thread_readdata.base, thread_readdata.base + headpos + 1, truepacketlen - headpos - 1); //2.4
                    truepacketlen -= headpos + 1;
                } else {//thread_readdata数据不足
                    if (thread_readdata.len < NET_PACKAGE_HEADLEN + theNexPacket.datalen + 1) {//包含最后的tail
                        thread_readdata.base = (char*)realloc(thread_readdata.base, NET_PACKAGE_HEADLEN + theNexPacket.datalen + 1);
                        thread_readdata.len = NET_PACKAGE_HEADLEN + theNexPacket.datalen + 1;
                    }
                    memmove(thread_readdata.base, thread_readdata.base + headpos + 1, NET_PACKAGE_HEADLEN); //2.4
                    truepacketlen = NET_PACKAGE_HEADLEN;
                    memcpy(thread_readdata.base + truepacketlen, thread_packetdata.base, theNexPacket.datalen + 1);
                    truepacketlen += theNexPacket.datalen + 1;
                }
                parsetype = PARSE_NOTHING;//重头再来
                continue;
            }
            if (truepacketlen - headpos - 1 - NET_PACKAGE_HEADLEN >= theNexPacket.datalen + 1) {//thread_readdata数据足够
                memmove(thread_readdata.base, thread_readdata.base + headpos + NET_PACKAGE_HEADLEN + theNexPacket.datalen + 2,
                        truepacketlen - (headpos + NET_PACKAGE_HEADLEN + theNexPacket.datalen + 2)); //2.4
                truepacketlen -= headpos + NET_PACKAGE_HEADLEN + theNexPacket.datalen + 2;
            } else {
                truepacketlen = 0;//从新开始读取数据
            }
            //回调帧数据给用户
            if (this->packet_cb_) {
                this->packet_cb_(theNexPacket, (const unsigned char*)thread_packetdata.base, this->packetcb_userdata_);
            }
            parsetype = PARSE_NOTHING;//重头再来
        }
    }
    void SetPacketCB(GetFullPacket pfun, void* userdata) {
        packet_cb_ = pfun;
        packetcb_userdata_ = userdata;
    }
private:

    GetFullPacket packet_cb_;//回调函数
    void*         packetcb_userdata_;//回调函数所带的自定义数据

    enum {
        PARSE_HEAD,
        PARSE_NOTHING,
    };
    int parsetype;
    int getdatalen;
    uv_buf_t  thread_readdata;//负责从circulebuffer_读取数据
    uv_buf_t  thread_packetdata;//负责从circulebuffer_读取packet 中data部分
    int truepacketlen;//readdata有效数据长度
    int headpos;//找到头位置
    char* headpt;//找到头位置
    unsigned char HEAD;//包头
    unsigned char TAIL;//包尾
    NetPacket theNexPacket;
    unsigned char md5str[MD5_DIGEST_LENGTH];
private:// no copy
    PacketSync(const PacketSync&);
    PacketSync& operator = (const PacketSync&);
};

/***********************************************辅助函数***************************************************/
/*****************************
* @brief   把数据组合成NetPacket格式的二进制流，可直接发送。
* @param   packet --NetPacket包，里面的version,header,tail,type,datalen,reserve必须提前赋值，该函数会计算check的值。然后组合成二进制流返回
	       data   --要发送的实际数据
* @return  std::string --返回的二进制流。地址：&string[0],长度：string.length()
******************************/
inline std::string PacketData(NetPacket& packet, const unsigned char* data)
{
    if (packet.datalen == 0 || data == NULL) {//长度为0的md5为：d41d8cd98f00b204e9800998ecf8427e，改为全0
        memset(packet.check, 0, sizeof(packet.check));
    } else {
        MD5_CTX md5;
        MD5_Init(&md5);
        MD5_Update(&md5, data, packet.datalen);
        MD5_Final(packet.check, &md5);
    }
    unsigned char packchar[NET_PACKAGE_HEADLEN];
    NetPacketToChar(packet, packchar);

    std::string retstr;
    retstr.append(1, packet.header);
    retstr.append((const char*)packchar, NET_PACKAGE_HEADLEN);
    retstr.append((const char*)data, packet.datalen);
    retstr.append(1, packet.tail);
    return std::move(retstr);
}

//客户端或服务器关闭的回调函数
//服务器：当clientid为-1时，表现服务器的关闭事件
//客户端：clientid无效，永远为-1
typedef void (*TcpCloseCB)(int clientid, void* userdata);

//TCPServer接收到新客户端回调给用户
typedef void (*NewConnectCB)(int clientid, void* userdata);

//TCPServer接收到客户端数据回调给用户
typedef void (*ServerRecvCB)(int clientid, const NetPacket& packethead, const unsigned char* buf, void* userdata);

//TCPClient接收到服务器数据回调给用户
typedef void (*ClientRecvCB)(const NetPacket& packethead, const unsigned char* buf, void* userdata);

//网络事件类型
typedef enum {
    NET_EVENT_TYPE_RECONNECT = 0,  //与服务器自动重连成功事件
    NET_EVENT_TYPE_DISCONNECT      //与服务器断开事件
} NET_EVENT_TYPE;
//TCPClient断线重连函数
typedef void (*ReconnectCB)(NET_EVENT_TYPE eventtype, void* userdata);

#endif//PACKET_SYNC_H