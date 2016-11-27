
ARCH := $(shell getconf LONG_BIT)

ASILIB_64=lib/x64/libASICamera2.a
ASILIB_32=lib/x86/libASICamera2.a

all: libyaaca.so.1 recorder

libyaaca.so.1: yaaca_server.c yaaca_server.h sb.o
	gcc -ggdb -pthread -D_GNU_SOURCE -O2 -std=c99 -Iinclude -I jsmn -fPIC -g -c -Wall \
		-DSELF_BULK yaaca_server.c jsmn/jsmn.c
	g++ -ggdb -pthread -shared -Wl,-soname,libyaaca.so.1 -o libyaaca.so.1.0.1 \
		yaaca_server.o jsmn.o ${ASILIB_${ARCH}} -lusb-1.0 -lc
	ln -sf libyaaca.so.1.0.1 libyaaca.so.1

recorder: recorder.c
	gcc -ggdb -pthread -D_GNU_SOURCE -std=c99 -Iinclude -g -c -Wall recorder.c
	g++ -ggdb -pthread recorder.o ${ASILIB_${ARCH}} -lusb-1.0 -lc -o recorder

recorder-libusb: recorder.c sb.o
	gcc -ggdb -pthread -DSELF_BULK -D_GNU_SOURCE -std=c99 -Iinclude -g -c -Wall recorder.c
	g++ -ggdb -pthread sb.o recorder.o ${ASILIB_${ARCH}} ./libusb-1.0.20/libusb/.libs/libusb-1.0.a -ludev -lc -o recorder-libusb

sb.o: sb.c
	gcc -ggdb -pthread -DSELF_BULK -D_GNU_SOURCE -std=c99 -Ilibusb-1.0.20/libusb -g -c -Wall sb.c

clean:
	rm -f *~ *.so.* *.o *.pyc recorder recorder-libusb


