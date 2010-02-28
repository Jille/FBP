CFLAGS=-g -I../common -I/sw/include/libmd -Wall -DVERBOSE
LDFLAGS=-L/sw/lib -lm -lmd

fbpd: fbpd.c ../common/fbp.h ../common/sha1.o Makefile
	cc $(LDFLAGS) -o fbpd $(CFLAGS) fbpd.c ../common/sha1.o

../common/sha1.o: ../common/sha1.c
	make -C ../common sha1.o
