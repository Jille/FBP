CFLAGS=-g -I../common -I/sw/include/libmd -Wall
LDFLAGS=-L/sw/lib -lm -lmd

fbpc: fbpc.c ../common/fbp.h Makefile
	cc $(LDFLAGS) -o fbpc $(CFLAGS) fbpc.c
