

#include "../Market.h"
#include "../Agent/Agent.h"
#include "../Subscriber/Subscriber.h"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <deque>

#include <csignal>

#include "../forcesim.h"
#include "../json_conversion.h"

#include "common.h"

#include <boost/asio.hpp>





/*
special purpose agent used to test internal Market state relating to info_history, info cursor,
info views provided to agents, etc
*/
struct info_test_history {
    // track parameters and contents of the info_view provided to the Agent
    std::deque<std::optional<Info::infoset_t>> info_history;
};
struct info_test_agent : public Agent::Agent {
    std::shared_ptr<info_test_history> info_history;
    
    info_test_agent(std::shared_ptr<info_test_history> h) 
    :   Agent::Agent(::Agent::AgentConfigBase()), info_history(h) 
    {}

    public:
    ::Agent::AgentAction do_evaluate(price_t p) {

        auto infoset = this->read_next_infoset();
        this->info_history->info_history.push_back(infoset);

        return ::Agent::AgentAction {
            Direction::UP,
            0
        };
    }
};
// 


void print_infohistory(info_test_history& h, std::optional<Market::agentid_t> id) {
    std::string idstr;
    if (id.has_value()) {
        idstr = std::string("agent " + id.value().to_string());
    } else {
        idstr = "reference";
    }
    std::cout << idstr << " history\n";


    std::ostringstream s;
    int i = 0;
    for (auto& infoset : h.info_history) {
        std::cout << i << ": ";
        if (infoset.has_value()) {
            for (auto& i : infoset.value()) {
                auto casted = Info::get_cast_throws<Info::Types::Test1>(i);
                std::cout << std::to_string(casted->item1) << " ";
            }
        } else {
            std::cout << "null";
        }
        std::cout << "\n";
        i++;
    }
}


int main (int argc, char* argv[]) {
    forcesim_client client ({ forcesim_component::Market });

    client.options_desc.add_options()
        ("test-count", po::value<int>()->default_value(5), "")

        // the likelihood that a given iterblock will have an Infoset submitted
        ("info-probability", po::value<float>()->default_value(0.8), "")

        // the following parameters are chosen randomly, up to the given max


        // number of agents to receive Info objects
        ("agent-count-max", po::value<int>()->default_value(1), "")

        // number of Info objects to be contained in each submitted Infoset
        ("infoset-size-max", po::value<int>()->default_value(1), "")

        // at most one Infoset is submitted for each iteration block
        ("iterblock-count-max", po::value<int>()->default_value(5), "")
        
        // maximum size of iteration blocks (i.e. to occur between Infoset submissions)
        ("iter-max", po::value<int>()->default_value(5), "")

    ;
    client.parse_cli(argc, argv);
    auto test_count = client.options_vm["test-count"].as<int>();
    auto agent_count_max = client.options_vm["agent-count-max"].as<int>();
    auto infoset_size_max = client.options_vm["infoset-size-max"].as<int>();
    auto iterblock_count_max = client.options_vm["iterblock-count-max"].as<int>();
    auto iter_max = client.options_vm["iter-max"].as<int>();
    auto info_probability = client.options_vm["info-probability"].as<float>();
    auto m = client.market.value();

    std::thread client_thread([m, &client]{
        m->start();
        client.run();

    });

    // TODO 
    test_count = 1;

    // Market will wait on the op_queue after starting
    //m->start();

    std::cout << "test_count=" << std::to_string(test_count) << "\n";


    for (int test_i = 0; test_i < test_count; test_i++) {

        std::cout << "starting test " << std::to_string(test_i) << "\n";
        int agent_count = rand(agent_count_max);
        std::cout << "agent_count=" << std::to_string(agent_count) << "\n";

        m->reset();

        // compare each agent's history against our reference
        info_test_history reference_info_history;

        // agents will modify these info_test_history objects
        std::map<Market::agentid_t, std::shared_ptr<info_test_history>> 
        agent_info_history;

        for (int i = 0; i < agent_count; i++) {
            auto hptr = std::make_shared<info_test_history>();

            std::unique_ptr<Agent::Agent> agent(
                new info_test_agent(hptr)
            );

            auto id = m->add_agent(std::move(agent));

            agent_info_history[id] = hptr;
        }

        timepoint_t total_iter;

        int iterblock_count = rand(iterblock_count_max);
        std::cout << "iterblock_count=" << std::to_string(iterblock_count) << "\n";

        for (int i = 0; i < iterblock_count; i++) {

            int iter = rand(iter_max);
            std::cout << "iter=" << std::to_string(iter) << "\n";

            if (randtf(info_probability)) {
                Info::infoset_t is;
                for (int j = 0; j < rand(infoset_size_max); j++) {
                    auto info_ptr = std::make_shared<Info::Info<Info::Types::Test1>>();
                    info_ptr->item1 = frand(1);

                    is.insert(is.end(), std::shared_ptr<Info::Abstract>(info_ptr));
                }

                m->emit_info(is);
                reference_info_history.info_history.push_back(is);
            } else {
                reference_info_history.info_history.push_back(std::nullopt);
            }

            total_iter += iter;
            std::shared_ptr<Market::op_abstract> op(
                new Market::op<Market::op_t::RUN>(iter)
            );
            m->queue_op(op);

            // no info is emitted during the iteration - not possible with current Market implementation
            for (int j = 0; j < iter-1; j++) {
                reference_info_history.info_history.push_back(std::nullopt);
            }

            m->wait_for_pause(total_iter, true);
        }

        auto& ref_history = reference_info_history.info_history;
        auto rsize = ref_history.size();
        std::cout << "reference history size: " << rsize << "\n";

        print_infohistory(reference_info_history, std::nullopt);
        std::cout << "\n\n";

        for (auto [id, hptr] : agent_info_history) {
            auto hsize = hptr->info_history.size();
            std::cout 
                << "agent_info_history[" << id.to_string() 
                << "] size: " << hsize << "\n";


            if (hsize != rsize) {
                std::cout << "sizes differ\n";
                print_infohistory(*hptr, id);
                std::cout << "\n\n";
                continue;
            }


            print_infohistory(*hptr, id);
            for (int i = 0; i < ref_history.size(); i++) {
                if (hptr->info_history.at(i) != ref_history.at(i)) {
                    std::cout << "not equal at " << i << "\n";

                    print_infohistory(*hptr, id);
                    std::cout << "\n\n";
                    continue;
                }

            }
        }
    }

    client_thread.join();
}



/*
*/

/*
void test_opqueue(std::shared_ptr<Market::Market> m) {
    m->reset();
    // START has already been tested by main()

    // TODO - if this test is needed, move it to a separate file

    
    std::unique_ptr<Agent::Agent> agent(
        new Agent::BasicNormalDistAgent(Agent::AgentConfig<Agent::AgentType::BasicNormalDist> {{
            { "mean", 0 },
            { "stddev", 1 },
            { "schedule_every", 1 },
            { "external_force", 0.01 }
        }}
    ));

    std::shared_ptr<Market::op_abstract> op(
        new Market::op<Market::op_t::ADD_AGENT>(std::move(agent))
    );
    m->queue_op(op);

}
*/

