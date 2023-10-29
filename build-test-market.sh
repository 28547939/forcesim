#!/bin/sh


#clang++ -c -std=c++14 -I/usr/local/include/   main.cpp 
#clang++ -c -std=c++14 -I/usr/local/include/   Agent.cpp

#clang++ -o main Agent.o main.o

clang++ -g -O0 -c -std=c++20 -pthread -I/usr/local/include/  \
	-I/usr/home/market-force-sim/vendor/Crow/include \
	-I/usr/home/market-force-sim/vendor/json/include/   \
	-fdiagnostics-fixit-info \
	json_conversion.cpp Subscriber.cpp Market.cpp Interface.cpp Agent.cpp  test_market.cpp 


clang++ -o test-market -lglog -lboost_random -L/usr/local/lib -pthread  test_market.o Interface.o json_conversion.o Agent.o Market.o Subscriber.o 

