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
    int opt_debug = 0;
    int one = 1;

    noSIGPIPE();

    for (int iarg=0; iarg<argc; ++iarg)
    {
#       define ARGCMP(OPT, S) \
        OPT = !iarg ? 0 : (strcmp(argv[iarg],S) ? OPT : 1)
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
        char recvBuff[256];

        maxfd = 0;

        // Setup fd_set structures for select
        FD_ZERO(rfds);
        // - listening socket:  read and exception
        FD_SET(listenfd, rfds);
        maxfd = listenfd > maxfd ? listenfd : maxfd;
        if (connfd > -1)
        {
            // - accepted socket:  write and exception
            FD_SET(connfd, rfds);
            maxfd = connfd > maxfd ? connfd : maxfd;
        }

        if (opt_debug)
        {
            fprintf(stderr, "%16lx before\n", *((long*)rfds));
        }

        // Issue select; ignore signal interruptions
        tv = tv2s;
        errno = 0;
        while (0 > (n=select(maxfd+1, rfds, NULL, NULL, &tv)))
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
            fprintf(stderr, "%16lx after\n", *((long*)rfds));
        }

        // Process select results

        if (connfd > -1)
        {
            while ( (n = read(connfd, recvBuff, sizeof(recvBuff))) > 0)
            {
                fprintf(stderr, "R[%d]=%d[%s]\n", connfd, n, strerror(errno));
                errno = 0;
                // Write received data
                for (char* p=recvBuff; p<(recvBuff+n); ++p)
                {
                    if (31 < ((int)*p) && ((int)*p) < 128)
                    {
                        fprintf(stderr, "%c", *p);
                    }
                    else
                    {
                        fprintf(stderr, "<0x%02x>", 255 & (int)*p);
                    }
                }
                fprintf(stderr, "%c", '\n');
            }
            if (n==0 || (EWOULDBLOCK!=errno && EAGAIN!=errno && errno))
            {
                close(connfd);
                connfd = -1;
            }
        } // if (connfd > -1)

        if (connfd == -1 && FD_ISSET(listenfd,rfds))
        {
            // listen socket accept new connection, only after previous
            // accepted socket has closed
            connfd = accept(listenfd, (pSS)NULL, NULL);
            fprintf(stderr, "L%d\n", connfd);
            if (0 > fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK))
            {
               fprintf(stderr, "ERROR:  fcntl(%d, F_GETFL, ...|O_NONBLOCK) Failed[%s]\n", connfd, strerror(errno));
               return 3;
            }

        }
        // Delay for balance of time in select
        if (tv.tv_sec || tv.tv_usec) { select(0, NULL, NULL, NULL, &tv); }
     }
}


int main(int arg, char *argv[])
{
    char *pbn = argv[0] + strlen(argv[0]);
    char *pbn0 = pbn;
    while (pbn >= argv[0] &&*pbn != '/') { pbn0 = pbn--; }

#   define IS_CLIENT ((*pbn0)=='c')
    fprintf(stderr,"Starting %s...\n", IS_CLIENT ? "client" : "server");
    //if (IS_CLIENT) { return client(arg,argv); }
    return server(arg, argv);
}
