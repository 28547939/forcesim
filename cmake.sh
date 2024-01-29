#!/bin/sh

if [ ! -z $1 ]; then
	CMAKE_BUILD_TYPE=Debug
else
	CMAKE_BUILD_TYPE=Release
fi

if [ -z $PREFIX ]; then
	PREFIX=/usr/local
	# /usr on linux
fi



cmake  \
	-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
	-DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
	-DCMAKE_COMMAND=/${PREFIX}/bin/cmake \
	-B./build \
	.
