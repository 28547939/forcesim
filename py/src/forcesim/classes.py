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

import asyncio


from .api_types import *
from .interface import Interface
import logging
from . import logging as forcesim_logging


"""
Higher-level interface
Classes to represent various entities in the simulator
"""

class Graph():

    # TODO support for animated, real-time graph
    def __init__(self, output_path=None, is_animated=False) -> None:
        self._output_path=output_path
        self._is_animated=is_animated
        self.reset()

    def add_points(self, points : List[tuple[int,int]]):
        for p in points:
            self._axis_tp.append(p[0])
            self._axis_price.append(p[1])
        
        #print('added %d points to graph' % len(points))


    def activate(self):
        pass

    def reset(self):
        self._axis_tp = []
        self._axis_price = []

    def to_file(self, path=None):
        
        if path is None:
            if self._output_path is None:
                raise Exception('saving graph to file: no path provided')

            path=self._output_path
            
        fig, ax = plt.subplots()
        ax.plot(self._axis_tp, self._axis_price, linewidth=0.1)
        plt.savefig(path, dpi=900)

        self.reset()




"""
TODO listener multiplexing
Subscribers which use a parameter (currently just AGENT_ACTION, but in general, most likely 
any subscriber type aside from PRICE) will require mutliplexing if we allow the same
UDP endpoint to be shared among distinct parameters (that is, distinct logical Subscribers)
In particular there will be a distinct Subscriber+Listener pair of instances for each 
distinct parameter value (eg agent ID), with an adapter class which de-multiplexes
data received by the subscriber_protocol, passing data to the correct Subscriber/Listener.

That way, we can run hundreds or even thousands of logically distinct Subscribers on the 
same UDP endpoint, so avoiding using a different UDP port for each one. 
Since each UDP message contains the parameter (eg agent ID) associated with the data, 
de-multiplexing will not be a problem.
"""

"""
Representation of a subscriber in this program
SubscriberRecord represents a subscriber that has been registered with the simulator
"""
class Subscriber():
    class Listener():

        _socket : Any
        _logger : logging.Logger

        class subscriber_protocol:
            def __init__(self, subscriber_obj):
                self._subscriber = subscriber_obj 

            def connection_made(self, transport):
                self.transport = transport

            def datagram_received(self, data, addr):
                message = data.decode()
                #print('Received %r from %s' % (message, addr))

                try:
                    struct=json.loads(message)
                    points=struct[str(self._subscriber._config.type)]

                    if points == []:
                        self._subscriber._logger.debug('received empty record - flushed')
                        self._subscriber._flushed_wait.set()

                    else:
                        self._subscriber._logger.info(
                            f'subscriber ({self._subscriber._config.type}): received '
                            +f'%s points' % (len(points))
                        )
                        self._subscriber.add_points(points)
                except json.JSONDecodeError as e:
                    print(e)

        def __init__(self, subscriber_obj):
            self._subscriber = subscriber_obj

        async def _start_endpoint(self):
            addr=self._subscriber._config.addr
            port=self._subscriber._config.port

            self._subscriber._logger.info(f"subscriber UDP server starting at {addr}:{port}")

            loop = asyncio.get_running_loop()

            self._socket=await loop.create_datagram_endpoint(
                lambda: self.subscriber_protocol(self._subscriber),
                local_addr=(addr, port)
            )


        def _destroy_listener(self):
            del(self._socket)



    _flushed_wait : Any
    _record_count_wait : Dict[int, Tuple[int, asyncio.Event]] = {}
    _points : List = []

    def __init__(self, i : Interface, config : SubscriberConfig, graph : Optional[Graph] = None):
        """
        get a SubscriberRecord (i.e. register the subscriber with the Market instance)
        create listener
        set up graph

        returns SubscriberRecord, SubscriberListener, and SubscriberGraph in a tuple,
        with each of these instances already configured to pass data to the others

        client needs to activate the listener (so that it passes data to the hook) and
        activate the graph (so that it appears on screen)

        TODO how to manage destruction
        """

        self._interface=i
        self._config=config
        self._logger=forcesim_logging.get_logger(f'Subscriber({config.type})')

        s_l = Subscriber.Listener(self)
        self.listener=s_l

        self._flushed_wait = asyncio.Event()

        if graph:
            self.setup_graph(graph)


    async def start(self):
        try: 
            ret=await self._interface.add_subscribers([self._config])
        except Interface.InterfaceException as e: 
            if e.error.error_code == error_code_t.Multiple:
                if ret[0][0] == error_code_t.Subscriber_config_error:
                    self._logger.error('Subscriber_config_error: %s (%s)', 
                        ret[0][1], self._config
                    )
                else:
                    raise e
            else:
                raise e

        #(s_r)=ret.data[0]
        s_r=ret.data[0]
        self.record=s_r

        self._logger.info('successfully started subscriber (id=%s)', self.record.id)
        self._logger.debug('subscriber record=%s', self.record)

        await asyncio.create_task(self.listener._start_endpoint())

    async def delete(self):
        await self._interface.del_subscribers(self.record.id)
        del(self.record)
        self.remove_graph()

    def add_points(self, points):
        #self._logger.debug(f'points: {points}')
        self._points.extend(points)
        if self._graph:
            self._graph.add_points(points)

        for (count, evs) in self._record_count_wait.items():
            print(f'{count} {len(self._points)}')
            if len(self._points) >= count:
                for ev in evs:
                    ev.set()

    """
        Configure this subscriber instance to emit received data to the given Graph
        Only one Graph can be associated with the subscriber at one time
    """
    def setup_graph(self, graph : Graph):
        self._graph=graph

    def remove_graph(self):
        #self.listener.remove_graph()
        del(self._graph)

    def render_graph(self, path=None):
        #self.listener.render_graph()
        self._graph.to_file(path)
    
    def has_graph(self):
        try:
            if isinstance(self.listener._graph, Graph):
                return True
            else:
                raise TypeError('has_graph: subscriber listener has a non-Graph _graph attribute')
        except AttributeError:
            return False

    async def wait_flushed(self):
        if not self._flushed_wait.is_set():
            await self._flushed_wait.wait()

        self._flushed_wait.clear()

    async def wait_record_count(self, count : int):
        if len(self._points) >= count:
            return

        ev=asyncio.Event()
        if count in self._record_count_wait:
            self._record_count_wait[count].append(ev)
        else:
            self._record_count_wait[count]=[ ev ]

        await ev.wait()
        self._record_count_wait[count].remove(ev)


    """
    TODO - allow the subscriber to pass on received data to other components
    """
    def add_consumer(self):
        raise NotImplementedError
        #pass



class Agent():
    def __init__(self, i : Interface, spec : AgentSpec):
        self._interface=i
        self._spec=spec
        self._logger=forcesim_logging.get_logger('Agent')
    
    async def register_one(self):
        if self._id:
            self._logger ("Agent.register_one: already registered")

        self._id=await self._interface.add_agents([ ( self._spec, 1 ) ])

    async def subscribe_agentaction(self, config : SubscriberConfig, graph : Graph):
        raise NotImplementedError

        #config.parameter=self._id.id
        #s=Subscriber(self._interface, config, graph)
        #return s

    async def delete(self):
        if self._id == None:
            self._logger.error('Agent.delete called, but no record ')

        await self._interface.delete_agents([self._id.id])
    

class AgentSet():
    """
    create and manage a group of identical agents (each with its own ID,
    assigned by the forcesim instance, contained in AgentRecord objects)
    """
    def __init__(self, interface : Interface, spec : AgentSpec, count : int):
        self._interface=interface
        self._spec=spec
        self._logger=forcesim_logging.get_logger('AgentSet')

        self._agents=set([
            Agent(interface, spec) for _ in range(0, count)
        ])
        self._count=count
    
    async def register(self):
        ret=await self._interface.add_agents([ (self._spec, self._count) ])
        id_list=ret.data[0]


        for (id, agent) in zip(id_list, self._agents):
            agent._id=id

    async def delete(self):
        for agent in self._agents:
            if agent._id is None:
                self._logger.error(
                    'AgentSet.delete: an agent was found without ID (spec=%s)', self._spec
                )
    
        try: 
            deleted=await self._interface.delete_agents([
                agent._id for agent in self._agents
            ])
        except Interface.InterfaceException as e:
            for (error_code, data) in e.error.handle_multiple():
                if error_code == error_code_t.Not_found:
                        self._logger.error(
                            'AgentSet.delete: agent ID does not exist in forcesim instance: {'
                        )
                else:
                    raise e


        # delete all our agents regardless of the outcome of the request - if there's an
        # error, we need to clean up anyway
        for agent in self._agents:
            del agent._id



@dataclass
class Config():
    # ID associated with the agent's JSON config -> how many of this agent to create
    agents: Dict[str, int]

    # IDs associated with the subscriber's JSON config, mapped to dict with additional options
    subscribers: Dict[str, Dict]

    #graphs: Dict[str, bool] TODO - possibly separate graph config when more options are provided

    output_dir: str

    iter_block_size: Optional[int]

    info_sequence: List[List[str]]


