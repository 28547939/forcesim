#!/usr/local/bin/python3.9

import itertools

import logging
import argparse

import json


import logging
import forcesim.logging

from forcesim.api_types import *
from forcesim.classes import *
import forcesim.util as util


def main():

    log=forcesim.logging.get_logger('graph')

    prs=argparse.ArgumentParser(
        prog='',
        description='',
    )

    prs.add_argument('--points-file', required=True)
    prs.add_argument('--graph-file', required=True)
    args=vars(prs.parse_args())
    print(args)

    points=Subscriber.load_points_file(args['points_file'])
    
    graph=Graph(output_path=args['graph_file'])
    graph.add_points(points)

    graph.image_to_file()


if __name__ == '__main__':
    ain()
