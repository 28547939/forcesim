

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




int main (int argc, char* argv[]) {
    FLAGS_stderrthreshold=0;
    FLAGS_logtostderr=1;
    FLAGS_v=9;

    google::InitGoogleLogging(argv[0]);

    std::shared_ptr<Market::Market> m(new Market::Market());

    m->configure({
        100
    });



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

        auto agents = std::to_array({
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



//    m.add_agent(std::move(agent));


}