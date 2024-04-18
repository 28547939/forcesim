

#include "Market.h"
#include "Agent/Agent.h"
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

#include <asio.hpp>
#include <boost/program_options.hpp>


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
            ("subscriber-max-records", po::value<int>()->default_value(1000), "")
            ("glog-verbosity", po::value<int>()->default_value(0), "")
        ;


        if (components.contains(forcesim_component::Market)) {
            this->market = std::make_shared<Market::Market>();
        }
    }

    void
    parse_cli(int argc, const char* const argv[]) {
        google::InitGoogleLogging(argv[0]);

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

    void run() {

        for (auto c : this->components) {
            if (this->threads.find(c) != this->threads.end()) {
                LOG(ERROR) << "Thread for " 
                    << forcesim_component_str(c) << " already exists" << std::endl;
            }
        }

        if (components.contains(forcesim_component::Market)) {

            this->market = std::make_shared<Market::Market>();
            auto market_v = this->market.value();
            market_v->launch();

            market_v->configure({
                this->options_vm["iter-block"].as<int>()
            });

        }

        if (components.contains(forcesim_component::Subscribers)) {

            Subscriber::Subscribers::manager_thread_poll_interval.store(
                this->options_vm["subscriber-poll-interval"].as<int>()
            );

            this->threads.insert({ 
                forcesim_component::Subscribers, 
                std::thread([this]() {

                    Subscriber::Subscribers::launch_manager_thread(
                        this->options_vm["subscriber-max-records"].as<int>()
                    );

                    VLOG(5) << "Subscriber manager thread exiting";
                })
            });
        }

        if (components.contains(forcesim_component::Interface)) {

            if (this->market.has_value()) {


                asio::ip::address listen_addr = asio::ip::address::from_string(
                    this->options_vm["interface-address"].as<std::string>()
                );

                int listen_port = this->options_vm["interface-port"].as<int>();


                auto t2 = std::thread([this, listen_addr, listen_port]() {
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
                Subscriber::Subscribers::shutdown_signal.store(true);

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


                LOG(INFO) << "Signal received, shutting down\n";
                break;
            }
        }

        for (auto& [component, thr] : this->threads) {
            thr.join();
        }

        VLOG(2) << "exiting\n";
    }

    void exit(int code) {
        exit(code);
    }

};
