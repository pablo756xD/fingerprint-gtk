#! /bin/bash
libtool --mode=link gcc -o main main.c -I~/libfprint/libfprint -L~/libfprint/libfprint -lfprint $(pkg-config --cflags --libs gtk+-3.0)

