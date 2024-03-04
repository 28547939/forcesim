import logging
import argparse

from datetime import datetime

import matplotlib.pyplot as plt

import os 

import asyncio

import logging
import forcesim.logging

# needs to be called before any modules (such as util) set up logging at module scope 
forcesim.logging.add_default_handlers([
    logging.StreamHandler()
])


from forcesim.api_types import *
from forcesim.classes import *
import forcesim.util as util





"""
The 'basic' command-line tester follows a basic format for configuring and running the 
forcesim instance. Configuration specifies the Agents and Subscribers to add, as well as
other options.
The instance is set to run for a specific number of iteration blocks; between each block, 
Info structures are sent into the market as specified. 
"""



async def main():

    log=forcesim.logging.get_logger('basic-py')

    prs=argparse.ArgumentParser(
        prog='',
        description='',
    )

    prs.add_argument('--instance-addr', default='127.0.0.1')
    prs.add_argument('--instance-port', default='18080')
    prs.add_argument('--agents-json-dir', required=True)
    prs.add_argument('--subscribers-json-dir', required=True)
    prs.add_argument('--info-json-dir', default=None)
    prs.add_argument('--config-yaml', required=True)

    args=vars(prs.parse_args())

    print(args)

    interface = Interface(args['instance_addr'] , args['instance_port'])

    agents_json_dir=args['agents_json_dir']
    subscribers_json_dir=args['subscribers_json_dir']
    info_json_dir=args['info_json_dir']
    config_path=args['config_yaml']

    agent_spec = util.load_json_recursive(agents_json_dir, util.AgentLoader, verbose=True)
    subscriber_config = util.load_json_recursive(subscribers_json_dir, util.SubscriberLoader, verbose=True)
    info_spec = util.load_json_recursive(info_json_dir, util.InfoLoader, verbose=True)

    config=util.load_config_yaml(config_path)


    # ensure we are configuring the instance starting from an empty/clean state
    log.info('resetting forcesim instance')
    await interface.reset()

    iter_block=config.iter_block_size if config.iter_block_size else 100
    log.info(f'setting iter_block={iter_block}')
    await interface.configure(iter_block=iter_block)

    info_sequence=config.info_sequence

    # ensure early on that we can write into the graph output directory
    # TODO
    datestr=datetime.now().isoformat()
    output_dir_path=os.path.join(
        config.output_dir, datestr
    )

    try:
        os.makedirs(output_dir_path)
    except FileExistsError:
        if os.path.isdir(output_dir_path):
            pass
        else:
            raise Exception(f'output_dir {output_dir_path} exists already and is not a directory')
    

    agroup={}
    subscribers={}

    # subscriber name   =>   list of subscriber parameters (agent IDs)
    pending_agent_subscribers = {}

    for (name, count) in config.agents.items():
        if name in agent_spec:
            (spec, subscriber_name)=agent_spec[name]

            agroup[name]=AgentSet(
                interface=interface,
                spec=spec,
                count=count
            )

            await agroup[name].register()
            log.info(f'registered AgentSet (name={name})')

            if isinstance(subscriber_name, str):
                raise NotImplementedError('')

                #pending_agent_subscribers[subscriber_name] = [
                #    a._record.id for a in agroup[name]
                #]


        else:
            print(f'skipping agent {name} - not loaded')

    async def create_subscriber(name):
        sconfig=subscriber_config[name]
        if not sconfig:
            raise Exception(f'create_subscriber called for {name} but not set in subscriber_config')

        parameter=None
        graph=None
        if config.subscribers[name]['graph']:

            graph=Graph(output_path=os.path.join(
                output_dir_path, name+'.png'
            ))

        if (sconfig.type == subscriber_type_t.AGENT_ACTION 
            and sconfig.parameter is None):

            raise Exception(
                f'AGENT_ACTION subscriber must have an agent ID parameter '
                + 'unless it is referenced by agent(s)'
            )

        subscribers[name] = Subscriber(
            i=interface,
            config=sconfig,
            graph=graph,
        )

        await subscribers[name].start()
        print(f'started subscriber (name={name})')


    for name in config.subscribers:

        if name in pending_agent_subscribers:
            agent_ids=pending_agent_subscribers[name]
            raise NotImplementedError('')
            
            #if subscriber_config[name].type != subscriber_type_t.AGENT_ACTION:
            #    raise Exception(
            #        f'agents specified subscriber {name}, but this '
            #        + 'subscriber is not an AGENT_ACTION subscriber'
            #    )

            #if not subscriber_config[name].parameter is None:
            #    raise Exception(
            #        f'agents specified subscriber {name}, but this '
            #        + 'subscriber already has an agent ID parameter'
            #    )
            
            ## create multiple identical subscribers with distinct agent ID parameters

            #for agent_id in agent_ids:
            #    subscriber_config[name].parameter=agent_id
            #    create_subscriber(name)

        else:
            log.info(f'created subscriber (name={name})')
            await create_subscriber(name)

    
    try:
        await interface.start()
    except Interface.ErrorResponseException as e:
        if e.error.code == error_code_t.Already_started:
            pass
        else:
            raise e

    for info_list in info_sequence:

        info_obj=[]
        for info_name in info_list:
            try:
                info_obj.append(info_spec[info_name])
            except KeyError as e:
                log.warning(f'info object {info_name} specified in info_sequence was not found - skipping')

        if len(info_obj) > 0:
            await interface.emit_info(info_obj)

        log.info(f'about to run {iter_block} iterations')
        await interface.run(iter_block)
        await interface.wait_for_stop()



    # python 3.11
    #async with asyncio.TaskGroup() as tg:
    #    for (_, s) in subscribers.items():
    #        tg.create_task(s.listener.wait_flushed()) 

    log.info('waiting for subscribers')


    # some Subscriber points can arrive after the initial flush - wait for stopped market first
    await interface.wait_for_stop()
    for (_, s) in subscribers.items():
        await s.wait_flushed()
    
    record_count=iter_block * len(info_sequence)
    log.info(f'waiting for {record_count} records')
    await s.wait_record_count(record_count)

    log.info('outputting graphs')

    for (_, s) in subscribers.items():
        s.render_graph()
        s.points_to_file(os.path.join(
            output_dir_path, name+'.json'
        ))



"""
2023-10-20 TODO
subscription option in agent config -> coordinate here
(for AGENT_ACTION subscribers - not yet implemented)
"""


if __name__ == '__main__':
    loop=asyncio.get_event_loop()
    try:
        loop.run_until_complete(main())
    finally:
        loop.close()
