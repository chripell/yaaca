CC=gcc
CFLAGS=$(shell pkg-config --cflags gtk+-2.0 libusb-1.0) -I. -g -O2 -Wall -D_LIN -pthread
LDFLAGS=SDK/libASICamera.a $(shell pkg-config --libs gtk+-2.0 libusb-1.0 libpng) -lstdc++ -lm -g -pthread

all: libasill.so

libasill.so: asill.c
	gcc $(CFLAGS) -fPIC -shared -o $@ asill.c $(shell pkg-config --libs libusb-1.0)

clean:
	rm *~ *.o *.so yaaca
