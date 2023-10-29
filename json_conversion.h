#ifndef JSON_CONVERSION_H
#define JSON_CONVERSION_H 1

#include <utility>
#include <functional>

#include <nlohmann/json.hpp>
#include "Agent.h"
#include "Market.h"

#include <boost/asio.hpp>

using json = nlohmann::json;
namespace ba = boost::asio;


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


void from_json(const json& j, agent_config_item& c);
void from_json(const json& j, subscriber_config_item& c);

namespace Info {
    void from_json(const json& j, std::shared_ptr<Abstract>& t);
    /*
    void from_json(const json& j, Info<Types::Subjective>& i);
    void from_json(const json& j, Types& t);
    */
};

namespace nlohmann {

   // for now, treat the high-precision price_t as a double for JSON purposes

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