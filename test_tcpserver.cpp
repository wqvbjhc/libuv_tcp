#include <iostream>
#include <string>
#include "tcpsocket.h"
#include "mswin_special/sys/DumpFile.h"
using namespace std;
using namespace uv;

TCPServer server;

void ReadCB(int cliendid, const char* buf, int bufsize)
{
    static char senddata[256];
    sprintf(senddata,"recv client %d(%d)",cliendid,bufsize);
    fprintf(stdout,"%s\n",senddata);
    for (int i=0; i< bufsize; ++i) {
        fprintf(stdout,"%c",buf[i]);
    }
    fprintf(stdout,"\n");
    if(server.send(cliendid,senddata,strlen(senddata)) <=0) {
        fprintf(stdout,"send error.%s\n",server.GetLastErrMsg());
    }
} 

void NewConnect(int clientid)
{
    fprintf(stdout,"new connect:%d\n",clientid);
    server.setrecvcb(clientid,ReadCB);
}

int main(int argc, char** argv)
{
    if (argc !=2 ) {
        fprintf(stdout,"usage: %s server_ip_address\neg.%s 192.168.1.1\n",argv[0],argv[0]);
        return 0;
    }
	DeclareDumpFile();
	TCPServer::StartLog("log/");
    server.setnewconnectcb(NewConnect);
    if(!server.Start(argv[1],12345)) {
        fprintf(stdout,"Start Server error:%s\n",server.GetLastErrMsg());
    }
	fprintf(stdout,"server return on main.\n");
    return 0;
}