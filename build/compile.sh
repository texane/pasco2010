#!/usr/bin/env sh

XKAAPIDIR=$HOME/install/xkaapi_master
GMPDIR=$HOME/install

gcc -std=c99 -Wall -O3 -march=native \
    -I$GMPDIR/include -I$XKAAPIDIR/include -I. \
    ../src/main.c ../src/matrix.c \
    ../src/mul_matrix_0.c \
    ../src/mul_matrix_1.c \
    ../src/innerprod.c \
    -L$XKAAPIDIR/lib -lxkaapi -lpthread -L$GMPDIR/lib -lgmp
