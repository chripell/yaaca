CC=gcc
CFLAGS=$(shell pkg-config --cflags gtk+-2.0) -I. -g -O2 -Wall -D_LIN
LDFLAGS=$(shell pkg-config --libs gtk+-2.0 libusb-1.0) SDK/libASICamera.a -lstdc++ -lm -g

yaaca: zwo.o yaaca.o
	gcc $(CFLAGS) -o $@ $^ $(LDFLAGS)

zwo.o: zwo.c yaaca.h

yaaca.o: yaaca.c yaaca.h

clean:
	rm *~ *.o yaaca

