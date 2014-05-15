#include <iostream>
#include <string>
#include "tcpsocket.h"
#include "mswin_special/sys/DumpFile.h"
using namespace std;
using namespace uv;

std::string serverip;
int call_time = 0;
void ReadCB(const char* buf, int bufsize, void* userdata)
{
    char senddata[256]={0};
    TCPClient *client = (TCPClient *)userdata;
    sprintf(senddata,"recv server data(%p,%d)",client,bufsize);
    fprintf(stdout,"%s\n",senddata);
    for (int i=0; i< bufsize; ++i) {
        fprintf(stdout,"%c",buf[i]);
    }
    fprintf(stdout,"\n");
	if(client->send(senddata,(std::min)(strlen(senddata),sizeof(senddata)-1)) <=0) {
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
        pClients[i] = new TCPClient(ploops[i]);
		pClients[i]->setrecvcb(ReadCB,pClients[i]);
		if(!pClients[i]->connect(serverip.c_str(),12345)) {
			fprintf(stdout,"connect error:%s\n",pClients[i]->GetLastErrMsg());
		}else{
			fprintf(stdout,"client(%p) connect succeed.\n",pClients[i]);
		}
		memset(senddata,0,sizeof(senddata));
		sprintf(senddata,"client(%p) call %d",pClients[i],++call_time);
		if(pClients[i]->send(senddata,(std::min)(strlen(senddata),sizeof(senddata)-1)) <=0) {
			fprintf(stdout,"(%p)send error.%s\n",pClients[i],pClients[i]->GetLastErrMsg());
		} else {
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