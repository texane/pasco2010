#!/usr/bin/env sh

KAAPI_CPUSET=8,9 \
KAAPI_WSSELECT=workload \
./a.out ../data/A_n20 ../data/B_n20 ../data/C_n20 ../data/C_n20.seq
