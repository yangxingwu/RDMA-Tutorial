CC=gcc
CFLAGS=-Wall -Werror -std=c99
LIBS=-libverbs

DEP_SRCS=ib.c

CLIENT_SRCS=client.c $(DEP_SRCS)
CLIENT_OBJS=$(CLIENT_SRCS:.c=.o)

SERVER_SRCS=server.c $(DEP_SRCS)
SERVER_OBJS=$(SERVER_SRCS:.c=.o)

PROG=server client

all: $(PROG)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJS) $(LIBS)

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS) $(LIBS)

clean:
	$(RM) *.o $(PROG)
