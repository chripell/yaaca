
ARCH := $(shell getconf LONG_BIT)

ASILIB_64=lib/x64/libASICamera2.a
ASILIB_32=lib/x86/libASICamera2.a

INSTALL_LIST=$(shell cat install_list | tr '\n' ' ')

all: astrolove/libyaaca.so.1 recorder astrolove/dcraw-astrolove

astrolove/libyaaca.so.1: yaaca_server.c yaaca_server.h
	gcc -ggdb -pthread -D_GNU_SOURCE -O2 -std=c99 -Iinclude -I jsmn -fPIC -g -c -Wall \
		-DSELF_BULK yaaca_server.c jsmn/jsmn.c
	g++ -ggdb -pthread -shared -Wl,-soname,libyaaca.so.1 -o astrolove/libyaaca.so.1.0.1 \
		yaaca_server.o jsmn.o ${ASILIB_${ARCH}} -lusb-1.0 -lc
	cd astrolove && ln -sf libyaaca.so.1.0.1 libyaaca.so.1

recorder: recorder.c
	gcc -ggdb -pthread -D_GNU_SOURCE -std=c99 -Iinclude -g -c -Wall recorder.c
	g++ -ggdb -pthread recorder.o ${ASILIB_${ARCH}} -lusb-1.0 -lc -o recorder

recorder-libusb: recorder.c
	gcc -ggdb -pthread -DSELF_BULK -D_GNU_SOURCE -std=c99 -Iinclude -g -c -Wall recorder.c
	g++ -ggdb -pthread recorder.o ${ASILIB_${ARCH}} ./libusb-1.0.20/libusb/.libs/libusb-1.0.a -ludev -lc -o recorder-libusb

astrolove/dcraw-astrolove:
	gcc -O4 -o astrolove/dcraw-astrolove astrolove/dcraw.c -lm -DNODEPS

clean:
	rm -f *~ *.so.* *.o *.pyc recorder recorder-libusb astrolove/*.so.* astrolove/*.pyc

deb: all
	./make_deb.sh yaaca "Astrocapture for ZWO ASI cams" "libgtk-3-0 (>= 3.10.8), libusb-1.0-0 (>= 2:1.0.17), python (>= 2.7.5), python-gi (>= 3.12.0), python-gi-cairo (>= 3.12.0), python-numpy (>= 1:1.8.2), python-scipy (>= 0.13.3)" ${INSTALL_LIST}

TAGS:
	(find . -type f -iname "*.[ch]"; find . -type f -iname "*.py") | etags -
