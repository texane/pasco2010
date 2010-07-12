#!/usr/bin/env sh

XKAAPIDIR=$HOME/install/xkaapi_master
GMPDIR=$HOME/install

gcc -std=c99 -Wall -O3 \
    -I$GMPDIR/include -I$XKAAPIDIR/include -I. \
    ../src/main.c ../src/matrix.c \
    -L$XKAAPIDIR/lib -lxkaapi -lpthread -L$GMPDIR/lib -lgmp
