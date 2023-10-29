#!/bin/sh


#clang++ -c -std=c++14 -I/usr/local/include/   main.cpp 
#clang++ -c -std=c++14 -I/usr/local/include/   Agent.cpp

#clang++ -o main Agent.o main.o

WORKDIR=/home/market-force-sim/clone/swap-work

TMPDIR=$WORKDIR clang++ -g -O0 -c -std=c++20 -ferror-limit=100 -pthread -I/usr/local/include/  \
	-I/usr/home/market-force-sim/vendor/json/include/ \
	-fdiagnostics-fixit-info \
	json_conversion.cpp Subscriber.cpp Market.cpp tests/lock.cpp 

#-I/usr/home/market-force-sim/vendor/Crow/include \


#clang++ -o main -pthread Agent.o Interface.o 

clang++ -o test-lock -lglog -lboost_random -L/usr/local/lib -pthread  lock.o Market.o Agent.o Subscriber.o json_conversion.o

