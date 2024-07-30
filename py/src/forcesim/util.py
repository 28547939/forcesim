from typing import List, Tuple, Any, Optional, Dict, Generator

import json
import yaml
import os


from .api_types import *
from .classes import *
from . import logging as forcesim_logging

    

def load_config_yaml(filename) -> Config:
    with open(filename, 'r') as f:
        config=yaml.safe_load(f)

        return Config(**config)


# trivial base type just to check membership in do_load_json_recursive
class JsonLoader():
    pass

class JsonLoaders(Enum):
    Agents = auto()
    Subscribers = auto()
    Info = auto()

logger = forcesim_logging.get_logger('util')



"""
the following functions can throw json.JSONDecodeError

in the case of Agent and Info objects, the configuration will differ depending on 
the specific type of the Agent or Info. The proper constructor is selected using the 
`type` 
"""



def load_json_recursive(path, cls, verbose=False): 
    if path is None:
        if verbose:
            print('load_json_recursive: path is None')

        return dict()

    merged=dict()
    for ret in do_load_json_recursive(path, cls):
        logger.debug(f'load_json_recursive: loading {ret[0]}')
        merged.update(ret[1])

    return merged

def do_load_json_recursive(path, cls):
    if not issubclass(cls, JsonLoader):
        raise Exception(f'{cls} is not a JsonLoader')

    for dirpath, _,  files in os.walk(path):
        for filename in files:
            if not (filename.endswith('.json') or filename.endswith('.JSON')):
                logger.debug(f"file {filename} skipped (filename isn't .json)")
                continue

            path=os.path.join(dirpath, filename)
            ret=cls.load(path)
            if ret is not None:
                yield (path, ret)


class AgentLoader(JsonLoader):
    @staticmethod
    def load(filename) -> Generator[Tuple[str, Tuple[AgentSpec,Optional[str]] ], None, None]:
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
                    logger.error(f"""Failed to load agent {k}:
                        {repr(e)}
                        JSON data:
                        {x}
                    """)

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
                    logger.error(f"""Failed to load subscriber {k}:
                        {repr(e)}
                        JSON data:
                        {x}
                    """)



class InfoLoader(JsonLoader):
    @staticmethod
    def load(filename) -> Dict[str, Info]:
        with open(filename, 'r') as f:
            conf=json.load(f)

            for k, item in conf.items():
                t=item['type']

                info=(info_ctor[str_to_infotype(t)])(**item)
                yield (k, info)


