#! /bin/bash
libtool --mode=link gcc -o main main.c -I/home/pablo/libfprint/libfprint -L/home/pablo/libfprint/libfprint -lfprint $(pkg-config --cflags --libs gtk+-3.0)

