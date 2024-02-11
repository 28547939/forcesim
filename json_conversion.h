#ifndef JSON_CONVERSION_H
#define JSON_CONVERSION_H 1

#include <utility>
#include <functional>

#include <nlohmann/json.hpp>
#include "Agent.h"
#include "Market.h"


using json = nlohmann::json;


/*
    Type used to interpret Agent configuration sent to add_agents
*/
struct agent_config_item {
    std::string type;
    int count;

    // interpreted manually depending on the type 
    json config;
};

struct subscriber_config_item {
    json config;
    json parameter;
};

struct info_item {
    json type;
    json data;
};


void to_json(json& j, const timepoint_t tp);

namespace Market {

    void to_json(json& j, const agentid_t id);
    void to_json(json& j, const agentrecord_desc_t id);

    void from_json(const json& j, agentid_t& id);

    void from_json(const json& j, Config& c);

};

//namespace Subscriber {};
// converters for the Subscriber namespace are in Subscriber.h/Subscriber.cpp

void to_json(json& j, const AgentAction act);

void to_json(json& j, const numeric_id<market_numeric_id_tag> id);
void to_json(json& j, const numeric_id<subscriber_numeric_id_tag> id);

void from_json(const json& j, numeric_id<market_numeric_id_tag>& id);
void from_json(const json& j, numeric_id<subscriber_numeric_id_tag>& id);


void from_json(const json& j, agent_config_item& c);
void from_json(const json& j, subscriber_config_item& c);

namespace Info {
    void from_json(const json& j, std::shared_ptr<Abstract>& t);
};

namespace nlohmann {

   // for now, treat the high-precision price_t as a double for JSON purposes
   // alternative would be to store it in a separate JSON object using boost's 
   // internal representation, or as a string

    template<>
    struct adl_serializer<price_t> {
        static void to_json(json& j, const price_t& x) {
            j = static_cast<double>(x);
        }

        static void from_json(const json& j, price_t& x) {
            x = j.get<double>();
        }
    };
};





#endif