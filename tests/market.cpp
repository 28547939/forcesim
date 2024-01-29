

#include "Market.h"
#include "Agent.h"
#include "Subscriber.h"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <deque>

#include <csignal>

#include "json_conversion.h"

#include <boost/asio.hpp>

/*
    Verbosity levels



*/


int main (int argc, char* argv[]) {
    FLAGS_stderrthreshold=0;
    FLAGS_logtostderr=1;
    FLAGS_v=9;

    google::InitGoogleLogging(argv[0]);

    std::shared_ptr<Market::Market> m(new Market::Market());

    m->configure({
        100
    });



    // TODO investigate the dual use of the price_t and record_type
    // needs to be a check to prevent inconsistency; also not clear if both are still needed

    auto factory = std::make_shared<Subscriber::Factory<price_t>>();
    Subscriber::Subscribers::add({
        { factory, {
            Subscriber::record_type::PRICE,
            {
                boost::asio::ip::make_address("192.168.22.34"),
                5000
            },
            1
        }}
    });

    Subscriber::Subscribers::manager_thread_poll_interval.store(500);

    auto t0 = std::thread([]() {
        Subscriber::Subscribers::launch_manager_thread(10);
    });

    auto t1 = std::thread([m]() {
        m->run(10);
        m->launch();
    });


    auto t2 = std::thread([m]() {

        auto agents = std::to_array({
            /*
            std::unique_ptr<Agent> (new TrivialAgent(AgentConfig<AgentType::Trivial> {{
                { "external_force", 0.0005 },
                { "internal_force", 100 },
                { "schedule_every", 1 },
                { "direction", "UP" }
            }})),
            std::unique_ptr<Agent> (new TrivialAgent(AgentConfig<AgentType::Trivial> {{
                { "external_force", 0.0005 },
                { "internal_force", 100 },
                { "schedule_every", 1 },
                { "direction", "DOWN" }
            }})),
            */

            std::unique_ptr<Agent> (new BasicNormalDistAgent(AgentConfig<AgentType::BasicNormalDist> {{
                { "mean", 0 },
                { "stddev", 10 },
                { "schedule_every", 2 },
                { "external_force", 0.01 }
            }})),
        });

        for (auto it = agents.begin(); it != agents.end(); it++) {
            auto agent = std::move(*it);

            std::shared_ptr<Market::op_abstract> op(
                new Market::op<Market::op_t::ADD_AGENT>(std::move(agent))
            );
            m->queue_op(op);
        }

    });


    t0.join();
    t1.join();
    t2.join();






//    m.add_agent(std::move(agent));


}