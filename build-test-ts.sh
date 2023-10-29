#!/bin/sh


#clang++ -c -std=c++14 -I/usr/local/include/   main.cpp 
#clang++ -c -std=c++14 -I/usr/local/include/   Agent.cpp

#clang++ -o main Agent.o main.o

WORKDIR=/home/market-force-sim/clone/swap-work

TMPDIR=$WORKDIR clang++ -g -O0 -c -std=c++20 -ferror-limit=100 -pthread -I/usr/local/include/  \
	-fdiagnostics-fixit-info \
	tests/ts.cpp 

#-I/usr/home/market-force-sim/vendor/Crow/include \


#clang++ -o main -pthread Agent.o Interface.o 

clang++ -o test-ts -lglog -lboost_random -L/usr/local/lib -pthread  ts.o

