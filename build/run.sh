#!/usr/bin/env sh

KAAPI_CPUSET=8,9,10,11,12,13,14,15 \
KAAPI_WSSELECT=workload \
./a.out ../data/A_n100_b10000 ../data/B_n100_b10000 ../data/C_n100_b10000
#./a.out ../data/A_n20 ../data/B_n20 ../data/C_n20 ../data/C_n20.seq