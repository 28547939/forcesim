
#include <utility>
#include <functional>

#include "json_conversion.h"

using json = nlohmann::json;


void to_json(json& j, const timepoint_t tp) {
    j = json(tp.to_numeric());
}

namespace Market {

    void to_json(json& j, const agentid_t id) {
        j = json(id.to_numeric());
    }

    void from_json(const json& j, agentid_t& id) {
        id = agentid_t(j.get<int>());
    }

    void from_json(const json& j, Config& c) {
        if (j.find("iter_block") != j.end()) {
            c.iter_block = j.at("iter_block").get<uintmax_t>();
        }


    // Market::Config conversions - currently we're not using Config
        /*
        auto agent_history_prune_age = j.value("agent_history_prune_age", std::nullopt);
        auto price_history_prune_age = j.value("price_history_prune_age", std::nullopt);
        auto info_history_prune_age = j.value("info_history_prune_age", std::nullopt);
        */

    /*
        c = {
            .iter_block = iter_block ? iter_block.get<uintmax_t>() : std::nullopt;
            .agent_history_prune_age = agent_history_prune_age 
                ? agent_history_prune_age.get<uintmax_t>() : std::nullopt;

            .price_history_prune_age = price_history_prune_age 
                ? price_history_prune_age.get<uintmax_t>() : std::nullopt;

            .info_history_prune_age = info_history_prune_age 
                ? info_history_prune_age.get<uintmax_t>() : std::nullopt;
        };
        */
    }

    void to_json(json& j, const agentrecord_desc_t r) {

        json jobj = json::object({
            { "id", json(r.id) },
            { "created", json(r.created) },
            { "history_count", json(r.history_count) },
            { "flags", json(r.flags) }
        });

        j = jobj;
    }
};


// this is unfortunate - acceptable for now
void to_json(json& j, const numeric_id<market_numeric_id_tag> id) {
    j = json(id.to_numeric());
}
void to_json(json& j, const numeric_id<subscriber_numeric_id_tag> id) {
    j = json(id.to_numeric());
}
void from_json(const json& j, numeric_id<market_numeric_id_tag>& id) {
    id = numeric_id<market_numeric_id_tag>(j.get<unsigned int>());
}
void from_json(const json& j, numeric_id<subscriber_numeric_id_tag>& id) {
    id = numeric_id<subscriber_numeric_id_tag>(j.get<unsigned int>());
}


void to_json(json& j, const AgentAction act) {
    j = json();
    j["direction"] = json(act.direction == Direction::UP ? "UP" : "DOWN");
    j["internal_force"] = json(act.internal_force);
}

/*
currently not relying on this for incoming Agent configuration - better to check 
manually to give a more informative error message in the case of a non-implemented
agent than "JSON error"
void from_json(const json& j, enum AgentType& t) {
    std::string s = j.get<std::string>();
    using enum AgentType;

    auto it = agenttype_str.find(s);
    if (it == agenttype_str.end()) {
        throw std::invalid_argument(std::string("unknown AgentType: ") + s);
    } else {
        t = it->second;
    }
}
*/

void from_json(const json& j, agent_config_item& c) {
    j.at("type").get_to(c.type);
    j.at("count").get_to(c.count);
    j.at("config").get_to(c.config);
}

void from_json(const json& j, subscriber_config_item& c) {
    j.at("config").get_to(c.config);
    j.at("parameter").get_to(c.parameter);
}

namespace Info {



    // expects the JSON object to have 'type' and 'data' keys (see Interface.h)
    //
    // convert the Info object to the property subtype based on the type indicated
    // in the input JSON, then cast it to a shared_ptr<Abstract>
    // see Info.h for methods to down-cast the Abstract pointer 
    void from_json(const json& j, std::shared_ptr<Abstract>& info_json) {

        auto t_str = j.at("type").get<std::string>();
        if (t_str == "Test1") {
        } else if (t_str == "Subjective") {
            auto ptr = std::make_shared<Info<Types::Subjective>>();

            j.at("subjectivity_extent").get_to(ptr->subjectivity_extent);
            j.at("price_indication").get_to(ptr->price_indication);
            j.at("is_relative").get_to(ptr->is_relative);


            info_json = ptr;
        } else {
            throw std::invalid_argument("Invalid type argument for info object");
        }
    }
};

namespace nlohmann {

    /*  price_t is a high-precision floating point number from the boost library (see types.h),
        but to start with, we just consider it as a double when dealing with JSON conversions.
        It seems unlikely that we will need to receive `
    */

/*
    template<>
    void adl_serializer<price_t>::to_json(json& j, const price_t& x) {
        j = static_cast<double>(x);
    }

    template<>
    void adl_serializer<price_t>::from_json(const json& j, price_t& x) {
        x = j.get<double>();
    };
    */
};
