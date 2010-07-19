#!/usr/bin/env sh

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$HOME/install/xkaapi_master/lib

#KAAPI_CPUSET=8,9,10,11,12,13,14,15 \
KAAPI_CPUSET=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 \
KAAPI_WSSELECT=workload \
./a.out ../data/A_n4_b100000 ../data/B_n4_b100000 ../data/C_n4_b100000
#./a.out ../data/A_n4_b50000 ../data/B_n4_b50000 ../data/C_n4_b50000
#./a.out ../data/A_n4_b10000 ../data/B_n4_b10000 ../data/C_n4_b10000
#./a.out ../data/A_n100_b1000 ../data/B_n100_b1000 ../data/C_n100_b1000
#./a.out ../data/A_n300_b1000 ../data/B_n300_b1000 ../data/C_n300_b1000
#./a.out ../data/A_n20 ../data/B_n20 ../data/C_n20 ../data/C_n20.seq
#./a.out ../data/A_n100_b10000 ../data/B_n100_b10000 ../data/C_n100_b10000 ../data/C_n100_b10000.seq
