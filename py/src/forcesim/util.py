#!/usr/local/bin/python3.9

import pdb


from datetime import date


from typing import List, Tuple, Any, Optional, Dict


from dataclasses import dataclass, asdict
from enum import Enum, auto

import json
import yaml

import asyncio


from .api_types import *
from .classes import *




    


"""
the following functions can throw json.JSONDecodeError

in the case of Agent and Info objects, the configuration will differ depending on 
the specific type of the Agent or Info. The proper constructor is selected using the 
`type` 
"""

def load_agents_from_json(filename) -> Dict[str, Tuple[AgentSpec,Optional[str]] ]:
    with open(filename, 'r') as f:
        conf=json.load(f)

        for (k, x) in conf.items():
            t=x['type']
            del x['type']

            subscriber_ref = None
            if 'subscriber' in x:
                subscriber_ref = x['subscriber']
                del x['subscriber']

            try: 
                config=(agentconf_ctor[str_to_agentclass(t)])(**x)
            except TypeError as e:
                print(f'Failed to load agent {k}:')
                print(e)
                print("\nJSON data:")
                print(x)
                print("\n")

                continue

            yield (k, (
                AgentSpec(
                    type=t,
                    config=config,
                ),
                subscriber_ref
            ))

            # TODO exception

def load_config_yaml(filename) -> Config:
    with open(filename, 'r') as f:
        config=yaml.safe_load(f)

        return Config(**config)

def load_subscribers_from_json(filename) -> Dict[str, SubscriberConfig]:
    with open(filename, 'r') as f:
        conf=json.load(f)

        for (k, x) in conf.items():
            t=str_to_subscriber_type(x['type'])
            x['type']=t

            try: 
                yield (k, SubscriberConfig(**x))
            except TypeError as e:
                print(f'Failed to load subscriber {k}:')
                print(e)
                print("\nJSON data:")
                print(x)
                print("\n")



def load_info_from_json(filename) -> Dict[str, Info]:
    with open(filename, 'r') as f:
        conf=json.load(f)

        for k, item in conf.items():
            t=item['type']

            info=(info_ctor[str_to_infotype(t)])(**item)
            yield (k, info)