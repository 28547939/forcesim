#!/usr/local/bin/python3.9

import matplotlib

import itertools

from datetime import date

import requests

import matplotlib.pyplot as plt
import numpy as np

from typing import List, Tuple, Any, Optional, Dict


import yaml
from dataclasses import dataclass, asdict
from enum import Enum, auto

import json

import asyncio

from api_types import *
from classes import *


# TODO deprecated for now - use tests/basic/basic.py


async def control_task(i : Interface, s : Subscriber):

    agents = []
    N = 200
    for k in range(N):
        x = Agent(i, agentclass_t.ModeledCohortAgent,
            external_force=0.01,
            schedule_every=k+1, 
            initial_variance=1, 
            variance_multiplier=0.1,
            force_threshold=1,
            default_price_view=2
        )

        agents.append(x)

    M = 20
    for k in range(M):
        pass

        x = Agent(i, agentclass_t.ModeledCohortAgent,
            external_force=0.1,
            schedule_every=(k+1)*10,
            initial_variance=1, 
            variance_multiplier=0.1,
            force_threshold=1,
            default_price_view=2
        )

        agents.append(x)


    await i.run(100)
    i.start()

    print('started')

    await i.wait_for_stop()
    print('wait_for_stop completed')


    info_0 = SubjectiveInfo(
        type=info_type_t.Subjective,
        subjectivity_extent=1,
        price_indication=1000,
        is_relative=False
    )

    i.emit_info([ info_0 ])

    #print(i.list_subscribers())
    #print(i.list_agents())

    await i.run(500)

    await i.wait_for_stop()
    print('wait_for_stop completed')

    await s.listener.wait_flushed()

    print('wait_flushed completed')

    s.listener.render_graph('./test.png')

    i.reset()







async def main():

    i = Interface("127.0.0.1", 18080)
    #I.configure_market('test.yml')

    i.reset()

    s_test = Subscriber(
        i=i,
        sconfig=SubscriberConfig(type=subscriber_type_t.PRICE, granularity=1, port=5000, addr="127.0.0.1"),
        graph=Graph()
    )

    await s_test.start()
    await asyncio.create_task(control_task(i, s_test))


    #t_subscriber=asyncio.create_task(s_test.start())

    #await asyncio.gather(
    #    t_control,
    #    #t_subscriber,
    #)




if __name__ == '__main__':
    loop=asyncio.get_event_loop()
    try:
        loop.run_until_complete(main())
#        loop.run_forever()
    finally:
        loop.close()
