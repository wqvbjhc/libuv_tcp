/***************************************
* @file     packet.h
* @brief    TCP 数据包封装.依赖libuv,openssl.功能：接收数据，解析得到一帧后回调用户去处理
* @details  根据net_base.h中NetPacket的定义，对数据包进行封装。
            使用pod_circularbuffer.h中的类作为接收数据的缓冲区
            线程安全，使用libuv中的锁保证
			md5校验码使用openssl函数
//调用方法
Packet pac;
pac.SetPacketCB(GetPacket,&serpac);
packet.Start(0x01,0x02);
//socket有数据到达时，调用packet.recvdata((const unsigned char*)buf,bufsize); 只要足够一帧它会触发GetPacket

* @author   陈吉宏, wqvbjhc@gmail.com
* @date     2014-05-21
* @mod      2014-07-24 phata 添加GetUVError，PacketData两个公共函数. TCP Server Client会调用到
                             添加TCP编程使用到的回调函数定义
****************************************/
#ifndef PACKET_H
#define PACKET_H
#include <algorithm>
#include "uv.h"
#include <openssl/md5.h>
#include "net_base.h"
#include "../pod_circularbuffer.h"
#if defined (WIN32) || defined(_WIN32)
#include <windows.h>
#define ThreadSleep(ms) Sleep(ms);//睡眠ms毫秒
#elif defined __linux__
#include <unistd.h>
#define ThreadSleep(ms) usleep((ms) * 1000)//睡眠ms毫秒
#endif

#ifdef _MSC_VER
#ifdef NDEBUG
#pragma comment(lib,"libuv.lib")
#pragma comment(lib,"libeay32.lib")
#else
#pragma comment(lib,"libuvd.lib")
#pragma comment(lib,"libeay32d.lib")
#endif
#endif
typedef void (*GetFullPacket)(const NetPacket& packethead, const unsigned char* packetdata, void* userdata);

#ifndef BUFFER_SIZE
#define BUFFER_SIZE (1024*10)
#endif

class Packet
{
public:
    Packet():packet_cb_(NULL),packetcb_userdata_(NULL)
        ,circulebuffer_(BUFFER_SIZE)
        ,uvthread_(0)
    {
    }
    virtual ~Packet()
    {
        Stop();
    }

    bool Start(char packhead, char packtail)
    {
        if (uvthread_) {
            return true;
        }
        HEAD = packhead;
        TAIL= packtail;
        uv_mutex_init(&uvmutex_cirbuf_);

        is_uvthread_stop_= false;
        uv_thread_create(&uvthread_,ParseThread,this);//启动解析线程
        return true;
    }
    void Stop()
    {
        if (0 == uvthread_) {//通过uvthread_判断
            return;
        }
        is_uvthread_stop_ = true;
        uv_thread_join(&uvthread_);
        uvthread_ = 0;

        uv_mutex_destroy(&uvmutex_cirbuf_);
    }
public:
    void recvdata(const unsigned char* data, int len)   //接收到数据，把数据保存在circulebuffer_
    {
        int iret = 0;
        while(1) {
            uv_mutex_lock(&uvmutex_cirbuf_);
            iret +=circulebuffer_.write(data+iret,len-iret);
            uv_mutex_unlock(&uvmutex_cirbuf_);
            if (iret < len) {
                ThreadSleep(100);
                continue;
            } else {
                break;
            }
        }
    }
    void SetPacketCB(GetFullPacket pfun, void *userdata)
    {
        packet_cb_ = pfun;
        packetcb_userdata_ = userdata;
    }
protected:
    static void ParseThread(void *arg)
    {
        Packet *theclass = (Packet*)arg;
        if (!theclass) {
            return;
        }
        uv_buf_t  thread_readdata = uv_buf_init((char*)malloc(BUFFER_SIZE),BUFFER_SIZE);//负责从circulebuffer_读取数据
        uv_buf_t  thread_packetdata = uv_buf_init((char*)malloc(BUFFER_SIZE),BUFFER_SIZE);//负责从circulebuffer_读取packet 中data部分
        int truepacketlen = 0;//readdata有效数据长度
        int headpos = -1;//找到头位置
        char *headpt = NULL;//找到头位置
        const unsigned char HEAD = theclass->HEAD;//包头
        const unsigned char TAIL = theclass->TAIL;//包尾
        NetPacket thread_netpacket;

        unsigned char md5str[MD5_DIGEST_LENGTH];

        while(!theclass->is_uvthread_stop_) {
            uv_mutex_lock(&theclass->uvmutex_cirbuf_);
            if (theclass->circulebuffer_.empty() && truepacketlen < NET_PACKAGE_HEADLEN+3) {
                uv_mutex_unlock(&theclass->uvmutex_cirbuf_);
                ThreadSleep(100);
                continue;
            }
            truepacketlen += theclass->circulebuffer_.read((unsigned char*)thread_readdata.base+truepacketlen,thread_readdata.len-truepacketlen);
            uv_mutex_unlock(&theclass->uvmutex_cirbuf_);
            //fprintf(stdout,"接收到的数据为:\n");;
            //for (int i=0; i<truepacketlen; ++i) {
            //    if (thread_readdata.base[i] == HEAD) {
            //        fprintf(stdout," %02X ",(unsigned char)thread_readdata.base[i]);
            //    } else {
            //        fprintf(stdout,"%02X",(unsigned char)thread_readdata.base[i]);
            //    }
            //}
            //fprintf(stdout,"\n");;
            //if (truepacketlen < NET_PACKAGE_HEADLEN + 1) {//数据不够解析帧头，先缓存
            //    //fprintf(stdout,"读取%d数据，数据不够解析帧头，先缓存\n",truepacketlen);
            //    continue;
            //}
            //在接收到的数据查找包头
            //1.找不到，丢弃所有数据。(说明非法客户端在攻击)
            //2.在中间找到，丢弃前面的。
            //2.1解析帧头 -得一帧-校验
            //2.2数据无法得到帧头，先缓存起来
            //2.3根据帧头得一帧
            //2.4校验
            //2.3校验成功。完成一帧解析，继续下一帧
            //2.4校验失败。从包头的下一位置开始找头，转向步骤1
            headpt = (char*)memchr(thread_readdata.base,HEAD,truepacketlen);
            if (!headpt) {//1
                fprintf(stdout,"读取%d数据，找不到包头\n",truepacketlen);
                truepacketlen = 0;//标记thread_readdata里的数据为无效
                continue;
            }
            headpos = headpt - thread_readdata.base;
            if (truepacketlen - headpos - 1< NET_PACKAGE_HEADLEN) {//2.2
                //fprintf(stdout,"读取%d数据，找到包头,位于%d,数据不够解析帧头，先缓存\n",truepacketlen,headpos);
                memmove(thread_readdata.base,thread_readdata.base+headpos+1,truepacketlen - headpos - 1);
                truepacketlen -= headpos - 1;
                continue;
            }
            //得帧头
            headpt = &thread_readdata.base[headpos+1];
            //fprintf(stdout,"recv netpacketchar:\n");;
            //for (int i=0; i<NET_PACKAGE_HEADLEN; ++i) {
            //    fprintf(stdout,"%02X",(unsigned char)(headpt)[i]);
            //}
            //fprintf(stdout,"\n");;
            CharToNetPacket((const unsigned char*)(headpt),thread_netpacket);
            if (thread_netpacket.header != HEAD || thread_netpacket.tail != TAIL || thread_netpacket.datalen <= 0) {//帧头数据不合法
                fprintf(stdout,"读取%d数据,包头位于%d. 帧数据不合法(head:%x,tail:%x,datalen:%d)\n",truepacketlen,headpos,thread_netpacket.header,
                        thread_netpacket.tail,thread_netpacket.datalen);
                memmove(thread_readdata.base,thread_readdata.base+headpos+1,truepacketlen - headpos-1);//2.4
                truepacketlen -= headpos-1;
                continue;
            }
            //得帧数据
            if (thread_packetdata.len < (size_t)thread_netpacket.datalen+1) {//包含最后的tail
                thread_packetdata.base = (char*)realloc(thread_packetdata.base,thread_netpacket.datalen+1);
                thread_packetdata.len = thread_netpacket.datalen+1;
            }
            int getdatalen = (std::min)((int)(truepacketlen - headpos - 1- NET_PACKAGE_HEADLEN),(int)(thread_netpacket.datalen+1));
            //先从thread_readdata中取
            if (getdatalen > 0) {
                //fprintf(stdout,"Get %d data from thread_readdata\n",getdatalen);
                memcpy(thread_packetdata.base,thread_readdata.base+headpos+1+NET_PACKAGE_HEADLEN,getdatalen);
            }
            //不够再从theclass->circulebuffer_中取
            while(getdatalen < thread_netpacket.datalen+1 && !theclass->is_uvthread_stop_) {
                //fprintf(stdout,"Get data from while. %d total %d\n",getdatalen,thread_netpacket.datalen+1);
                ThreadSleep(100);
                fprintf(stdout,"get packet data %d-%d\n",getdatalen,thread_netpacket.datalen+1);
                uv_mutex_lock(&theclass->uvmutex_cirbuf_);
                getdatalen += theclass->circulebuffer_.read((unsigned char*)thread_packetdata.base+getdatalen,thread_netpacket.datalen+1-getdatalen);
                uv_mutex_unlock(&theclass->uvmutex_cirbuf_);
            }
            //fprintf(stdout,"包数据和尾部为:\n");;
            //for (int i=0; i<thread_netpacket.datalen+1; ++i) {
            //    fprintf(stdout,"%02X",(unsigned char)thread_packetdata.base[i]);
            //}
            //fprintf(stdout,"\n");;

            //检测校验码与最后一位
            if (thread_packetdata.base[thread_netpacket.datalen] != TAIL) {
                fprintf(stdout,"读取%d数据,包头位于%d. 包尾数据不合法(tail:%02x)\n",truepacketlen,headpos,thread_packetdata.base[thread_netpacket.datalen]);
                if (thread_readdata.len < 2 + NET_PACKAGE_HEADLEN + thread_netpacket.datalen ) {//包含最后的tail
                    thread_readdata.base = (char*)realloc(thread_readdata.base,thread_netpacket.datalen+2+NET_PACKAGE_HEADLEN);
                    thread_readdata.len = thread_netpacket.datalen+2+NET_PACKAGE_HEADLEN;
                }
                memmove(thread_readdata.base,thread_readdata.base+headpos+1,1 + NET_PACKAGE_HEADLEN);//2.4
                truepacketlen = 1 + NET_PACKAGE_HEADLEN;
                memcpy(thread_readdata.base+truepacketlen,thread_packetdata.base,thread_netpacket.datalen+1);
                truepacketlen +=thread_netpacket.datalen+1;
                continue;
            }
            MD5_CTX md5;
            MD5_Init(&md5);
            MD5_Update(&md5,thread_packetdata.base,thread_netpacket.datalen);//包数据的校验值
            MD5_Final(md5str,&md5);
            if (memcmp(thread_netpacket.check,md5str,MD5_DIGEST_LENGTH) != 0) {
                //{
                fprintf(stdout,"读取%d数据,包头位于%d. 校验码不合法\n",truepacketlen,headpos,thread_packetdata.base[thread_netpacket.datalen]);
                //    fprintf(stdout,"recvmd5:");
                //    for (int i=0; i< MD5_DIGEST_LENGTH; ++i) {
                //        fprintf(stdout,"%02x",thread_netpacket.check[i]);
                //    }
                //    fprintf(stdout,"\ncal md5:");
                //    for (int i=0; i< MD5_DIGEST_LENGTH; ++i) {
                //        fprintf(stdout,"%02x",md5str[i]);
                //    }
                //    fprintf(stdout,"\n");;
                //}
                if (thread_readdata.len < 2 + NET_PACKAGE_HEADLEN + thread_netpacket.datalen ) {//包含最后的tail
                    thread_readdata.base = (char*)realloc(thread_readdata.base,thread_netpacket.datalen+2+NET_PACKAGE_HEADLEN);
                    thread_readdata.len = thread_netpacket.datalen+2+NET_PACKAGE_HEADLEN;
                }
                memmove(thread_readdata.base,thread_readdata.base+headpos+1,1 + NET_PACKAGE_HEADLEN);//2.4
                truepacketlen = 1 + NET_PACKAGE_HEADLEN;
                memcpy(thread_readdata.base+truepacketlen,thread_packetdata.base,thread_netpacket.datalen+1);
                truepacketlen +=thread_netpacket.datalen+1;
                continue;
            }
            truepacketlen = 0;//从新开始读取数据
            //{
            //    thread_packetdata.base[thread_netpacket.datalen] = '\0';
            //    fprintf(stdout,"获取一帧数据成功,datalen:%d, data:\n%s\n",thread_netpacket.datalen,thread_packetdata.base);
            //}
            //回调帧数据给用户
            if (theclass->packet_cb_) {
                theclass->packet_cb_(thread_netpacket,(const unsigned char*)thread_packetdata.base,theclass->packetcb_userdata_);
            }
        }
    }
private:
    uv_mutex_t uvmutex_cirbuf_;//对circulebuffer_进行保护
    PodCircularBuffer<unsigned char> circulebuffer_;//接收数据的缓冲区

    unsigned char HEAD;
    unsigned char TAIL;

    uv_thread_t uvthread_;//解析数据线程ID
    bool  is_uvthread_stop_;//

    GetFullPacket packet_cb_;//回调函数
    void*         packetcb_userdata_;//回调函数所带的自定义数据
private:// no copy
    Packet(const Packet &);
    Packet& operator = (const Packet &);
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
    unsigned char md5str[MD5_DIGEST_LENGTH];
    MD5_CTX md5;
    MD5_Init(&md5);
    MD5_Update(&md5,data,packet.datalen);
    MD5_Final(md5str,&md5);
    memcpy(packet.check,md5str,MD5_DIGEST_LENGTH);

    unsigned char packchar[NET_PACKAGE_HEADLEN];
    NetPacketToChar(packet,packchar);

    std::string retstr;
    retstr.append(1,packet.header);
    retstr.append((const char*)packchar,NET_PACKAGE_HEADLEN);
    retstr.append((const char*)data,packet.datalen);
    retstr.append(1,packet.tail);
    return std::move(retstr);
}

/*****************************
* @brief   获取libuv错误码对应的错误信息
* @param   retcode --libuv函数错误码(不等于0的返回值)
* @return  std::string --返回的详细错误说明
******************************/
inline std::string GetUVError(int retcode)
{
    std::string err;
    err = uv_err_name(retcode);
    err +=":";
    err += uv_strerror(retcode);
    return std::move(err);
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

#endif//PACKET_H