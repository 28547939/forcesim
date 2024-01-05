#!/bin/sh


#clang++ -c -std=c++14 -I/usr/local/include/   main.cpp 
#clang++ -c -std=c++14 -I/usr/local/include/   Agent.cpp

#clang++ -o main Agent.o main.o

clang++ -g -O0 -c -std=c++20 -pthread -I/usr/local/include/  \
	-I/usr/home/market-force-sim/vendor/Crow/include \
	-I/usr/home/market-force-sim/vendor/json/include/   \
	-fdiagnostics-fixit-info \
	tests/v2_dist_points.cpp Agent.cpp json_conversion.cpp 


clang++ -o v2-dist-points -lglog -lboost_program_options -lboost_random -L/usr/local/lib -pthread  \
	v2_dist_points.o Agent.o json_conversion.o 

