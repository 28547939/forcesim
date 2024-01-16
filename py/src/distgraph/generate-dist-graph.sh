#!/bin/sh

set -o nounset
set -x

BASE=../..

# generate-dist-graph.sh
# See notes in generate-dist-graph.py

$BASE/v2-dist-points --agent-config-path $BASE/py/tests/basic/agents-json/1.json  \
	--agent-config-key 2023-11-19_3 --price-view 100 --current-price 1 --subjectivity-extent 0.99 \
	| /usr/local/bin/python3.9 ./generate-dist-graph.py \
		--distgraph-output-file ./distgraph.png \
		--parameters-output-file ./parameters.png
