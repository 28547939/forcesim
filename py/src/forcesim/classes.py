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

        _sockets = {}

        class subscriber_protocol:
            def __init__(self, listener_obj, t : subscriber_type_t) -> None:
                self._listener = listener_obj
                self._subscriber_type = t

            def connection_made(self, transport):
                self.transport = transport

            def datagram_received(self, data, addr):
                message = data.decode()
                #print('Received %r from %s' % (message, addr))

                try:
                    struct=json.loads(message)
                    points=struct[str(self._subscriber_type)]

                    if points == []:
                        print('received empty record - flushed')
                        self._listener._flushed.set()
                    else:
                        print('subscriber: received %s points' % (len(points)))
                        self._listener._graph.add_points(points)
                except json.JSONDecodeError as e:
                    print(e)

        def __init__(self, s : SubscriberConfig):
            self._subscriber_config = s
            self._flushed = asyncio.Event()

        def setup_graph(self, graph):
            self._graph = graph

        def remove_graph(self):
            del(self._graph)

        def render_graph(self, path=None):
            self._graph.to_file(path)
        
        async def wait_flushed(self):
            await self._flushed.wait()
            self._flushed.clear()

        async def _start_endpoint(self):
            addr=self._subscriber_config.addr
            port=self._subscriber_config.port

            print(f"subscriber UDP server starting at {addr}:{port}")

            loop = asyncio.get_running_loop()

            await loop.create_datagram_endpoint(
                lambda: self.subscriber_protocol(self, self._subscriber_config.type),
                local_addr=(addr, port)
            )


        def _destroy_listener(self):
            pass



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

        s_l = Subscriber.Listener(config)
        self.listener=s_l

        if graph:
            self.setup_graph(graph)


    async def start(self):
        print('start')

        try: 
            (s_r)=(self._interface.add_subscribers([self._config]))[0]
        except Interface.InterfaceException as e: 
            print('Aborting subscriber creation')
            print(e)

        print('subscriber record: %s' % s_r)
        self.record=s_r

        t=asyncio.create_task(self.listener._start_endpoint())

        print('started')

        await t

    def delete(self):
        self._interface.del_subscribers(self.record.id)
        del(self.record)
        self.remove_graph()

    """
        Configure this subscriber instance to emit received data to the given Graph
        Only one Graph can be associated with the subscriber at one time
    """
    def setup_graph(self, graph : Graph):
        self.listener.setup_graph(graph)
        self._graph=graph

    def remove_graph(self):
        self.listener.remove_graph()
        del(self._graph)

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
    
    def register_one(self):
        if self._record:
            raise Exception("already registered")

        self._record=self._interface.add_agents([ ( self._spec, 1 ) ])

    def subscribe_agentaction(self, config : SubscriberConfig, graph : Graph):
        config.parameter=self._record.id
        s=Subscriber(self._interface, config, graph)
        return s

    def delete(self):
        if not self._record:
            raise Exception('cannot delete: no record')

        self._interface.delete_agents([self._record.id])
    

class AgentSet():
    """
    create and manage a group of identical agents (each with its own ID,
    assigned by the forcesim instance, contained in AgentRecord objects)
    """
    def __init__(self, interface : Interface, spec : AgentSpec, count : int):
        self._interface=interface
        self._spec=spec

        self._agents=set([
            Agent(interface, spec) for _ in range(0, count)
        ])
        self._count=count
    
    #def add(self, agents : List[Agent]):
    #    self._agents.extend(agents)

    def register(self):
        records=self._interface.add_agents([ (self._spec, self._count) ])

        for (r, agent) in zip(records, self._agents):
            agent._record=r

    def delete(self):
        for agent in self._agents:
            if not agent._record:
                raise Exception("cannot delete agent: no record")
    
        self._interface.delete_agents([
            agent._record for agent in self._agents
        ])

        for agent in self._agents:
            del agent._record



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


