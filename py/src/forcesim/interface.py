import itertools

from datetime import datetime

from typing import List, Tuple, Any, Optional, Dict

from dataclasses import dataclass, asdict
from enum import Enum, auto

import json

import asyncio, aiohttp
import logging
import pprint

from .api_types import *
from . import logging as forcesim_logging



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

class interface_ret():
    def __init__(self, url, req_data, resp, json, text, msg=None, data_type=None, data=None):
        self.url=url
        self.req_data=req_data
        self.resp = resp 
        self.json=json
        self.text=text
        self.msg = msg 
        self.data = data
        self.data_type = data_type

    def __str__(self):
        x=pprint.PrettyPrinter()
        return x.pformat(self.__dict__)

class Ok(interface_ret): 
    pass
class Error(interface_ret):
    def __init__(self, error_code, *args, **kwargs):
        self.code=error_code
        super().__init__(*args, **kwargs)


"""
    Lower-level interface:
    More or less direct representation of the HTTP API exposed by the market simulator
    Stateless and "procedural" (ie not object-oriented); 
        used by stateful / object oriented interfaces 

    Structure of JSON returned by server:

    { 
       "error_code": str|null,  # string representation of error code, or null if no error
       "message": str,
       "api_version": float,
       "data_type": TODO
       "data": Any  (generally either dict or list)
    }
"""
class Interface():
    """
    TODO documentation of return values, for now see Interface.h
    """

    class InterfaceException(Exception):
        pass

    """
    raised when the response from the server indicates that an error occurred during the request
    """
    class ErrorResponseException(InterfaceException):
        error : Error
        def __init__(self, error : Error, *args, **kwargs):
            self.error=error

            # calls interface_ret.__str__
            output=f"""The instance responded with an error:\n\n{error.__str__()}"""
            super().__init__(output, *args, **kwargs)

        # TODO add error to output string

    """
    raised when the response received from the server does not match correct specification /
    it can't be processed
    """
    class ResponseIntegrityException(InterfaceException):
        # set if this exception was the result of a (built-in) exception 
        internal_exception : Optional[Exception]
        # raw response
        response_text : str
        # JSON structure, if it was possible to load it
        response_json : Optional[dict]
        # optionally, a message to describe the condition
        message : Optional[str]

        def __init__(self, response_text, response_json, 
            internal_exception=None, message=None, data_processed=None,
            *args, **kwargs):

            self.response_json=response_json
            self.response_text=response_text
            self.internal_exception=internal_exception
            self.message=message
            self.data_processed=data_processed

            # string which is printed with the exception
            output=f"""
            response_json={response_json}
            response_text='{response_text}'
            message={message}
            internal_exception={repr(internal_exception)}
            data_processed={data_processed}
            """

            super().__init__(output, *args, **kwargs)

        


    def __init__(self, remote_addr, remote_port):
        self.remote_addr = remote_addr
        self.remote_port = remote_port

        self.logger=forcesim_logging.get_logger('interface')

        
    
# TODO
    def _url(self, path):
        return self.remote_addr +':'+ self.remote_port

    def _ok_or_raise(self, ret : interface_ret) -> Ok:
        if isinstance(ret, Error):
            # TODO possibly use ExceptionGroup in the case of Multiple
            raise Interface.ErrorResponseException(ret)
        else:
            return ret


    """
    perform some minimal interpretation/processing of the raw response 
    structure to make it easier for the client program to detect errors
    and process the data
    """
    async def _process_response(self, url, data, resp):

        resp_text = await resp.text()
        # for generate_exception closure - it gets set in the try-catch below
        resp_json=None

        def generate_exception(message=None, internal_exception=None):
            return Interface.ResponseIntegrityException(
                response_text=resp_text, response_json=resp_json,
                internal_exception=internal_exception, message=message
            )

        try:
            resp_json = await resp.json(content_type=None)

            message=resp_json['message']
            data=resp_json['data']

            error_code=(
                str_to_error_code(resp_json['error_code']) 
                if resp_json['error_code'] is not None
                else None
            )

            try:
                if resp_json['data_type'] != None:
                    data_type=str_to_response_type(resp_json['data_type'])
                else:
                    data_type=None
            except AttributeError as e:
                raise generate_exception(
                    message='unknown data_type, not in response_type_t',
                    internal_exception=e
                ) from e
                

        except KeyError as e:
            raise generate_exception(
                message='missing keys from JSON response',
                internal_exception=e
            ) from e
        except json.JSONDecodeError as e:
            raise generate_exception(
                message='unable to parse JSON',
                internal_exception=e
            ) from e

        
        def multi_convert_error_code(key, entry):
            if isinstance(entry, tuple):
                (ec, msg)=entry
                return (str_to_error_code(ec), msg)
            else:
                raise generate_exception(
                    message=f'key {key} was marked as an error, but does '
                    +'not contain error structure'
                )

        data_processed=None

        # extra level of structure if it's one of the Multiple-type requests
        if data_type != None and data_type.is_multiple():
            error_keys=data['error_keys']
            data=data['data']

            if error_code is not None and error_code != error_code_t.Multiple:
                raise generate_exception(
                    message='error response with data_type of Multiple_* requires error code Multiple'
                )

        if data_type == response_type_t.Data:
            data_processed=data

        elif data_type == response_type_t.Multiple_barelist:
            if not isinstance(data, list):
                raise generate_exception(
                    message='Multiple_barelist response type indicated but data is not a list'
                ) 

            # with Multiple_barelist, server returns items in the list in the order that 
            # they were submitted in the request
            # here we make it easier for the client by including the position in the list
            #   as a numeric index and permitting lookup with a dict
            data_processed=dict()

            # dict allows us to either iterate over pairs or look up by index
            # as of Python 3.7, the dict is ordered by insertion order
            for i in range(0, len(data)):
                if i in error_keys:
                    data_processed[i]=multi_convert_error_code(i, data[i])
                else:
                    data_processed[i]=data[i]

        elif data_type == response_type_t.Multiple_pairlist:
            data_processed
            if not isinstance(data, list):
                raise generate_exception(
                    message='Multiple_pairlist response type indicated but data is not a list'
                )

            data_processed=[]

            for (key, item) in data:
                if key in error_keys:
                    data_processed.append( (key, multi_convert_error_code(key, item)) )
                else:
                    data_processed.append( (key, item) )
        
        elif data_type == response_type_t.Multiple_stringmap:
            if not isinstance(data, dict):
                raise generate_exception(
                    message='Multiple_stringmap response type indicated but data is not a dict'
                ) 

            data_processed={}

            for (key, item) in data.items():
                if key in error_keys:
                    data_processed[key]=multi_convert_error_code(key, item)
                else:
                    data_processed[key]=item

            else:
                raise generate_exception(
                    message=f'invalid value for data_type: {data_type}'
                )

        # no processing - we return an Ok with data=None
        elif data_type == None and data == None:
            pass

        # data_type should only be null if data is also null
        elif data_type == None and data != None:
            raise generate_exception(
                message='response has data_type=null but non-null data'
            )
        
        else:
            raise generate_exception(
                message='internal error: unimplemented data_type: data_type={data_type}'
            )

        if error_code is not None:
            return Error(error_code, url, data, resp, resp_json, 
            resp_text, message, data_type, data=data_processed)
        else:
            return Ok(url, data, resp, resp_json, resp_text, message, 
            data_type, data=data_processed)


    async def _aio_json_req(self, path, data=None, method='POST') -> interface_ret:
        async with aiohttp.ClientSession() as session:
            url='http://'+ self.remote_addr +':'+ str(self.remote_port) + path

            if not isinstance(data, str):
                data=json.dumps(data, default=lambda x: str(x))

            async with session.request(method, url, data=data) as resp:
                return await self._process_response(url, data, resp)




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

    def get_price_history(self):
        pass

    # second item of spec_list tuple is `count`: the number of agents specified by AgentSpec
    # to add; for each (spec, count) pair, a list of size `count` is returned 
    async def add_agents(self, spec_list : List[Tuple[AgentSpec, int]]): #-> List[List[AgentRecord]]:
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

        if len(validated_list) != len(spec_list):
            diff=len(spec_list) - len(validated_list) 
            self.logger.warning(f'{diff} entries in add_agents were discarded')

        ret=await self._aio_json_req('/agent/add', data=validated_list)
        ret=self._ok_or_raise(ret)

        for (i, ids) in ret.data.items():
            if len(ids) != validated_list[i]['count']:
                raise Interface.ResponseIntegrityException(
                    response_json=ret.json, response_text=ret.text,
                    message=f'add_agents: AgentSpec at position {i}: response contains {len(ids)} '
                    +f'entries, request contained {len(validated_list[i])}. AgentSpec={spec_list[i]}',
                )

        return ret

    async def delete_agents(self, x : List[int]): # -> List[Tuple[int, Any]]:
        ret=await self._aio_json_req('/agent/delete', data=x)
        return self._ok_or_raise(ret)

    def get_agent_history(self, x: AgentRecord):
        pass

    def delete_agent_history(self, x : AgentRecord):
        pass

    def get_price_history(self):
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

    async def add_subscribers(self, ss : List[SubscriberConfig]): # -> List[SubscriberRecord]:
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

        # replace the data attribute in the existing Interface.Ok object
        ret.data=[
            SubscriberRecord(config=config, id=id)
            for (id, config) in itertools.filterfalse(
                lambda x: isinstance(x[1], str) == True,
                zip(ret.data.values(), ss)
            )
        ]

        return ret
    
    async def del_subscribers(self, ss : List[SubscriberRecord]):
        ret=await self._aio_json_req('/subscribers/delete', data=[ s.id for s in ss ])
        ret=self._ok_or_raise(ret)
        return ret.data


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