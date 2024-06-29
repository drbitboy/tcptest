#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/fcntl.h>
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
    struct sockaddr_in serv_addr_AF_INET;
    int maxfd = 0;
    int no_sends = 0;
    fd_set fds[3];
    fd_set* rfds = fds;
    fd_set* wfds = fds+1;
    fd_set* efds = fds+2;
    int opt_close = 0;
    int opt_no_send = 0;
    int opt_rotate = 0;
    int opt_debug = 0;
    int one = 1;

    noSIGPIPE();

    for (int iarg=0; iarg<argc; ++iarg)
    {
#       define ARGCMP(OPT, S) \
        OPT = !iarg ? 0 : (strcmp(argv[iarg],S) ? OPT : 1)
        ARGCMP(opt_close,   "--close"  );
        ARGCMP(opt_no_send, "--no-send");
        ARGCMP(opt_rotate,  "--rotate" );
        ARGCMP(opt_debug,   "--debug"  );
    }

    // Set up socket for listening
    if (0 > (listenfd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0)))
    {
      fprintf(stderr, "socket: %s\n", strerror(errno));
      return 1;
    }
    // - Allow that socket (addres/port) pair to have multiple servers
    //   - N.B. this allows server to start when the (address/port) pair
    //          is still in the TIME_WAIT state e.g. after another
    //          server crashed
    if (0 > ( setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,(char *) &one, sizeof one)))
    {
        fprintf(stderr, "setsockopt[reuseport]: %s\n", strerror(errno));
        close(listenfd);
    }
    if (0 > ( setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT,(char *) &one, sizeof one)))
    {
        fprintf(stderr, "setsockopt[reuseport]: %s\n", strerror(errno));
        close(listenfd);
    }

    // Bind listen socket to (addres/port) pair
    memset(&serv_addr_AF_INET, '0', sizeof serv_addr_AF_INET);
    serv_addr_AF_INET.sin_family = AF_INET;
    serv_addr_AF_INET.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr_AF_INET.sin_port = htons((short)SCPORT);
    bind(listenfd, (pSS)&serv_addr_AF_INET, sizeof serv_addr_AF_INET);

    // Start listening on this port
    listen(listenfd, 10);

    while(1)
    {
        struct timeval tv2s = {2, 0};
        struct timeval tv;
        int n;

        maxfd = 0;

        // Setup fd_set structures for select
        FD_ZERO(rfds); FD_ZERO(wfds); FD_ZERO(efds);
        // - listening socket:  read and exception
        FD_SET(listenfd, rfds);
        FD_SET(listenfd, efds);
        maxfd = listenfd > maxfd ? listenfd : maxfd;
        if (connfd > -1)
        {
            // - accepted socket:  write and exception
            FD_SET(connfd, wfds);
            FD_SET(connfd, efds);
            maxfd = connfd > maxfd ? connfd : maxfd;
        }

        if (opt_debug)
        {
            fprintf(stderr, "%16lx %16lx %16lx before\n", *((long*)rfds), *((long*)wfds), *((long*)efds));
        }

        // Issue select; ignore signal interruptions
        tv = tv2s;
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

        if (opt_debug)
        {
            fprintf(stderr, "%16lx %16lx %16lx after\n", *((long*)rfds), *((long*)wfds), *((long*)efds));
        }

        // Process select results

        if (connfd > -1)
        {
            // Check accepted socket, if present
            if (FD_ISSET(connfd,efds))
            {
                // - accepted socket exception; something should happen
                fprintf(stderr, "EC%d\n", connfd);
            }
            if (FD_ISSET(connfd,wfds))
            {
                if (!opt_close && !opt_no_send)
                {
                    // - ready to write
                    char sendBuff[1025];
                    time_t ticks;
                    ssize_t nw;
                    // - put time into a string
                    ticks = time(NULL);
                    snprintf(sendBuff, sizeof sendBuff, "%.24s\n", ctime(&ticks));
                    errno = 0;
                    // - write time string to socket if netther
                    //   --close nor --no-send options were specified
                    fprintf(stderr, "W%ld/%ld\n", strlen(sendBuff), nw=write(connfd, sendBuff, strlen(sendBuff)));
                    if (nw < 0)
                    {
                        fprintf(stderr, "Closing fd %d[%s]\n", connfd, strerror(errno));
                        close(connfd);
                        connfd = -1;
                        if (opt_rotate)
                        {
                            opt_close = 1;
                            opt_no_send = 0;
                        }
                    }
                    else
                    {
                        char at = '@';
                        fprintf(stderr, "W%ld/%ld\n", 1L, write(connfd, &at, 1));
                    }
                }
                else if (opt_close)
                {
                    // Close FD without writing anything
                    fprintf(stderr, "Closing, without writing, fd %d[%s]\n", connfd, "because of --close/--rotate argument");
                    close(connfd);
                    connfd = -1;
                    if (opt_rotate)
                    {
                        opt_close = 0;
                        opt_no_send = 1;
                    }
                }
                else if (opt_no_send)
                {
                    // Close eventually, without writing anything,
                    if (5 > ++no_sends)
                    {
                       fprintf(stderr, "Writeable fd %d[%s]\n", connfd, "doing nothing because of --no-send/--rotate argument");
                    }
                    else
                    {
                        fprintf(stderr, "Writeable fd %d[closing after %d no-sends]\n", connfd, no_sends);
                        close(connfd);
                        connfd = -1;
                        no_sends = 0;
                        if (opt_rotate)
                        {
                            opt_close = 0;
                            opt_no_send = 0;
                        }
                    }
                }
            } // if (FD_ISSET(connfd,wfds))
        } // if (connfd > -1)

        if (FD_ISSET(listenfd,efds))
        {
            // listen socket exception; something should happen
            fprintf(stderr, "EL%d\n", listenfd);
        }

        if (connfd == -1 && FD_ISSET(listenfd,rfds))
        {
            // listen socket accept new connection, only after previous
            // accepted socket has closed
            connfd = accept(listenfd, (pSS)NULL, NULL);
            fprintf(stderr, "L%d\n", connfd);
        }
        // Delay for balance of time in select
        if (tv.tv_sec || tv.tv_usec) { select(0, NULL, NULL, NULL, &tv); }
     }
}


int client(int argc, char *argv[])
{
    int sockfd = 0;
    int n = 0;
    int selreturn = 0;
    char recvBuff[1024];
    struct sockaddr_in serv_addr_AF_INET;
    int maxfd = 0;

    struct timeval tv5s = {5, 0};
    struct timeval tv;

    fd_set fds[3];
    fd_set* rfds = fds;
    fd_set* wfds = fds+1;
    fd_set* efds = fds+2;

    if(argc != 2)
    {
        fprintf(stderr, "\n Usage: %s <ip of server> \n",argv[0]);
        return 1;
    }

    memset(recvBuff, '0',sizeof(recvBuff));
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        fprintf(stderr, "ERROR:  Could not create socket\n");
        return 1;
    }

    memset(&serv_addr_AF_INET, '0', sizeof(serv_addr_AF_INET));

    serv_addr_AF_INET.sin_family = AF_INET;
    serv_addr_AF_INET.sin_port = htons((short)SCPORT);

    if(inet_pton(AF_INET, argv[1], &serv_addr_AF_INET.sin_addr)<=0)
    {
        fprintf(stderr, "ERROR: inet_pton error occured\n");
        return 1;
    }

    if( connect(sockfd, (pSS)&serv_addr_AF_INET, sizeof(serv_addr_AF_INET)) < 0)
    {
       fprintf(stderr, "ERROR:  Connect Failed\n");
       return 2;
    }

    if (0 > fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK))
    {
       fprintf(stderr, "ERROR:  fcntl(%d, F_GETFL, ...|O_NONBLOCK) Failed[%s]\n", sockfd, strerror(errno));
       return 3;
    }

    FD_ZERO(rfds); FD_ZERO(wfds); FD_ZERO(efds);
    FD_SET(sockfd, rfds);

    errno = 0;

    // Wait up to 5s for select, continue wait on signals
    tv = tv5s;
    while (0 > (selreturn=select(sockfd+1, rfds, wfds, efds, &tv)))
    {
        if (errno != EINTR) { perror("Non-EINTR error in select"); return -1; }
        fprintf(stderr,"%s\n", "I");
        errno = 0;
    }

    errno = 0;
    if (!selreturn)
    {
        fprintf(stderr, "Select timed out[%s]; closing sockfd[%d] at client end and waiting 9s to exit\n", strerror(errno), sockfd);
        tv.tv_sec = 9;
        select(0, NULL, NULL, NULL, &tv);
    }
    else if ( (n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
    {
        char c1;
        int errno0;
        int errno1;
        int n1;
        errno0 = errno;
        errno = 0;
        n1 = read(sockfd, &c1, 1);
        fprintf(stderr, "R[%d]=%d[%s]\n", sockfd, n, strerror(errno0));
        errno1 = errno;
        // Write received data
        recvBuff[n] = 0;
        errno = 0;
        if(fputs(recvBuff, stdout) == EOF)
        {
            fprintf(stderr, "Error:  fputs error\n");
        }
        fprintf(stderr, "R[%d]=%d[%s]'0x%02x'\n", sockfd, n1, strerror(errno1), (int)c1);
    }
    else if(n < 0)
    {
        // Log read error
        fprintf(stderr, "Read error[%s]\n", strerror(errno));
    }
    else
    {
        // n is 0 because server closed socket
        fprintf(stderr, "Remote connection read[%s] detected server closed socket; closing socket[%d] at client end\n", strerror(errno), sockfd);
    }

    // Cleanup and exit
    close(sockfd);
    return 0;
}

int main(int arg, char *argv[])
{
    char *pbn = argv[0] + strlen(argv[0]);
    char *pbn0 = pbn;
    while (pbn >= argv[0] &&*pbn != '/') { pbn0 = pbn--; }

#   define IS_CLIENT ((*pbn0)=='c')
    fprintf(stderr,"Starting %s...\n", IS_CLIENT ? "client" : "server");
    if (IS_CLIENT) { return client(arg,argv); }
    return server(arg, argv);
}
