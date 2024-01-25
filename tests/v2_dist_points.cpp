

#include "../types.h"
#include "../Agent.h"
#include "../ts.h"
#include "../json_conversion.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <memory>
#include <variant>

#include <boost/program_options.hpp>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// given an Agent (ModeledCohortAgent_v2) configuration (present in some JSON file), instantiate 
// the agent and output the results of compute_distribution_points, including trace data,
// providing the agent with a current price and price view

void print_agentaction(price_t p, AgentAction a) {
	std::cout 
		<< "price=" << std::to_string(static_cast<double>(p))
		<< " direction=" 
		<< (a.direction == Direction::UP ? "UP" : "DOWN")
		<< " internal_force=" << std::to_string(a.internal_force)
		<< "\n"
	;
}

void print_distribution(std::shared_ptr<ModeledCohortAgent_v2> a, price_t p) {
    auto pts = a->compute_distribution_points(p);

    std::ostringstream s;
    for (auto x = std::get<0>(pts).begin(), y = std::get<1>(pts).begin(); 
            x != std::get<0>(pts).end(); ++x, ++y) 
    {
        s << "(" + std::to_string(*x) +", "+ std::to_string(*y) +")\n";
    }

    std::cout << s.str();
}

ModeledCohortAgent_v2 agent_from_file(std::string path, std::string agent_key) {
    std::ifstream f(path, std::ios::binary);
    json agent_config_list_json;
    f >> agent_config_list_json;

    json agent_config_json = agent_config_list_json[agent_key];

    AgentConfig<AgentType::ModeledCohort_v2> agent_config(agent_config_json);
    return ModeledCohortAgent_v2(agent_config);
}



namespace po = boost::program_options;

int main(int argc, char* argv[]) {


    po::options_description desc("Options");
    desc.add_options()
        ("agent-config-path", po::value<std::string>(), "")
        ("agent-config-key", po::value<std::string>(), "")
        ("price-view", po::value<double>(), "")
        ("current-price", po::value<double>(), "")
        ("subjectivity-extent", po::value<float>(), "")
    ;
    
    json agent_config;

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);

        auto path = vm["agent-config-path"].as<std::string>();
        auto key = vm["agent-config-key"].as<std::string>();

        po::notify(vm);

        auto agent = agent_from_file(path, key);

        auto agent_config = agent.Agent_base<AgentType::ModeledCohort_v2>::config();

        agent.set_price_view(
            price_t(vm["price-view"].as<double>())
        );

        std::map<std::string, double> parameters = {
            { "e_0", agent_config.e_0 },
            { "i_0", agent_config.i_0 },
            { "r_0", agent_config.r_0 },
            { "r_1", agent_config.r_1 },
            { "r_2", agent_config.r_2 },
            { "i_1", agent_config.i_1 },
            { "i_2", agent_config.i_2 }
        };

        auto points = agent.compute_distribution_points(
            price_t(vm["current-price"].as<double>()),
            vm["subjectivity-extent"].as<float>(),
            true
        );

        json outjson({
            std::get<0>(points),
            std::get<1>(points),
            std::get<2>(points).value(),
            parameters
        });

        std::ostringstream outstr;
        outstr << outjson;

        std::cout << outstr.str();
        
    } catch (po::error& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
