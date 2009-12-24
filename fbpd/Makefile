CFLAGS=-g -I../common -I/sw/include/libmd -Wall
LDFLAGS=-L/sw/lib -lm -lmd

fbpd: fbpd.c ../common/fbp.h Makefile
	cc $(LDFLAGS) -o fbpd $(CFLAGS) fbpd.c
