/***************************************
* @file     tcpserverprotocolprocess.h
* @brief    TCP Server protocol pure base class,need to subclass.
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

    //parse the recv packet, and make the response packet return. see test_tcpserver.cpp for example
	//packet     : the recv packet
	//buf        : the packet data
	//std::string: the response packet. no response can't return empty string.
    virtual const std::string& ParsePacket(const NetPacket& packet, const unsigned char* buf) = 0;
};

#endif//TCP_SERVER_PROTOCOL_PROCESS_H