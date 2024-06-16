CC=gcc
CFLAGS=-s

all: FontTool MELTTIMTool tim2bmp.exe bmp2tim

FontTool: FontTool.c
	$(CC) $(CFLAGS) -O3 -o FontTool FontTool.c
	
MELTTIMTool: MELTTIMTool.c
	$(CC) $(CFLAGS) -O3 -o MELTTIMTool MELTTIMTool.c

tim2bmp.exe: tim2bmp.c
	$(CC) $(CFLAGS) -O2 -o tim2bmp.exe tim2bmp.c -static -LC:\zlib -lz -IC:\zlib

bmp2tim: bmp2tim.c
	$(CC) $(CFLAGS) -O2 -o bmp2tim bmp2tim.c

clean:
	rm -f FontTool MELTTIMTool tim2bmp.exe bmp2tim
