CC=gcc

all: tcptest
	$(RM) c
	ln -s tcptest c

clean:
	$(RM) tcptest tcptest.o c
