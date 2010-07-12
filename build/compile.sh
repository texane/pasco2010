#!/usr/bin/env sh

XKAAPIDIR=$HOME/install/xkaapi_master

gcc -std=c99 -Wall -O3 -I$XKAAPIDIR/include -I. ../src/main.c -lgsl -lblas -L$XKAAPIDIR/lib -lxkaapi -lpthread
