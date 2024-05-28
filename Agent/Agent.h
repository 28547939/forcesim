#ifndef AGENT_H
#define AGENT_H 1

#include <stdio.h>
#include <memory>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "../Info.h"
#include "../types.h"
#include "../ts.h"

using json = nlohmann::json;

#include <random>

namespace Agent {

typedef std::unique_ptr<ts<Info::infoset_t>::sparse_view> info_view_t;

const int MAX_INTERNAL_FORCE = 100;

/* investment action taken by each Agent */
struct AgentAction {
    enum Direction direction;

    /* must be in [0,100] */
    long double internal_force;
};

enum class AgentType { 
    // unimplemented
    Determinstic,
    // first version/attempt for investigation
    ModeledCohort_v1,
    // second attempt for investigation
    ModeledCohort_v2,
    // testing: takes the same exact action every time (as configured)
    Trivial,
    // testing: takes action according to a normal distribution with
    BasicNormalDist
};

static std::map<std::string, enum AgentType> str_agenttype = {
    { "TrivialAgent", AgentType::Trivial },
    { "BasicNormalDistAgent", AgentType::BasicNormalDist },
    { "ModeledCohortAgent_v1", AgentType::ModeledCohort_v1 },
    { "ModeledCohortAgent_v2", AgentType::ModeledCohort_v2 }
};

// for now, just generate it anew every time - not a performance-sensitive operation
inline std::string agenttype_str(enum AgentType t) {
    std::map<enum AgentType, std::string> x;

    std::transform(str_agenttype.begin(), str_agenttype.end(), 
    std::inserter(x, x.end()),
        [](auto& p) -> std::pair<enum AgentType, std::string> {
            return { p.second, p.first };
        }
    );

    return x.find(t)->second;
}



struct AgentConfigBase {
    // in (0, 1]
    float external_force;
    int schedule_every;

    AgentConfigBase(json c) :
        external_force(c.at("external_force")),
        schedule_every(c.at("schedule_every")) 
    {
        if (this->schedule_every <= 0) {
            throw std::invalid_argument("AgentConfig: schedule_every must be > 0");
        }

        if (this->external_force <= 0) {
            throw std::invalid_argument("AgentConfig: external_force must be > 0");
        }
    }
};
template<enum AgentType T>
struct AgentConfig : AgentConfigBase {
    AgentConfig(json);
};


// abstract base class
class Agent {
    private:

    // temporary object passed to the Agent at every Agent::evaluate invocation
    // info_view is nullopt  iff  Market::info_history is empty
    std::optional<info_view_t> info_view;

    // timepoint of the most recently read info entry
    // an empty _info_cursor indicates the agent has not read any Market::info_history entries
    std::optional<timepoint_t> _info_cursor;

    protected:
    Agent(AgentConfigBase c) : config(c) {}

    std::optional<Info::infoset_t>
    read_next_infoset() {

        if (this->info_view.has_value()) {
            auto& info_view_ptr = this->info_view.value();

            auto [first,last] = info_view_ptr->bounds();

            if (this->_info_cursor < last || ! this->_info_cursor) {

                // read() does not increment the internal pointer (cursor) of the view
                this->_info_cursor = info_view_ptr->cursor();
                auto v = info_view_ptr->read();
                (*info_view_ptr)++;
                return v;
            } 
            
            // either Market::info_history is empty, or there are currently no further
            // non-empty entries
            else {
                return std::nullopt;
            }
        } 
        
        // info_view was not provided to the agent (eg Market::info_history not initialized yet)
        else {
            return std::nullopt;
        }
    }

    public:
    virtual ~Agent() {}
    //const static enum AgentType t;
    const AgentConfigBase config;

    const std::optional<timepoint_t>& 
    info_cursor() { return this->_info_cursor; }

    std::pair<std::optional<AgentAction>, std::optional<info_view_t>>
    evaluate(price_t p, std::optional<info_view_t> info_view) {
        this->info_view = std::move(info_view);

        try {
            AgentAction act = this->do_evaluate(p);
            return { act, std::move(this->info_view) };
        // generally the Agent should handle its own exceptions, but we include this here in case
        } catch (...) {
            LOG(ERROR) << "exception encountered during agent do_evaluate";
            return { std::nullopt, std::move(this->info_view) };
        }
    }

    // implemented by specific Agent classes
    virtual AgentAction do_evaluate(price_t p) = 0;
};


// agent mixin to handle T-type config
template<enum AgentType T>
class Agent_base : public virtual Agent {
    protected:
    AgentConfig<T> _config;

    public:
    virtual AgentConfig<T> config() final { return this->_config; }

    Agent_base(AgentConfig<T> c) : _config(c), Agent(c) {}
    ~Agent_base() = default;
};



template<enum AgentType>
class Agent_impl;

// JSON object access exceptions should be caught by the client
// generally this is in Interface.cpp


template<>
struct AgentConfig<AgentType::BasicNormalDist> : AgentConfigBase {
    AgentConfig(json config) : 
        AgentConfigBase(config),
        mean(config["mean"]),
        stddev(config["stddev"]) 
    {}

    double stddev;
    double mean;
};
template<>
class Agent_impl<AgentType::BasicNormalDist> : public Agent_base<AgentType::BasicNormalDist> {
    protected:

    public:
    Agent_impl(AgentConfig<AgentType::BasicNormalDist>);
    ~Agent_impl() {}
    AgentAction do_evaluate(price_t p);

    std::mt19937 engine;
    std::normal_distribution<float> dist;
};
using BasicNormalDistAgent = Agent_impl<AgentType::BasicNormalDist>;



template<>
struct AgentConfig<AgentType::Trivial> : AgentConfigBase {
    AgentConfig(json config) : 
        AgentConfigBase(config),
        direction(direction_str_ctor(config["direction"])), 
        internal_force(config["internal_force"]) 
    {}

    enum Direction direction;
    float internal_force;
};
template<>
class Agent_impl<AgentType::Trivial> : public Agent_base<AgentType::Trivial> {
    protected:

    public:
    Agent_impl(AgentConfig<AgentType::Trivial>);
    ~Agent_impl() {}
    AgentAction do_evaluate(price_t p);
};
using TrivialAgent = Agent_impl<AgentType::Trivial>;



template<>
struct AgentConfig<AgentType::ModeledCohort_v1> : AgentConfigBase {
    AgentConfig(json config) : 
        AgentConfigBase(config),

        // no longer used
//        initial_variance(config["initial_variance"]),  
        variance_multiplier(config["variance_multiplier"]),
        force_threshold(config["force_threshold"])
    {

        try {
            double x = config.at("default_price_view");
            this->default_price_view = x;
        } catch (json::out_of_range& e) {
            this->default_price_view = 1;
        }

    }

    double initial_variance = 0;
    double variance_multiplier;
    double force_threshold;
    price_t default_price_view;
};
template<>
class Agent_impl<AgentType::ModeledCohort_v1> : public Agent_base<AgentType::ModeledCohort_v1> {
    protected:

    price_t price_view;

    public:
    Agent_impl(AgentConfig<AgentType::ModeledCohort_v1>);
    ~Agent_impl() {}
    virtual AgentAction do_evaluate(price_t p);


    virtual void info_handler();
    virtual void info_update_view(std::shared_ptr<Info::Info<Info::Types::Subjective>>&);


    // for testing/debugging
    price_t get_price_view() { return this->price_view; }
    void set_price_view(price_t p) { this->price_view = p; }


    std::mt19937 engine;
    std::normal_distribution<double> dist;
};
using ModeledCohortAgent_v1 = Agent_impl<AgentType::ModeledCohort_v1>;


template<>
struct AgentConfig<AgentType::ModeledCohort_v2> : public AgentConfig<AgentType::ModeledCohort_v1> {
    AgentConfig(json config) 
    :   AgentConfig<AgentType::ModeledCohort_v1>(config)
    {
        auto parameters = config["distribution_parameters"].get<std::deque<double>>();
        this->e_0 = parameters[0];
        this->i_0 = parameters[1];
        this->r_0 = parameters[2];
        this->r_1 = parameters[3];
        this->r_2 = parameters[4];
        this->i_1 = parameters[5];
        this->i_2 = parameters[6];
        this->e_1 = parameters[7];

        for (int x : parameters) {
            if (x < 0 || x > 1) {
                throw std::out_of_range(
                    "values in distribution_parameters need to all be in [0,1]"
                );
            }
        }

    }

    // see diagram(s) for explanation
    double e_0;
    double i_0;
    double r_0;
    double r_1;
    double r_2;
    double i_1;
    double i_2;
    double e_1;
};
template<>
class Agent_impl<AgentType::ModeledCohort_v2> : public ModeledCohortAgent_v1, 
    public Agent_base<AgentType::ModeledCohort_v2> {
    protected:
    
    float current_subjectivity_extent;

    public:
    Agent_impl(AgentConfig<AgentType::ModeledCohort_v2>);
    ~Agent_impl() {}
    virtual AgentAction do_evaluate(price_t p);

    virtual void info_update_view(std::shared_ptr<Info::Info<Info::Types::Subjective>>&);


    // optionally return "trace" info
    std::tuple<std::deque<double>, std::deque<double>,
        std::optional<std::tuple<
            std::deque<std::string>,    // segment labels
            std::deque<std::string>,          // y labels
            std::deque<double>          // segment values
        >>
    >
    compute_distribution_points(price_t, std::optional<float> = std::nullopt, bool return_trace = false);
};
using ModeledCohortAgent_v2 = Agent_impl<AgentType::ModeledCohort_v2>;

};

#endif