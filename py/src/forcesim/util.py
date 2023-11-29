#!/usr/local/bin/python3.9

import pdb


from datetime import date


from typing import List, Tuple, Any, Optional, Dict



import json
import yaml
import os

import asyncio


from .api_types import *
from .classes import *



    

def load_config_yaml(filename) -> Config:
    with open(filename, 'r') as f:
        config=yaml.safe_load(f)

        return Config(**config)


"""
the following functions can throw json.JSONDecodeError

in the case of Agent and Info objects, the configuration will differ depending on 
the specific type of the Agent or Info. The proper constructor is selected using the 
`type` 
"""

class JsonLoader():
    pass

class JsonLoaders(Enum):
    Agents = auto()
    Subscrbiers = auto()
    Info = auto()

#loaders = {
#    JsonLoaders
#}

def load_json_recursive(path, cls, verbose=False): 
    if path is None:
        if verbose:
            print('load_json_recursive: path is None')

        return dict()

    merged=dict()
    for ret in do_load_json_recursive(path, cls):
        if verbose == True:
            print(f'load_json_recursive: loading {ret[0]}')

        merged.update(ret[1])

    return merged

def do_load_json_recursive(path, cls):
    if not issubclass(cls, JsonLoader):
        raise Exception(f'{cls} is not a JsonLoader')

    for dirpath, _,  files in os.walk(path):
        for filename in files:
            path=os.path.join(dirpath, filename)
            ret=cls.load(path)
            if ret is not None:
                yield (path, ret)


class AgentLoader(JsonLoader):
    @staticmethod
    def load(filename) -> Dict[str, Tuple[AgentSpec,Optional[str]] ]:
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


class SubscriberLoader(JsonLoader):
    @staticmethod
    def load(filename) -> Dict[str, SubscriberConfig]:
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



class InfoLoader(JsonLoader):
    @staticmethod
    def load(filename) -> Dict[str, Info]:
        with open(filename, 'r') as f:
            conf=json.load(f)

            for k, item in conf.items():
                t=item['type']

                info=(info_ctor[str_to_infotype(t)])(**item)
                yield (k, info)


