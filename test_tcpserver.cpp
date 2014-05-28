#include <iostream>
#include <string>
#include "tcpsocket.h"
#include "mswin_special/sys/DumpFile.h"
using namespace std;
using namespace uv;

TCPServer server(0x01,0x02);

void ReadCB(int cliendid, const NetPacket& packet, const unsigned char* buf,void * userdata)
{
    static char senddata[256];
    sprintf(senddata,"****recv client %d(%d)",cliendid,packet.datalen);
	fprintf(stdout,"%s\n",senddata);
	//for (int i=0; i< packet.datalen; ++i) {
	//	fprintf(stdout,"%c",buf[i]);
	//}
	//fprintf(stdout,"]\n");
	NetPacket tmppack = packet;
	tmppack.datalen = (std::min)(strlen(senddata),sizeof(senddata)-1);
	std::string retstr = PacketData(tmppack,(const unsigned char*)senddata);
    if(server.Send(cliendid,&retstr[0],retstr.length()) <=0) {
        fprintf(stdout,"send error.%s\n",server.GetLastErrMsg());
    }
} 

void NewConnect(int clientid)
{
    fprintf(stdout,"new connect:%d\n",clientid);
    server.SetRecvCB(clientid,ReadCB,NULL);
}

int main(int argc, char** argv)
{
    if (argc !=2 ) {
        fprintf(stdout,"usage: %s server_ip_address\neg.%s 192.168.1.1\n",argv[0],argv[0]);
        return 0;
    }
	DeclareDumpFile();
	TCPServer::StartLog("log/");
    server.SetNewConnectCB(NewConnect);
    if(!server.Start(argv[1],12345)) {
        fprintf(stdout,"Start Server error:%s\n",server.GetLastErrMsg());
    }
	fprintf(stdout,"server return on main.\n");
    return 0;
}