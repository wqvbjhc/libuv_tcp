#include <iostream>
#include <string>
#include "tcpsocket.h"
using namespace std;
using namespace uv;

std::string serverip;
int call_time = 0;
void ReadCB(const char* buf, int bufsize, void* userdata)
{
    char senddata[256];
    TCPClient *client = (TCPClient *)userdata;
    sprintf(senddata,"recv server data(%p,%d)",client,bufsize);
    fprintf(stdout,"%s\n",senddata);
    for (int i=0; i< bufsize; ++i) {
        fprintf(stdout,"%c",buf[i]);
    }
    fprintf(stdout,"\n");
}


static void thread_entry(void* arg)
{
    TCPClient *pClients = (TCPClient *)arg;
    if(!pClients->connect(serverip.c_str(),12345)) {
        fprintf(stdout,"connect error:%s\n",pClients->GetLastErrMsg());
    }
}

static void thread_entry_senddata(void* arg)
{
    TCPClient *pClients = (TCPClient *)arg;
    char senddata[256];
    memset(senddata,0,sizeof(senddata));
    sprintf(senddata,"client(%p) call %d",pClients,++call_time);
    if(pClients->send(senddata,strlen(senddata)) <=0) {
        fprintf(stdout,"(%p)send error.%s\n",pClients,pClients->GetLastErrMsg());
    } else {
        fprintf(stdout,"send succeed:%s\n",senddata);
    }
}
int main(int argc, char **argv)
{
	if (argc !=2 ) {
		fprintf(stdout,"usage: %s server_ip_address\neg.%s 192.168.1.1\n",argv[0],argv[0]);
		return 0;
	}
    const int clientsize = 10;
	uv_loop_t *ploops[clientsize];
	TCPClient *pClients[clientsize];
    uv_thread_t threads[clientsize];
	TCPClient::StartLog("log/");

    serverip = argv[1];
    int i=0, r;
    for (int i=0; i< clientsize; ++i) {
		ploops[i] = uv_loop_new();
		pClients[i] = new TCPClient(ploops[i]);

        pClients[i]->setrecvcb(ReadCB,pClients[i]);
        r = uv_thread_create(&threads[i], thread_entry, pClients[i]);
        if (r) {
            fprintf(stdout,"create %d error:%s\n",i,uv_err_name(r));
        }
    }
    //for (i = 0; i < clientsize; i++) {
    //    r = uv_thread_join(&threads[i]);
    //    if (r) {
    //        fprintf(stdout,"join %d error:%s\n",i,uv_err_name(r));
    //    }
    //}
    Sleep(2000);
    fprintf(stdout,"begin senddata threads\n");
    for (int i=0; i< clientsize; ++i) {
        pClients[i]->setrecvcb(ReadCB,pClients[i]);
        r = uv_thread_create(&threads[i], thread_entry_senddata, pClients[i]);
        if (r) {
            fprintf(stdout,"create %d error:%s\n",i,uv_err_name(r));
        }
    }
    for (i = 0; i < clientsize; i++) {
        r = uv_thread_join(&threads[i]);
        if (r) {
            fprintf(stdout,"join %d error:%s\n",i,uv_err_name(r));
        }
    }
    fprintf(stdout,"end senddata threads\n");

	//for (int i=0; i< clientsize; ++i) {
	//	uv_loop_delete(ploops[i]);
	//	delete pClients[i];
	//}
	while (1)
	{
	}
    return 0;
}