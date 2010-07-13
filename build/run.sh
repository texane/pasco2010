#!/usr/bin/env sh

N=100
B=32


rm /tmp/o ;

for N in `seq 10 10 300`; do
    echo $N ;
    KAAPI_CPUSET=8,9,10,11,12,13,14,15 \
    KAAPI_WSSELECT=workload \
    ./a.out ../data/A_n$N\_b$B ../data/B_n$N\_b$B ../data/C_n$N\_b$B >> /tmp/o ;
done

#./a.out ../data/A_n10_b10000 ../data/B_n10_b10000 ../data/C_n10_b10000
#./a.out ../data/A_n10_b512 ../data/B_n10_b512 ../data/C_n10_b512
#./a.out ../data/A_n40_b128 ../data/B_n40_b128 ../data/C_n40_b128
#./a.out ../data/A_n100_b10000 ../data/B_n100_b10000 ../data/C_n100_b10000
#./a.out ../data/A_n1000_b200 ../data/B_n1000_b200 ../data/C_n1000_b200
#./a.out ../data/A_n20 ../data/B_n20 ../data/C_n20 ../data/C_n20.seq