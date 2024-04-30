#ifndef AGENTFACTORY_H
#define AGENTFACTORY_H

#include "Agent.h"


namespace Agent {

// constructing Agent instances from data provided from the HTTP interface
// currently the JSON configuration is interpreted by the AgentConfig constructors
// (in most other situations, we use specific JSON conversion functions defined
// in json_conversion.cpp, automatically called by the JSON library)

using factory_map_t = 
    std::map<enum AgentType, std::function<std::unique_ptr<Agent>(json)>>;

template<enum AgentType T>
factory_map_t::value_type
factory_generator() {
    return {
        T,
        [](json y) {
            auto agent_ptr = new Agent_impl<T>(AgentConfig<T>(y));

            // this can be used to avoid errors with multiple inheritance in the case of an agent
            // class inheriting from other agent class, if virtual inheritance is not used
            // but this will create its own problems
            //auto agent_ptr_base = dynamic_cast<Agent_base<T>*>(agent_ptr);

            auto agent_ptr_base = dynamic_cast<Agent*>(agent_ptr);

            return std::unique_ptr<Agent>(agent_ptr_base);
        }
    };
}

inline factory_map_t factory {
    factory_generator<AgentType::Trivial>(),
    factory_generator<AgentType::BasicNormalDist>(),
    factory_generator<AgentType::ModeledCohort_v1>(),
    factory_generator<AgentType::ModeledCohort_v2>()

    /* each invocation of factory_generator produces something like the following:
    { "TrivialAgent", 
        [](json y) {
            return unique_ptr<Agent>(
                new TrivialAgent (AgentConfig<AgentType::Trivial>(y))
            );
        }
    },
    */
};

};

#endif