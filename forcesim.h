
#include "Market.h"
#include "Agent/Agent.h"
#include "Subscriber/Subscriber.h"
#include "Subscriber/Subscribers.h"
#include "Subscriber/Factory.h"
#include "Interface.h"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <deque>
#include <csignal>
#include <functional>
#include <thread>

#include "json_conversion.h"
#include "Subscriber/json_conversion.h"

#include <asio.hpp>
#include <boost/program_options.hpp>

/*
#ifdef __FreeBSD__
#include <sys/thr.h>
#endif
#ifdef __linux__
#include <pthread.h>
#endif
*/


namespace {
    volatile std::atomic<bool> shutdown_signal;
}
void signal_handler (int s) {
    if (s == SIGINT || s == SIGTERM) {
        shutdown_signal.store(true);
    }
};



enum class forcesim_component {
    Market,
    Subscribers,
    Interface
};
std::string forcesim_component_str(enum forcesim_component c) {
    switch (c) {
        case forcesim_component::Market:
            return "Market";
        break;
        case forcesim_component::Subscribers:
            return "Subscribers";
        break;
        case forcesim_component::Interface:
            return "Interface";
        break;

        return "";
    }
}

namespace po = boost::program_options;

// a simple "framework" designed to reduce boilerplate involved in the creation of CLI C++ clients
// since the general intention is that clients access a forcesim instance via the HTTP
//  interface using Python, this will mostly be useful for writing lower-level tests

// testing:
// set loglevel to error to view any test-related problems (or other problems that
//  are logged by the simulator libraries)
// => verbose loglevel is intended to help diagnose any errors
// TODO currently, glog is not recognizing the --v flag properly, so using --glog-verbosity instead

using fc = forcesim_component;


struct forcesim_client {
    std::map<enum forcesim_component, std::thread> threads;

    std::deque<Market::agentid_t> agents;
    std::deque<Subscriber::id_t> subscribers;

    // available to the client program to make changes before parse_cli is called
    po::options_description options_desc;
    po::variables_map options_vm;

    std::optional<std::shared_ptr<Market::Market>> market;
    std::optional<std::shared_ptr<Interface>> interface;

    const std::set<enum forcesim_component> components;

    //std::string proctitle;

    forcesim_client(std::set<enum forcesim_component> _components) 
        :   options_desc("forcesim CLI options"),
            components(_components)
    
    {
        FLAGS_logtostderr = 1;
        google::InstallFailureSignalHandler();

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        this->options_desc.add_options()
            ("help", "...")
            ("interface-address", po::value<std::string>()->default_value("127.0.0.1"), "")
            ("interface-port", po::value<int>()->default_value(18080), "")
            ("iter-block", po::value<int>()->default_value(1000), "")
            ("subscriber-poll-interval", po::value<int>()->default_value(5000), "")
            ("subscriber-max-records", po::value<int>()->default_value(100), "")
            ("glog-verbosity", po::value<int>()->default_value(0), "")
        ;


        if (components.contains(forcesim_component::Market)) {
            this->market = std::make_shared<Market::Market>();
        }
    }

    // parse_cli must be called before run
    void
    parse_cli(int argc, const char* const argv[]) {
        google::InitGoogleLogging(argv[0]);

        //this->proctitle = argv[0];
        //for (int i = 1; i < argc; i++) {
        //    this->proctitle += " ";
        //    this->proctitle += argv[i];
        //}

        try {
            po::parsed_options opt = po::command_line_parser(argc, argv)
                .options(this->options_desc)
                .allow_unregistered()
                .run()
            ;
            po::store(opt, this->options_vm);

            if (this->options_vm.count("help")) {
                std::cout << this->options_desc << std::endl;
            }

            if (this->options_vm.count("glog-verbosity")) {
                FLAGS_v=this->options_vm["glog-verbosity"].as<int>();
            }

            po::notify(this->options_vm);
        } catch (po::error& e) {
            LOG(FATAL) << e.what() 
                << std::endl
                << this->options_desc
            ;
        }
    }

    template<enum Agent::AgentType T>
    Market::agentid_t add_agent(json j) {
        auto agent_ptr = Agent::factory_generator<T>().second(j);

        if (this->market.has_value()) {
            auto market = this->market.value();
            return market->add_agent(std::move(agent_ptr));
        } else {
            LOG(FATAL) << "add_agent called when Market not initialized";
        }
    }

    template<typename RecordType>
    Subscriber::id_t 
    add_subscriber(
        std::string addr, int port, 
        Subscriber::FactoryParameter<RecordType> param,
        uintmax_t granularity = 1, uintmax_t chunk_min_records = 1
    ) 
    {
        auto factory = std::make_shared<Subscriber::Factory<RecordType>>(param);
        auto ret = Subscriber::Subscribers::add(
            factory, Subscriber::Config {
                Subscriber::constraint<RecordType>::t,
                Subscriber::EndpointConfig {
                    addr, port
                },
                granularity,
                chunk_min_records
            }
        );

        try {
            return std::get<Subscriber::id_t>(ret);
        } catch (std::bad_variant_access&) {
            LOG(ERROR) 
                << "forcesim_client::add_subscriber failed: "
                << std::get<std::string>(ret)
            ;
        }
    }

    // parse_cli must be called before run
    void run() {

        for (auto c : this->components) {
            if (this->threads.find(c) != this->threads.end()) {
                LOG(FATAL) << "Thread for " 
                    << forcesim_component_str(c) << " already exists" << std::endl;
            }
        }

        if (components.contains(forcesim_component::Market)) {
            auto market_v = this->market.value();
            this->threads[fc::Market] = market_v->launch();
            //this->proctitle(this->threads[fc::Market], fc::Market)


            market_v->configure({
                this->options_vm["iter-block"].as<int>()
            });

        }

        if (components.contains(fc::Subscribers)) {

            Subscriber::Subscribers::manager_thread_poll_interval.store(
                this->options_vm["subscriber-poll-interval"].as<int>()
            );

            this->threads[fc::Subscribers] = 
                std::thread([this]() {

                    Subscriber::Subscribers::launch_manager_thread(
                        this->options_vm["subscriber-max-records"].as<int>()
                    );

                    VLOG(5) << "Subscriber manager thread exited";
                })
            ;
            // currently not going to bother with this
            //this->thread_title(this->threads[fc::Subscribers], fc::Subscribers);
        }

        if (components.contains(fc::Interface)) {

            if (this->market.has_value()) {


                asio::ip::address listen_addr = asio::ip::address::from_string(
                    this->options_vm["interface-address"].as<std::string>()
                );

                int listen_port = this->options_vm["interface-port"].as<int>();


                this->threads[fc::Interface] = std::thread([this, listen_addr, listen_port]() {
                    auto i = Interface::get_instance(this->market.value());
                    this->interface = i;

                    i->start(listen_addr, listen_port);
                    VLOG(5) << "Interface thread exiting";
                });
            } else {
                LOG(ERROR) << "Interface component requires Market component";
            }

        }

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (shutdown_signal == true) {    
                if (this->threads.contains(fc::Subscribers)) {
                    Subscriber::Subscribers::shutdown(std::move(this->threads[fc::Subscribers]));
                    this->threads.erase(fc::Subscribers);
                }
                if (this->interface.has_value()) {
                    this->interface.value()->stop();
                }
                if (this->market.has_value()) {
                    this->market.value()->queue_op(
                        std::shared_ptr<Market::op<Market::op_t::SHUTDOWN>> { 
                            new Market::op<Market::op_t::SHUTDOWN> {} 
                        }
                    );
                }
                LOG(INFO) << "Shutdown complete, waiting for threads\n";
                break;
            }
        }

        for (auto& [component, thr] : this->threads) {
            thr.join();
            LOG(INFO) << "component thread exited: " << forcesim_component_str(component);
        }

        VLOG(2) << "exiting\n";
    }

    // trigger client shutdown just as if it were sent SIGINT/SIGTERM
    void shutdown() {
        shutdown_signal.store(true);
    }

    void exit(int code) {
        std::exit(code);
    }

};
