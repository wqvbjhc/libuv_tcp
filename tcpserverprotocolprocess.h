/***************************************
* @file     tcpserverprotocolprocess.h
* @brief    TCP Server协议处理纯基类，需要子类化
* @details  
* @author   phata, wqvbjhc@gmail.com
* @date     2014-7-31
****************************************/
#ifndef TCP_SERVER_PROTOCOL_PROCESS_H
#define TCP_SERVER_PROTOCOL_PROCESS_H
#include <string>

class TCPServerProtocolProcess
{
public:
    TCPServerProtocolProcess(){}
    virtual ~TCPServerProtocolProcess(){}
    //解析用户的包并生成回应包
	//应该返回const引用避免内存拷贝，所以得定义一个成员变量std::string用于返回
    virtual const std::string& ParsePacket(const NetPacket& packet, const unsigned char* buf) = 0;
};

#endif//TCP_SERVER_PROTOCOL_PROCESS_H