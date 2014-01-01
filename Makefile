CC=gcc
CFLAGS=$(shell pkg-config --cflags gtk+-2.0) -I. -g -O2 -Wall
LDFLAGS=$(shell pkg-config --libs gtk+-2.0 libusb-1.0) -L./SDK/lib -lASICamera -lm -g

yaaca: zwo.o yaaca.o
	gcc $(CFLAGS) $(LDFLAGS) -o $@ $^

zwo.o: zwo.c yaaca.h

yaaca.o: yaaca.c yaaca.h

clean:
	rm *~ *.o yaaca

