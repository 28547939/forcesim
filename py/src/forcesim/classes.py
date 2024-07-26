import matplotlib.pyplot as plt
from typing import List, Tuple, Any, Optional, Dict
from dataclasses import dataclass, asdict

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

    class actual_defaultdict(dict):
        def __init__(self, defaults : dict):
            self._defaults=defaults
        def __missing__(self, key):
            return self._defaults.get(key)

    # TODO support for animated, real-time graph
    def __init__(self, output_path=None, is_animated=False) -> None:
        self._output_path=output_path
        self._is_animated=is_animated


        self._settings=Graph.actual_defaultdict(dict(
            linewidth=0.25,
            dpi=600,
            title='Untitled'
        ))
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

    def settings(self, **kwargs):
        if len(kwargs) == 0:
            return self._settings
        else:
            self._settings.update(kwargs)


    def image_to_file(self, path=None):
        
        if path is None:
            if self._output_path is None:
                raise Exception('saving graph to file: no path provided')

            path=self._output_path
            
        fig, ax = plt.subplots()

        # TODO plot as points not line
        ax.plot(self._axis_tp, self._axis_price, 
            linewidth=self._settings['linewidth']
        )
        ax.set_title(self._settings['title'])
        ax.set_ylabel('Price')
        ax.set_xlabel('Timepoint')
        ax.locator_params(nbins=20)
        plt.grid(visible=True, linestyle='--', color='gray', linewidth=0.1)
        plt.savefig(path, dpi=self._settings['dpi'])

        self.reset()




"""
TODO listener multiplexing
Subscribers which use a parameter (currently just AGENT_ACTION, but in general, most likely 
any subscriber type aside from PRICE) will require multiplexing if we allow the same
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

    @staticmethod
    def load_points_file(path):
        with open(path, 'rb') as f:
            pts=json.load(f)
            assert(isinstance(pts, List))

            return pts

    class Listener():


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




    def __init__(self, i : Interface, config : SubscriberConfig, graph : Optional[Graph] = None):
        """
        get a SubscriberRecord (i.e. register the subscriber with the Market instance)
        create listener
        set up graph

        returns SubscriberRecord, SubscriberListener, and SubscriberGraph in a tuple,
        with each of these instances already configured to pass data to the others

        client needs to activate the listener (so that it passes data to the hook) and
        activate the graph (so that it appears on screen)
        """

        self._interface=i
        self._config=config
        self._logger=forcesim_logging.get_logger(f'Subscriber({config.type})')

        self._record_count_wait : Dict[int, Tuple[int, asyncio.Event]] = {}
        self._points : List = []

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
                    raise
            else:
                raise

        #(s_r)=ret.data[0]
        s_r=ret.data[0]
        self.record=s_r

        self._logger.info('successfully started subscriber (id=%s)', self.record.id)
        self._logger.debug('subscriber record=%s', self.record)

        await asyncio.create_task(self.listener._start_endpoint())

    async def delete(self):
        if hasattr(self, 'record'):
            await self._interface.del_subscribers([ self.record ])
            del(self.record)
        self.remove_graph()

    def points_to_file(self, path):
        with open(path, 'w') as f:
            json.dump(self._points, f)

        

    def add_points(self, points):
        #self._logger.debug(f'points: {points}')
        self._points.extend(points)

        for (count, evs) in self._record_count_wait.items():
            #print(f'{count} {len(self._points)}')
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
        if self.has_graph():
            del(self._graph)

    def render_graph(self, path=None):

        if self.has_graph():
            self._graph.add_points(self._points)
            self._graph.image_to_file(path)
        else:
            raise Exception('render_graph: no graph') 
    
    def has_graph(self):
        try:
            if isinstance(self._graph, Graph):
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


class Agent():
    def __init__(self, i : Interface, spec : AgentSpec):
        self._interface=i
        self._spec=spec
        self._logger=forcesim_logging.get_logger('Agent')
        self._id=None
    
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
            ret=await self._interface.delete_agents([
                agent._id for agent in self._agents
            ])
        except Interface.ErrorResponseException as e:
            for (_, error_code, data) in e.error.get_multiple():
                if error_code == error_code_t.Not_found:
                        self._logger.error(
                            'AgentSet.delete: agent ID does not exist in forcesim instance: {'
                        )
                else:
                    raise e
        except Interface.ResponseIntegrityException as e:
            pass


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



"""
Basic client class, intended to reduce boilerplate when implementing tests and other workflows
Maintain an Interface instance which is provided automatically to the classes above

Must be used with context manager `with` syntax
Currently, add_* methods must called before `start` - otherwise they won't take effect
"""
class Session():

    def __init__(self, interface_addr, interface_port, log, iter_block=100) -> None:
        self.interface=Interface(interface_addr, interface_port)
        self.log=log

        self.agents={}
        self.subscribers={}
        self.agentsets={}

        self.iter_block=iter_block

    # take actual objects - not config

    def add_agent(self, name, **kwargs):
        self.agents[name]=Agent(
            self.interface, **kwargs
        )
    def add_agentset(self, name, **kwargs):
        self.agentsets[name]=AgentSet(
            self.interface, **kwargs
        )
    def add_subscriber(self, name, **kwargs):
        self.subscribers[name]=Subscriber(
            self.interface, **kwargs
        )
        
    """
    """
    async def run(self, iterations=None):
        if iterations is None:
            iterations=self.iter_block

        self._completed_iter += iterations
        self.log.info(f'about to run {iterations} iterations (current total={self._completed_iter})')
        await self.interface.run(iterations)

        await self.interface.wait_for_pause()

        for name, s in self.subscribers.items():
            self.log.info(f'waiting for {iterations} records on subscriber {s.record.id}')
            await s.wait_record_count(self._completed_iter)

    """
    Add the objects to the forcesim instance
    should be called before `run`
    """
    async def start(self):
        for name, v in self.agentsets.items():
            await v.register()
            self.log.info(f'registered agentset (name={name})')

        for name, s in self.subscribers.items():
            print(f'starting subscriber (name={name})')
            await s.start()

        try:
            await self.interface.start()
        except Interface.ErrorResponseException as e:
            if e.error.code == error_code_t.Already_started:
                pass
            else:
                raise e

    async def __aenter__(self):

        # ensure we are configuring the instance starting from an empty/clean state
        self.log.info('resetting forcesim instance')
        await self.interface.reset()

        self.log.info(f'setting iter_block={self.iter_block}')
        await self.interface.configure(iter_block=self.iter_block)

        self._completed_iter=0

        return self


    """
    Remove all the objects from the forcesim instance
    """
    async def __aexit__(self, *args):

        for _, a in self.agents.items():
            await a.delete()

        for _, a in self.agentsets.items():
            await a.delete()

        for _, s in self.subscribers.items():
            await s.delete()
