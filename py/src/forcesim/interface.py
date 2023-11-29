#!/usr/local/bin/python3.9

import pdb

import pandas
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

import asyncio, aiohttp

from .api_types import *



"""
Represents the state in the forcesim instance after creating a 
subscriber with the given `config`, which the instance assigns a
unique ID `id`
"""
#@dataclass(kw_only=True, frozen=True)
@dataclass(frozen=True)
class SubscriberRecord():
    config : SubscriberConfig
    id : int


#@dataclass(kw_only=True, frozen=True)
@dataclass(frozen=True)
class AgentRecord():
    config : AgentConfig
    id : int

"""
    Lower-level interface:
    More or less direct representation of the HTTP API exposed by the market simulator
    Stateless and "procedural" (ie not object-oriented); 
        used by stateful / object oriented interfaces 

    Structure of JSON returned by server:

    { 
       "is_error": bool,
       "message": str,
       "api_version": float,
       "data": Any  (generally either dict or list)
    }
"""
class Interface():
    """
    TODO documentation of return values, for now see Interface.h
    """
    
    class interface_ret():
        def __init__(self, resp, msg=None, data=None):
            self.resp = resp 
            self.msg = msg 
            self.data = data

    class Ok(interface_ret): 
        pass
    class Error(interface_ret):
        def __init__(self, error_code, *args, **kwargs):
            self.error_code=error_code
            super().__init__(*args, **kwargs)


    class InterfaceException(Exception):
        error : Interface.Error
        def __init__(self, *args):
            e=args.pop(0)
            self.error=e
            super().__init__()

    # TODO need to be able to access the arguments from the handler...


    def __init__(self, remote_addr, remote_port):
        self.remote_addr = remote_addr
        self.remote_port = remote_port
    
# TODO
    def _url(self, path):
        return self.remote_addr +':'+ self.remote_port

    def _ok_or_raise(self, ret : interface_ret) -> Ok:
        if isinstance(ret, Interface.Error):
            raise Interface.InterfaceException(ret)
        else:
            return ret


    async def _aio_json_req(self, path, data=None, method='POST') -> interface_ret:
        async with aiohttp.ClientSession() as session:
            url='http://'+ self.remote_addr +':'+ str(self.remote_port) + path

            if not isinstance(data, str):
                data=json.dumps(data, default=lambda x: str(x))

            async with session.request(method, url, data=data) as resp:
                try:
                    json_resp = await resp.json(content_type=None)

                    error_code=json_resp['error_code']
                    data=json_resp['data']
                    if error_code != None:
                        if error_code == error_code_t.Multiple:
                            if isinstance(data, list):
                                data_typed = [
                                    (str_to_error_code(ec), msg)
                                    for (ec, msg) in data
                                ]

                                return Interface.Error(resp, json_resp['message'], data_typed)

                            elif isinstance(data, dict):
                                # TODO
                                raise NotImplementedError
                            else:
                                raise TypeError()

                        else:
                            return Interface.Error(resp, json_resp['message'], json_resp['data'])
                    else:
                        return Interface.Ok(resp, json_resp['message'], json_resp['data'])

                except Interface.InterfaceException as e:
                    raise e
                except KeyError as e:
                    err=Interface.Error(
                        resp, 
                        msg='could not parse JSON response: ' + e,
                        data=None
                    )
                    raise Interface.InterfaceException(err)
                except json.JSONDecodeError as e:
                    err=Interface.Error(
                        resp,
                        msg="Unable to parse JSON in server's response",
                        data=resp.text
                    )
                    raise Interface.InterfaceException(err)



    ## path must have a leading slash
    #def _aio_json_req(self, path, data=None, method='POST'):
    #    # resp: requests.Response object
    #    # TODO validate, parse, and re-generate path using a library

    #    if not isinstance(data, str):
    #        data=json.dumps(data, default=lambda x: str(x))

    #    resp=requests.request(
    #        method, 
    #        'http://'+ self.remote_addr +':'+ str(self.remote_port) + path, 
    #        data=data
    #    )
    #    try: 
    #        json_resp=resp.json()
    #        #print(json_resp)
    #        return (resp, json_resp)
    #    except requests.exceptions.JSONDecodeError:
    #        data="Unable to parse JSON in server's response\nServer's response:\n\n%s\n" % resp.text
    #        raise Interface.InterfaceException(data)

    async def run(self, iter_count : int):
        ret=await self._aio_json_req('/market/run', data={"iter_count": iter_count})
        ret=self._ok_or_raise(ret)
        return ret
        

    async def stop(self):
        ret=await self._aio_son_req('/market/stop')
        ret=self._ok_or_raise(ret)
        return ret

	# TODO wait_for_stop will need to be async
    async def wait_for_stop(self):
        ret=await self._aio_json_req('/market/wait_for_stop', method='GET')
        ret=self._ok_or_raise(ret)
        return ret

    async def start(self):
        ret=await self._aio_json_req('/market/start')
        ret=self._ok_or_raise(ret)
        return ret

    async def configure(self, **kwargs):
        ret=await self._aio_json_req('/market/configure', method='POST', data=kwargs)
        ret=self._ok_or_raise(ret)
        return ret

    def get_price_history():
        pass

    # second item of spec_list tuple is `count`: the number of agents specified by AgentSpec
    # to add; for each (spec, count) pair, a list of size `count` is returned 
    async def add_agents(self, spec_list : List[Tuple[AgentSpec, int]]) -> List[List[AgentRecord]]:
        def validate(s : AgentSpec):
            if s.config.external_force <= 100 and s.config.external_force >= 0:
                return s
            else:
                return None

        validated_list=list(itertools.filterfalse(
            lambda x: x is None, 
            [ 
                dict(asdict(validate(spec)), count=count)
                for (spec, count) in spec_list 
            ]
        ))

        if len(validated_list) != spec_list:
            # TODO
            pass

        ret=await self._aio_json_req('/agent/add', data=validated_list)
        return self._ok_or_raise(ret)

    def delete_agents(self, x : List[int]) -> List[Tuple[int, Any]]:
        pass

    def get_agent_history(self, x: AgentRecord):
        pass
    def get_agent_history(self, x: int):
        pass

    def delete_agent_history(self, x : AgentRecord):
        pass
    def delete_agent_history(self, x : int):
        pass

    async def list_agents(self):
        ret=await self._aio_json_req('/agent/list', method='GET')
        ret=self._ok_or_raise(ret)
        return ret.data

    async def emit_info(self, x: List[Info]):
        info_json = []
        for info in x:
            info_json.append(asdict(info))

        ret=await self._aio_json_req('/info/emit', data=info_json)
        return self._ok_or_raise(ret)

    async def add_subscribers(self, ss : List[SubscriberConfig]) -> List[SubscriberRecord]:
        req_objects = []

        """
            remove the "parameter" configuration item, specifying it separately instead
            transform a parameter value of None to {}
            change addr, port to endpoint: { remote_addr: ..., remote_port: ... }
        """
        for config in ss:
            param = getattr(config, "parameter")
            config_dict=asdict(config)
            del(config_dict["parameter"])

            (addr,port)=(config_dict.pop('addr'), config_dict.pop('port'))
            config_dict['endpoint'] = {
                'remote_addr': addr,
                'remote_port': port,
            }

            req_objects.append({
                "parameter": {} if param is None else param,
                "config": config_dict
            })

        print(f"add_subscribers on {len(ss)} items")
        ret=await self._aio_json_req('/subscribers/add', data=req_objects)
        ret=self._ok_or_raise(ret)



        #print([resp_json['data'], ss, zip(resp_json['data'], ss)])

        # replace the data attribute in the existing Interface.Ok object
        ret.data=[
            SubscriberRecord(config=config, id=id)
            for (id, config) in itertools.filterfalse(
                lambda x: isinstance(x[1], str) == True,
                zip(ret.data, ss)
            )
        ]

        return ret
    
    async def del_subscribers(self, ss : List[SubscriberRecord]):
        ret=await self._aio_json_req('/subscribers/delete', data=[ s.id for s in ss ])
        ret=self._ok_or_raise(ret)
        return ret.data

    #async def del_subscribers(self, ids : List[int]):
    #    await self._aio_json_req('/subscribers/delete', data=ids)


    async def list_subscribers(self):
        ret=await self._aio_json_req('/subscribers/list', method='GET')
        ret=self._ok_or_raise(ret)
        return ret.data

    async def reset(self):
        ret=await self._aio_json_req('/market/reset', method='POST')
        ret=self._ok_or_raise(ret)
        return ret


"""

TODO make note of
 An uncaught exception occurred: [json.exception.type_error.304] cannot use at() with string
 usually because it's not being converted to JSON - just sending string representation
"""