
#include "../Agent.h"
#include "../types.h"
#include "../Info.h"

#include <optional>
#include <fstream>

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
    for (auto x = std::get<0>(pts).begin(), y = std::get<1>(pts).begin(); x != std::get<0>(pts).end(); ++x, ++y) {
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

std::shared_ptr<Info::Abstract> info_from_file(std::string path, std::string key) {
    std::ifstream f(path, std::ios::binary);
    json info_map_json;
    f >> info_map_json;

    json info_json = info_map_json[key];
    return info_json.get<std::shared_ptr<Info::Abstract>>();
}