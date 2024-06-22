#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct sockaddr* pSS;
#define SCPORT 5000

static void noSIGPIPE()
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGPIPE, &sa, NULL);
}

int server(int argc, char *argv[])
{
    int listenfd = 0;
    int connfd = -1;
    struct sockaddr_in serv_addr;
    int maxfd = 0;
    fd_set fds[3];
    fd_set* rfds = fds;
    fd_set* wfds = fds+1;
    fd_set* efds = fds+2;

    noSIGPIPE();

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof serv_addr);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons((short)SCPORT);

    bind(listenfd, (pSS)&serv_addr, sizeof(serv_addr));

    listen(listenfd, 10);

    while(1)
    {
        struct timeval tv3s = {3, 0};
        struct timeval tv;
        int n;
        maxfd = 0;
        FD_ZERO(rfds); FD_ZERO(wfds); FD_ZERO(efds);
        FD_SET(listenfd, rfds);
        FD_SET(listenfd, efds);
        maxfd = listenfd > maxfd ? listenfd : maxfd;
        if (connfd > -1)
        {
            FD_SET(connfd, wfds);
            FD_SET(connfd, efds);
            maxfd = connfd > maxfd ? connfd : maxfd;
        }
        fprintf(stderr, "%16lx %16lx %16lx before\n", *((long*)rfds), *((long*)wfds), *((long*)efds));

        tv = tv3s;
        errno = 0;
        while (0 > (n=select(maxfd+1, rfds, wfds, efds, &tv)))
        {
            if (errno != EINTR) { perror("Non-EINTR error in select"); return -1; }
            fprintf(stderr,"%s\n", "I");
            errno = 0;
        }
        errno = 0;
        fprintf(stderr,"S%d/%d/%lu/%lu\n", n, connfd, tv.tv_sec, tv.tv_usec);
        if (n < 0) { fprintf(stderr, "S/%s\n", strerror(errno)); }
        if (tv.tv_sec || tv.tv_usec) { select(0, NULL, NULL, NULL, &tv); }
        fprintf(stderr, "%16lx %16lx %16lx after\n", *((long*)rfds), *((long*)wfds), *((long*)efds));

        if (connfd > -1)
        {
            if (FD_ISSET(connfd,efds))
            {
                fprintf(stderr, "EC%d\n", connfd);
            }
            if (FD_ISSET(connfd,wfds))
            {
                char sendBuff[1025];
                time_t ticks;
                ssize_t nw;
                ticks = time(NULL);
                snprintf(sendBuff, sizeof sendBuff, "%.24s\n", ctime(&ticks));
                errno = 0;
                if (argc<2 || (strcmp(argv[1],"--close") && strcmp(argv[1],"--no-send")))
                {
                    fprintf(stderr, "W%ld\n", nw=write(connfd, sendBuff, strlen(sendBuff)));
                    if (nw < 0)
                    {
                        fprintf(stderr, "Closing fd %d[%s]\n", connfd, strerror(errno));
                        close(connfd);
                        connfd = -1;
                    }
                }
                else if (!strcmp(argv[1],"--close"))
                {
                    // Close FD without writing anything
                    fprintf(stderr, "Closing, without writing, fd %d[%s]\n", connfd, "because of --close argument");
                    close(connfd);
                    connfd = -1;
                }
                else if (!strcmp(argv[1],"--no-send"))
                {
                    fprintf(stderr, "Writeable fd %d[%s]\n", connfd, "doing nothing because of --no-send argument");
                    *sendBuff = '\0';
                    fprintf(stderr, "W%ld\n", nw=write(connfd, sendBuff, 1));
                }
            }
        }

        if (FD_ISSET(connfd,efds))
        {
            fprintf(stderr, "EL%d\n", listenfd);
        }

        if (FD_ISSET(listenfd,rfds))
        {
            connfd = accept(listenfd, (pSS)NULL, NULL);
            fprintf(stderr, "L%d\n", connfd);
        }
     }
}


int client(int argc, char *argv[])
{
    int sockfd = 0, n = 0;
    char recvBuff[1024];
    struct sockaddr_in serv_addr;
    int maxfd = 0;

    struct timeval tv5s = {5, 0};
    struct timeval tv;

    fd_set fds[3];
    fd_set* rfds = fds;
    fd_set* wfds = fds+1;
    fd_set* efds = fds+2;

    if(argc != 2)
    {
        printf("\n Usage: %s <ip of server> \n",argv[0]);
        return 1;
    }

    memset(recvBuff, '0',sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error : Could not create socket \n");
        return 1;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((short)SCPORT);

    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<=0)
    {
        printf("\n inet_pton error occured\n");
        return 1;
    }

    if( connect(sockfd, (pSS)&serv_addr, sizeof(serv_addr)) < 0)
    {
       printf("\n Error : Connect Failed \n");
       return 1;
    }

    FD_ZERO(rfds); FD_ZERO(wfds); FD_ZERO(efds);
    FD_SET(sockfd, rfds);
    FD_SET(sockfd, efds);

    tv = tv5s;
    errno = 0;
    while (0 > (n=select(sockfd+1, rfds, wfds, efds, &tv)))
    {
        if (errno != EINTR) { perror("Non-EINTR error in select"); return -1; }
        fprintf(stderr,"%s\n", "I");
        errno = 0;
    }
    if (!n)
    {
        fprintf(stderr, "Select timed out[%s]; closing sockfd[%d]\n", strerror(errno), sockfd);
    }
    else
    {
        errno = 0;
        while ( (n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
        {
            recvBuff[n] = 0;
            if(fputs(recvBuff, stdout) == EOF)
            {
                printf("\n Error : Fputs error\n");
            }
            break;
        }

        if(n < 0)
        {
            fprintf(stderr, "Read error[%s]\n", strerror(errno));
        }
        else if(!n)
        {
            fprintf(stderr, "Remote connection closed during read[%s]; closing socket[%d]\n", strerror(errno),sockfd);
        }
    }

    close(sockfd);

    return 0;
}

int main(int arg, char *argv[])
{
    char *pbn = argv[0] + strlen(argv[0]);
    char *pbn0 = pbn;
    while (pbn >= argv[0] &&*pbn != '/') { pbn0 = pbn--; }

#   define IS_CLIENT ((*pbn0)=='c')
    fprintf(stderr,"Calling %s...\n", IS_CLIENT ? "client" : "server");
    if (IS_CLIENT) { return client(arg,argv); }
    return server(arg, argv);
}
