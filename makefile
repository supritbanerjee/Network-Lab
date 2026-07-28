CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=199309L

all: sender receiver

sender: sender.c common.c common.h
	$(CC) $(CFLAGS) -o sender sender.c common.c

receiver: receiver.c common.c common.h
	$(CC) $(CFLAGS) -o receiver receiver.c common.c

clean:
	rm -f sender receiver results.csv
