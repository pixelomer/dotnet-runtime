#include <switch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#ifndef SOCKET_EFFICIENCY
#define SOCKET_EFFICIENCY 4
#endif
u32 __nx_applet_exit_mode = 1;
static FILE *logfile;
static int round_number, checks;
static unsigned char expected[16384], actual[16384];
#define REQUIRE(expr) do { if (!(expr)) { fprintf(logfile,"FAIL round=%d line=%d errno=%d expression=%s\n",round_number,__LINE__,errno,#expr); return 1; } checks++; } while(0)
static struct sockaddr_in loopback(void)
{
    struct sockaddr_in addr = {0};
    addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    return addr;
}
static int create_socket(int type)
{
    int fd=socket(AF_INET,type,0);
    if(fd<0) return -1;
    struct timeval timeout={2,0};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) ||
       setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))) { close(fd); return -1; }
    return fd;
}
static int transfer(int from,int to)
{
    size_t n=0;
    while(n<sizeof(expected)) { ssize_t k=send(from,expected+n,sizeof(expected)-n,0); REQUIRE(k>0); n+=(size_t)k; }
    n=0;
    while(n<sizeof(actual)) { ssize_t k=recv(to,actual+n,sizeof(actual)-n,0); REQUIRE(k>0); n+=(size_t)k; }
    REQUIRE(memcmp(expected,actual,sizeof(actual))==0);
    return 0;
}
static int tcp_round(void)
{
    int listener=create_socket(SOCK_STREAM); REQUIRE(listener>=0);
    struct sockaddr_in addr=loopback(); socklen_t len=sizeof(addr);
    REQUIRE(bind(listener,(struct sockaddr*)&addr,sizeof(addr))==0);
    REQUIRE(listen(listener,1)==0);
    REQUIRE(getsockname(listener,(struct sockaddr*)&addr,&len)==0);
    REQUIRE(addr.sin_addr.s_addr==htonl(INADDR_LOOPBACK) && addr.sin_port!=0);
    int client=create_socket(SOCK_STREAM); REQUIRE(client>=0);
    int one=1; REQUIRE(setsockopt(client,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one))==0);
    fprintf(logfile,"CONNECT round=%d listener=%d client=%d port=%u\n",round_number,listener,client,ntohs(addr.sin_port));
    REQUIRE(connect(client,(struct sockaddr*)&addr,len)==0);
    int server=accept(listener,NULL,NULL); REQUIRE(server>=0);
    struct timeval timeout={2,0};
    REQUIRE(setsockopt(server,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    REQUIRE(setsockopt(server,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))==0);
    REQUIRE(transfer(client,server)==0); REQUIRE(transfer(server,client)==0);
    REQUIRE(shutdown(client,SHUT_WR)==0); REQUIRE(recv(server,actual,1,0)==0);
    REQUIRE(close(server)==0); REQUIRE(close(client)==0); REQUIRE(close(listener)==0);
    return 0;
}
static int udp_round(void)
{
    int receiver=create_socket(SOCK_DGRAM), sender=create_socket(SOCK_DGRAM);
    REQUIRE(receiver>=0 && sender>=0);
    struct sockaddr_in a=loopback(),b=loopback(),from; socklen_t len=sizeof(a);
    REQUIRE(bind(receiver,(struct sockaddr*)&a,sizeof(a))==0);
    REQUIRE(bind(sender,(struct sockaddr*)&b,sizeof(b))==0);
    REQUIRE(getsockname(receiver,(struct sockaddr*)&a,&len)==0);
    len=sizeof(b); REQUIRE(getsockname(sender,(struct sockaddr*)&b,&len)==0);
    REQUIRE(sendto(sender,expected,1024,0,(struct sockaddr*)&a,sizeof(a))==1024);
    len=sizeof(from); REQUIRE(recvfrom(receiver,actual,sizeof(actual),0,(struct sockaddr*)&from,&len)==1024);
    REQUIRE(memcmp(actual,expected,1024)==0 && from.sin_port==b.sin_port && from.sin_addr.s_addr==b.sin_addr.s_addr);
    REQUIRE(sendto(receiver,actual,1024,0,(struct sockaddr*)&from,len)==1024);
    memset(actual,0,sizeof(actual)); len=sizeof(from);
    REQUIRE(recvfrom(sender,actual,sizeof(actual),0,(struct sockaddr*)&from,&len)==1024);
    REQUIRE(memcmp(actual,expected,1024)==0 && from.sin_port==a.sin_port && from.sin_addr.s_addr==a.sin_addr.s_addr);
    REQUIRE(close(sender)==0); REQUIRE(close(receiver)==0);
    return 0;
}
int main(void)
{
    logfile=fopen("sdmc:/switch/native-networking-control.txt","w");
    if(!logfile) return 1;
    setvbuf(logfile,NULL,_IONBF,0);
    SocketInitConfig config=*socketGetDefaultInitConfig();
    config.sb_efficiency=SOCKET_EFFICIENCY;
    Result init=socketInitialize(&config);
    fprintf(logfile,"BEGIN plain libnx TCP+UDP init=%u efficiency=%u\n",init,config.sb_efficiency);
    int result=R_FAILED(init);
    for(round_number=0;!result && round_number<32;round_number++) {
        for(size_t i=0;i<sizeof(expected);i++) expected[i]=(unsigned char)(i*37+round_number);
        result=tcp_round(); if(!result) result=udp_round();
        fprintf(logfile,"ROUND round=%d checks=%d result=%d native_used=%zu\n",round_number,checks,result,mallinfo().uordblks);
    }
    fprintf(logfile,"END result=%d\n",result); fclose(logfile);
    // Match the managed probe's process-lifetime BSD initialization.
    return result;
}
