#!/bin/sh

export PYTHONPATH=../../src

./basic.py  --agents-json-dir=./agents-json \
	--subscribers-json-dir=./subscribers-json \
	--info-json-dir=./info-json \
	--config-yaml=./config.yml
