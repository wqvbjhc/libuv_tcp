#include <iostream>
#include <string>
#include "tcpclient.h"
#include "mswin_special/sys/DumpFile.h"
using namespace std;
using namespace uv;

std::string serverip;
int call_time = 0;
bool is_exist = false;

void CloseCB(int clientid, void* userdata)
{
    fprintf(stdout, "cliend close\n");
    TCPClient* client = (TCPClient*)userdata;
    client->Close();
}

void ReadCB(const NetPacket& packet, const unsigned char* buf, void* userdata)
{
    char senddata[256] = {0};
    TCPClient* client = (TCPClient*)userdata;
    sprintf(senddata, "****recv server data(%p,%d)", client, packet.datalen);
    fprintf(stdout, "%s\n", senddata);
    NetPacket tmppack = packet;
    tmppack.datalen = (std::min)(strlen(senddata), sizeof(senddata) - 1);
    std::string retstr = PacketData(tmppack, (const unsigned char*)senddata);
    if (client->Send(&retstr[0], retstr.length()) <= 0) {
        fprintf(stdout, "(%p)send error.%s\n", client, client->GetLastErrMsg());
    }
    fprintf(stdout, "call time %d\n", ++call_time);
    //if (call_time > 50) {
    //    is_exist = true;
    //}
}
void ReConnectCB(NET_EVENT_TYPE eventtype, void* userdata)
{
    TCPClient* client = (TCPClient*)userdata;
    if (NET_EVENT_TYPE_RECONNECT == eventtype) {
        fprintf(stdout, "succeed reconnect.\n");
		char senddata[256];
        memset(senddata, 0, sizeof(senddata));
        sprintf(senddata, "client(%p) call %d", client, ++call_time);
        NetPacket packet;
        packet.header = 0x01;
        packet.tail = 0x02;
        packet.datalen = (std::min)(strlen(senddata), sizeof(senddata) - 1);
        std::string str = PacketData(packet, (const unsigned char*)senddata);
        if (client->Send(&str[0], str.length()) <= 0) {
            fprintf(stdout, "(%p)send error.%s\n", client, client->GetLastErrMsg());
        } else {
            fprintf(stdout, "send succeed:%s\n", senddata);
        }
    } else {
        fprintf(stdout, "server disconnect.\n");
    }
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stdout, "usage: %s server_ip_address clientcount\neg.%s 192.168.1.1 50\n", argv[0], argv[0]);
        return 0;
    }
    DeclareDumpFile();
    serverip = argv[1];

    const int clientsize = std::stoi(argv[2]);
    TCPClient pClients(0x01, 0x02);
    TCPClient::StartLog("log/");

    int i = 0;
    char senddata[256];
    pClients.SetRecvCB(ReadCB, &pClients);
    pClients.SetClosedCB(CloseCB, &pClients);
    pClients.SetReconnectCB(ReConnectCB, &pClients);
    if (!pClients.Connect(serverip.c_str(), 12345)) {
        fprintf(stdout, "connect error:%s\n", pClients.GetLastErrMsg());
    } else {
        fprintf(stdout, "client(%p) connect succeed.\n", &pClients);
    }
    memset(senddata, 0, sizeof(senddata));
    sprintf(senddata, "client(%p) call %d", &pClients, ++call_time);
    NetPacket packet;
    packet.header = 0x01;
    packet.tail = 0x02;
    packet.datalen = (std::min)(strlen(senddata), sizeof(senddata) - 1);
    std::string str = PacketData(packet, (const unsigned char*)senddata);
    if (pClients.Send(&str[0], str.length()) <= 0) {
        fprintf(stdout, "(%p)send error.%s\n", &pClients, pClients.GetLastErrMsg());
    } else {
        fprintf(stdout, "send succeed:%s\n", senddata);
    }
    while (!is_exist) {
        Sleep(10);
    }
    return 0;
}