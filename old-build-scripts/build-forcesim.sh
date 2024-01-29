#!/bin/sh


#clang++ -c -std=c++14 -I/usr/local/include/   main.cpp 
#clang++ -c -std=c++14 -I/usr/local/include/   Agent.cpp

#clang++ -o main Agent.o main.o

WORKDIR=/home/market-force-sim/forcesim/

#rsync -aP  --include='*.cpp' --include='*.h' --include='*.hpp' --exclude='*' /home/market-force-sim/clone/ $WORKDIR

cd $WORKDIR

TMPDIR=$WORKDIR clang++ -g -O0 -c -std=c++20 -ferror-limit=100 -pthread -I/usr/local/include/  \
	-I/usr/home/market-force-sim/vendor/Crow/include \
	-I/usr/home/market-force-sim/vendor/json/include/   \
	-fdiagnostics-fixit-info \
	 json_conversion.cpp Subscriber.cpp Market.cpp Interface.cpp Agent.cpp  forcesim.cpp

#-I/usr/home/market-force-sim/vendor/Crow/include \


#clang++ -o main -pthread Agent.o Interface.o 

clang++ -o forcesim -lboost_program_options -lglog -L/usr/local/lib -pthread  forcesim.o Interface.o json_conversion.o Agent.o  Market.o Subscriber.o

