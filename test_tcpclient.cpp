#include <iostream>
#include <string>
#include "tcpsocket.h"
#include "mswin_special/sys/DumpFile.h"
using namespace std;
using namespace uv;

std::string serverip;
int call_time = 0;
void ReadCB(const NetPacket& packet, const unsigned char* buf, void* userdata)
{
    char senddata[256]={0};
    TCPClient *client = (TCPClient *)userdata;
    sprintf(senddata,"****recv server data(%p,%d)",client,packet.datalen);
    fprintf(stdout,"%s\n",senddata);
    //for (int i=0; i< packet.datalen; ++i) {
    //    fprintf(stdout,"%c",buf[i]);
    //}
    //fprintf(stdout,"]\n");
	NetPacket tmppack = packet;
	tmppack.datalen = (std::min)(strlen(senddata),sizeof(senddata)-1);
	std::string retstr = PacketData(tmppack,(const unsigned char*)senddata);
	if(client->Send(&retstr[0],retstr.length()) <=0) {
		fprintf(stdout,"(%p)send error.%s\n",client,client->GetLastErrMsg());
	}
}

int main(int argc, char **argv)
{
    if (argc !=3 ) {
        fprintf(stdout,"usage: %s server_ip_address clientcount\neg.%s 192.168.1.1 50\n",argv[0],argv[0]);
        return 0;
    }
	DeclareDumpFile();
	serverip = argv[1];

    const int clientsize = std::stoi(argv[2]);
    uv_loop_t **ploops = new uv_loop_t*[clientsize];
    TCPClient **pClients = new TCPClient*[clientsize];
    TCPClient::StartLog("log/");

    int i=0;
	char senddata[256];
    for (int i=0; i< clientsize; ++i) {
        ploops[i] = (uv_loop_t *)malloc(sizeof(uv_loop_t));
		uv_loop_init(ploops[i]);
        pClients[i] = new TCPClient(0x01,0x02,ploops[i]);
		pClients[i]->SetRecvCB(ReadCB,pClients[i]);
		if(!pClients[i]->Connect(serverip.c_str(),12345)) {
			fprintf(stdout,"connect error:%s\n",pClients[i]->GetLastErrMsg());
		}else{
			fprintf(stdout,"client(%p) connect succeed.\n",pClients[i]);
		}
		memset(senddata,0,sizeof(senddata));
		sprintf(senddata,"client(%p) call %d",pClients[i],++call_time);
		NetPacket packet;
		packet.header = 0x01;
		packet.tail = 0x02;
		packet.datalen = (std::min)(strlen(senddata),sizeof(senddata)-1);
		std::string str=PacketData(packet,(const unsigned char*)senddata);
		if(pClients[i]->Send(&str[0],str.length()) <=0) {
			fprintf(stdout,"(%p)send error.%s\n",pClients[i],pClients[i]->GetLastErrMsg());
		} else {
			//fprintf(stdout,"发送的数据为:\n");
   //         for (int i=0; i<str.length(); ++i) {
   //             fprintf(stdout,"%02X",(unsigned char)str[i]);
   //         }
   //         fprintf(stdout,"\n");
			fprintf(stdout,"send succeed:%s\n",senddata);
		}
	}
    //for (int i=0; i< clientsize; ++i) {
    //	uv_loop_delete(ploops[i]);
    //	delete pClients[i];
    //}
    while (1) {
        Sleep(10);
    }
    return 0;
}