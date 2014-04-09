CC=gcc
CFLAGS=$(shell pkg-config --cflags gtk+-2.0 libusb-1.0) -I. -g -O2 -Wall -D_LIN -pthread
LDFLAGS=SDK/libASICamera.a $(shell pkg-config --libs gtk+-2.0 libusb-1.0) -lstdc++ -lm -g -pthread

all: yaaca libasill.so

yaaca: zwo.o yaaca.o zwoll.o asill.o
	gcc $(CFLAGS) -o $@ $^ $(LDFLAGS)

libasill.so: asill.c
	gcc $(CFLAGS) -fPIC -shared -o $@ asill.c $(shell pkg-config --libs libusb-1.0)

zwo.o: zwo.c yaaca.h

zwoll.o: zwoll.c yaaca.h asill.h

asill.o : asill.c asill.h

yaaca.o: yaaca.c yaaca.h

clean:
	rm *~ *.o yaaca

deb: yaaca
	./make_deb.sh yaaca "Astrocapture for ZWO ASI cams" "libgtk2.0-0 (>= 2.20.1-2), libusb-1.0-0 (>= 2:1.0.8-2)" yaaca,/usr/bin
