#!/bin/sh


# useful for development - set the DEV environment variable to 
# ensure the forcesim python library is available despite not
# being installed
if [ ! -z $DEV ]; then
    if [ -z $PYTHONPATH ]; then 
        export PYTHONPATH=../../src
    else
        export PYTHONPATH=$PYTHONPATH:../../src
    fi
fi

set -o nounset

$PYTHON ./basic.py  --agents-json-dir=./agents-json \
    --subscribers-json-dir=./subscribers-json \
    --info-json-dir=./info-json \
    --config-yaml=./config.yml
