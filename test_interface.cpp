

#include "Market.h"
#include "Agent.h"
#include "Subscriber.h"
#include "Interface.h"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <deque>

#include "json_conversion.h"

#include <boost/asio.hpp>

/*
    Verbosity levels



*/


// TODO cppreference.com
/*
*/
/*
void signal_handler(int s) {
    signal = s;
}
    */


/*
    CROW_ROUTE(this->crow_app, "/add/<int>/<string>")
    .methods("POST"_method)
    ([](int a, std::string b, crow::request& req) {
    });
    */
/*
        switch (rv["type"]) {
            case "TrivialAgent":

            break;
            default:

            break;
        }
        */

/*        auto wv = crow::json::wvalue(rv);
        return crow::response(
            (wv.dump())
        );
    });
        return crow::response(
            config_list.dump()
        );

        */

namespace {
    volatile std::sig_atomic_t signal_status;
}
void signal_handler (int s) {
    signal_status = s;
    Subscriber::Subscribers::shutdown_signal.store(true);
    Market::Market::shutdown_signal.store(true);

    // TODO need to send a stop OP to market

};

void sig_fpe(int s) {
    printf("caught floating-point exception");
}

int main (int argc, char* argv[]) {
    FLAGS_stderrthreshold=0;
    FLAGS_logtostderr=1;
    FLAGS_v=9;

    google::InitGoogleLogging(argv[0]);

    std::shared_ptr<Market::Market> m(new Market::Market());
    auto t0 = m->launch();

    m->configure({
        1000
    });

    if (std::signal(SIGINT, signal_handler) == SIG_ERR) {
        printf("SIG_ERR\n");
    }

    if (std::signal(SIGFPE, signal_handler) == SIG_ERR) {
        printf("SIG_ERR\n");
    }

    // TODO investigate the dual use of the price_t and record_type
    // needs to be a check to prevent inconsistency; also not clear if both are still needed

/*
    auto factory = Interface::subscriber_factory_factory[Subscriber::record_type::PRICE]
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
    */

    Subscriber::Subscribers::manager_thread_poll_interval.store(5000);

    auto t1 = std::thread([]() {
        Subscriber::Subscribers::launch_manager_thread(100);
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

            m->add_agent(std::move(agent));
/*
            std::shared_ptr<Market::op_abstract> op(
                new Market::op<Market::op_t::ADD_AGENT>(std::move(agent))
            );
            m->queue_op(op);
            */
        }

/*
        m->run();
        */

    });

    auto t3 = std::thread([m]() {
        auto i = Interface::get_instance(m);

        i->start();
    });


    t0.join();
    t1.join();
    t2.join();
    t3.join();


// TODO respond to signals (kill)


//    m.add_agent(std::move(agent));


}