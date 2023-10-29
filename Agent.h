#ifndef AGENT_H
#define AGENT_H 1

#include <stdio.h>
#include <memory>
#include <nlohmann/json.hpp>
#include "Info.h"
#include "types.h"
#include "ts.h"

using json = nlohmann::json;

#include <random>


typedef std::unique_ptr<ts<Info::infoset_t>::sparse_view> info_view_t;

const int MAX_INTERNAL_FORCE = 100;

/* investment action taken by each Agent */
struct AgentAction {
    enum Direction direction;

    /* must be in [0,100] */
    long double internal_force;
};

enum class AgentType { 
    Determinstic,
    ModeledCohort_v1,
    Trivial,
    BasicNormalDist
};

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


class Agent {
    private:


    // temporary object passed to the Agent at every Agent::evaluate invocation
    std::optional<info_view_t> info_view;

    // an empty _info_cursor indicates the agent has not read any Market::info_history entries
    std::optional<timepoint_t> _info_cursor;

    protected:
    Agent(AgentConfigBase c) : config(c) {}

    std::optional<Info::infoset_t>
    read_next_infoset() {

        // std::nullopt info_view means that Market::info_history is empty
        if (this->info_view.has_value()) {
            auto& info_view_ptr = this->info_view.value();

            auto [first,last] = info_view_ptr->bounds();

            if (this->_info_cursor < last || ! this->_info_cursor) {

                // initialize our cursor to the first element in the info_view, or increment 
                // it from its previous value
                if (! this->_info_cursor) {
                    this->_info_cursor = first;
                } else {

                    // keep the info cursor updated consistently with the info_view
                    (*this->_info_cursor)++;
                    (*info_view_ptr)++;
                }

                return info_view_ptr->read();
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
    const static enum AgentType t;
    const AgentConfigBase config;

    const std::optional<timepoint_t>& info_cursor() { return this->_info_cursor; }


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

    /*
    */
    virtual AgentAction do_evaluate(price_t p) = 0;
};


template<enum AgentType T>
class Agent_base : public Agent {
    protected:
    AgentConfig<T> _config;

    public:
    AgentConfig<T> config() { return this->_config; }

    Agent_base(AgentConfig<T> c) : _config(c), Agent(c) {
    }
    const static enum AgentType t = T;
};





template<>
struct AgentConfig<AgentType::BasicNormalDist> : AgentConfigBase {
    AgentConfig(json config) : 
        AgentConfigBase(config),
        mean(config["mean"]),
        stddev(config["stddev"]) {

// TODO check exception for json key access
    }

    double stddev;
    double mean;
};
class BasicNormalDistAgent : public Agent_base<AgentType::BasicNormalDist> {
    protected:

    public:
    BasicNormalDistAgent(AgentConfig<AgentType::BasicNormalDist>);
    ~BasicNormalDistAgent() {}
    AgentAction do_evaluate(price_t p);

    std::mt19937 engine;
    std::normal_distribution<float> dist;
};



template<>
struct AgentConfig<AgentType::Trivial> : AgentConfigBase {
    AgentConfig(json config) : 
        AgentConfigBase(config),
        direction(direction_str_ctor(config["direction"])), 
        internal_force(config["internal_force"]) {

// TODO check exception for json key access
    }
    enum Direction direction;
    float internal_force;
};
class TrivialAgent : public Agent_base<AgentType::Trivial> {
    protected:

    public:
    TrivialAgent(AgentConfig<AgentType::Trivial>);
    ~TrivialAgent() {}
    AgentAction do_evaluate(price_t p);
};



// TODO refactor 
/*
class ModeledCohortAgent_base : public Agent_base<AgentType::ModeledCohort> {

};
*/


template<>
struct AgentConfig<AgentType::ModeledCohort_v1> : AgentConfigBase {
    AgentConfig(json config) : 
        AgentConfigBase(config),
        initial_variance(config["initial_variance"]),
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

    double initial_variance;
    double variance_multiplier;
    double force_threshold;
    price_t default_price_view;
};
class ModeledCohortAgent_v1 : public Agent_base<AgentType::ModeledCohort_v1> {
    protected:

    price_t price_view;

    public:
    ModeledCohortAgent_v1(AgentConfig<AgentType::ModeledCohort_v1>);
    ~ModeledCohortAgent_v1() {}
    AgentAction do_evaluate(price_t p);


    void info_update_view(Info::infoset_t&);


    // for testing/debugging
    price_t get_price_view() { return this->price_view; }

    std::mt19937 engine;
    std::normal_distribution<double> dist;


};




#endif