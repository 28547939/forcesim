#!/bin/sh


# useful for development
#if [ -z $PYTHONPATH ]; then 
#	export PYTHONPATH=../../src
#else
#	export PYTHONPATH=$PYTHONPATH:../../src
#fi

set -o nounset

$PYTHON ./basic.py  --agents-json-dir=./agents-json \
	--subscribers-json-dir=./subscribers-json \
	--info-json-dir=./info-json \
	--config-yaml=./config.yml
