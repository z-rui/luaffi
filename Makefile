CC=cc
CFLAGS=-Wall -O2 -fPIC
#CFLAGS+=pkg-config --cflags libffi
LIBS=-lffi

ffi.so: ffi.o
	$(CC) -shared -o $@ $(LDFLAGS) $< $(LIBS)
