#!/usr/bin/env sh

XKAAPIDIR=/home/texane/install

gcc -Wall -O3 -I$XKAAPIDIR/include -I. ../src/main.c -lgsl -lblas -L$XKAAPIDIR/lib -lxkaapi -lpthread
