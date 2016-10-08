
ARCH := $(shell getconf LONG_BIT)

ASILIB_64=lib/x64/libASICamera2.a
ASILIB_32=lib/x86/libASICamera2.a

libyaaca.so.1: yaaca_server.c yaaca_server.h
	gcc -pthread -D_GNU_SOURCE -O2 -std=c99 -Iinclude -I jsmn -fPIC -g -c -Wall \
		yaaca_server.c jsmn/jsmn.c
	g++ -pthread -shared -Wl,-soname,libyaaca.so.1 -o libyaaca.so.1.0.1 \
		yaaca_server.o jsmn.o ${ASILIB_${ARCH}} -lusb-1.0 -lc
	ln -sf libyaaca.so.1.0.1 libyaaca.so.1

clean:
	rm -f *~ *.so.* *.o *.pyc

