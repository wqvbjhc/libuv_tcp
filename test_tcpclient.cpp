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
    fprintf(stdout, "cliend %d close\n", clientid);
    TCPClient* client = (TCPClient*)userdata;
    client->Close();
}

void ReadCB(const NetPacket& packet, const unsigned char* buf, void* userdata)
{
    fprintf(stdout,"call time %d\n",++call_time);
    if (call_time > 5000) {
        return;
    }
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
    TCPClient** pClients = new TCPClient*[clientsize];
    TCPClient::StartLog("log/");

    int i = 0;
    char senddata[256];
    for (int i = 0; i < clientsize; ++i) {
        pClients[i] = new TCPClient(0x01, 0x02);
        pClients[i]->SetRecvCB(ReadCB, pClients[i]);
        pClients[i]->SetClosedCB(CloseCB, pClients[i]);
        if (!pClients[i]->Connect(serverip.c_str(), 12345)) {
            fprintf(stdout, "connect error:%s\n", pClients[i]->GetLastErrMsg());
        } else {
            fprintf(stdout, "client(%p) connect succeed.\n", pClients[i]);
        }
        memset(senddata, 0, sizeof(senddata));
        sprintf(senddata, "client(%p) call %d", pClients[i], ++call_time);
        NetPacket packet;
        packet.header = 0x01;
        packet.tail = 0x02;
        packet.datalen = (std::min)(strlen(senddata), sizeof(senddata) - 1);
        std::string str = PacketData(packet, (const unsigned char*)senddata);
        if (pClients[i]->Send(&str[0], str.length()) <= 0) {
            fprintf(stdout, "(%p)send error.%s\n", pClients[i], pClients[i]->GetLastErrMsg());
        } else {
            //fprintf(stdout,"发送的数据为:\n");
            //         for (int i=0; i<str.length(); ++i) {
            //             fprintf(stdout,"%02X",(unsigned char)str[i]);
            //         }
            //         fprintf(stdout,"\n");
            fprintf(stdout, "send succeed:%s\n", senddata);
        }
    }
    while (!is_exist) {
        Sleep(10);
    }
    return 0;
}