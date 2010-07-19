#!/usr/bin/env sh

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$HOME/install/lib

KAAPI_CPUSET=0,1 \
KAAPI_WSSELECT=workload \
./a.out ../data/A_n20 ../data/B_n20 ../data/C_n20 ../data/C_n20.seq
#./a.out ../data/A_n100_b10000 ../data/B_n100_b10000 ../data/C_n100_b10000 ../data/C_n100_b10000.seq
