#!/bin/sh

PROJECT_ROOT=$(dirname $0)

if [ ! -z $1 ]; then
	CMAKE_BUILD_TYPE=$1
else
	CMAKE_BUILD_TYPE=Debug
fi

if [ -z $INCLUDE_PREFIX ]; then
	INCLUDE_PREFIX=/usr/local
	# /usr on linux
fi

if [ -z $VENDOR ]; then
	VENDOR=$PROJECT_ROOT/vendor
fi

	#-DCMAKE_COMMAND=/${PREFIX}/bin/cmake \

#export VENDOR
#export PREFIX
export PROJECT_ROOT

cmake  \
	-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
	-DVENDOR=$VENDOR \
	-DINCLUDE_PREFIX=$INCLUDE_PREFIX \
	-DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
	-B$PROJECT_ROOT/build \
	.
