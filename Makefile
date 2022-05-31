CC=gcc

default:
	$(CC) -ljpeg -o jtoa jtoa.c
install:
	$(CC) -ljpeg -o /usr/local/bin/jtoa jtoa.c
uninstall:
	rm /usr/local/bin/jtoa
