
/*

https://github.com/CrowCpp/Crow


*/


#include <crow.h>
#include <nlohmann/json.hpp>
#include "Agent.h"
#include "Interface.h"
#include "Subscriber.h"
#include "Market.h"
#include "ts.h"
#include <utility>
#include <functional>
#include <map>
#include <format>

#include "json_conversion.h"


using json = nlohmann::json;
using namespace std;


namespace {

    // constructing Agent instances from data provided from the HTTP interface
    // currently the JSON configuration is interpreted by the AgentConfig constructors
    // (in most other situations, we use specific JSON conversion functions defined
    // in json_conversion.cpp, automatically called by the JSON library)

    std::map<std::string, std::function<unique_ptr<Agent>(json)>> agent_factory {
        { "TrivialAgent", 
            [](json y) {
                return unique_ptr<Agent>(
                    new TrivialAgent (AgentConfig<AgentType::Trivial>(y))
                );
            }
        },

        { "BasicNormalDistAgent", 
            [](json y) {
                return unique_ptr<Agent>(
                    new BasicNormalDistAgent (AgentConfig<AgentType::BasicNormalDist>(y))
                );
            }
        },

        { "ModeledCohortAgent_v1", 
            [](json y) {
                return unique_ptr<Agent>(
                    new ModeledCohortAgent_v1 (AgentConfig<AgentType::ModeledCohort_v1>(y))
                );
            }
        },
    };

};


namespace {
    using namespace Subscriber;

    // constructing Subscriber instances from data provided over the HTTP interface


    // generates an element in the subscriber_factory_factory map
    // RecordType (as enum)     ->      function which returns an AbstractFactory 
    //                                  given a FactoryParameter<RecordType> in JSON form
    template<typename RecordType>
    std::pair<
        record_type_t, 
        std::function<std::shared_ptr<Subscriber::AbstractFactory>(json)>
    > subscriber_factory_factory_generator() {
        return {
            constraint<RecordType>::t,
            [](json j) {
                auto p = j.get<FactoryParameter<RecordType>>();
                return std::shared_ptr<AbstractFactory>(new Factory<RecordType>(p));
            }
        };
    }

    std::map<
        Subscriber::record_type_t, 
        std::function<std::shared_ptr<Subscriber::AbstractFactory>(json)>
    > subscriber_factory_factory = 
    {
        subscriber_factory_factory_generator<AgentAction>(),
        subscriber_factory_factory_generator<price_t>()
    };
};



std::shared_ptr<Interface> Interface::get_instance(std::shared_ptr<Market::Market> m) {
    if (Interface::instantiated != true) {
        Interface::instance = std::shared_ptr<Interface>(new Interface(m));
        Interface::instantiated = true;
    }

    return Interface::instance;
}

crow::response Interface::crow__market_run(const crow::request& req) {
    auto interface = Interface::instance;
    json jreq = json::parse(req.body);

    std::optional<int> iter_count;
    try {
        iter_count = jreq["iter_count"];
    } catch (std::exception& e) {
        // TODO json exceptions
        iter_count = std::nullopt;
    }

    interface->market->queue_op(
        std::shared_ptr<Market::op<Market::op_t::RUN>> { new Market::op<Market::op_t::RUN> (iter_count) }
    );

    return interface->build_json_crow(false, "run request queued", std::nullopt);
}


void Interface::crow__market_stop(crow::request& req, crow::response& resp) {
    auto interface = Interface::instance;

    interface->market->queue_op(
        std::shared_ptr<Market::op<Market::op_t::STOP>> { new Market::op<Market::op_t::STOP> {} }
    );

    resp = interface->build_json_crow(false, "run request queued", std::nullopt);
    resp.end();
}


void Interface::crow__market_wait_for_stop(crow::request& req, crow::response& resp) {
    auto interface = Interface::instance;

    json jreq;
    try {
        jreq = json::parse(req.body);
    } catch (json::parse_error& e) {
        resp = interface->build_json_crow(true, std::string("JSON parse error: ") + e.what(), {});
    }

    std::optional<timepoint_t> tp;
    try {
        uintmax_t tmp = jreq["timepoint"];
        tp = timepoint_t(tmp);
    } catch (json::out_of_range& e) {
        tp = std::nullopt;
    }
    // it appears that type_error is thrown if the request body is empty (null) - the
    // libary interprets this as the 'timepoint' field being null
    catch (json::type_error& e) {
        tp = std::nullopt;
    }

    std::optional<timepoint_t> actual_tp = interface->market->wait_for_stop(tp);

    if (actual_tp.has_value()) {
        resp = interface->build_json_crow(false, "stopped", json::object({
            { "timepoint", json(actual_tp.value().to_numeric()) }
        }));
    } else {

        if (tp.has_value()) {
            resp = interface->build_json_crow(true, "timed out", json::object({
                { "limit", json(tp.value().to_numeric()) },
            }));
        } 
        // currently this is "impossible"
        else {
            resp = interface->build_json_crow(true, 
                "Market::wait_for_stop unexpectedly returned", {}
            );
        }

    }


    resp.end();
}

void Interface::crow__market_configure(crow::request& req, crow::response& resp) {
    auto interface = Interface::instance;
    json jreq = json::parse(req.body);

    auto config = jreq.get<Market::Config>();

    interface->market->configure(config);

    resp = interface->build_json_crow(false, "success", {});
    resp.end();
}

void Interface::crow__market_start(crow::request& req, crow::response& resp) {
    auto interface = Interface::instance;

    try {
        interface->market->start();
        resp = interface->build_json_crow(false, "successfully started", {});
    } catch (std::logic_error& e) {
        resp = interface->build_json_crow(true, 
            "already started", {}, 500);
    }

    resp.end();
}

void Interface::crow__market_reset(crow::request& req, crow::response& resp) {
    auto interface = Interface::instance;
    
    interface->market->reset();

    resp = interface->build_json_crow(false, "success", {});
    resp.end();
}

// finished 
// not tested
crow::response Interface::crow__get_price_history(const crow::request& req) {
    auto interface = Interface::instance;

    json jreq;
    try {
        jreq = json::parse(req.body);
    } catch (json::parse_error& e) {
        return interface->build_json_crow(true, std::string("JSON parse error: ") + e.what(), {});
    }

    bool erase = false;
    try {
        erase  = jreq["erase"];
    } catch (json::out_of_range& e) {
        return interface->build_json_crow(true, "missing `erase` argument", {});
    }

    auto history = interface->market->get_price_history(erase);
    json data = json(*(history->to_map()));
    return interface->build_json_crow(false, "success", data);
}





// this type allows the list_helper handler to return:
//  - a JSON object when RetKey is string, 
//  - a "bare" list with just the RetVal objects when RetKey is an int, 
//  - a list of pairs when RetKey is some other type.
//
// in the second case, by "bare" list, I mean that a value's index in the returned list
//  is just equal to the integer key for that value.
//  eg. { 1: A, 0: B } returns the JSON array: [ A, B ]

template<typename RetKey>
struct list_json_ret_t {
    typename std::map<RetKey, json> data_ret;

    void add(RetKey&& k, json&& v) {
        this->data_ret.emplace(k, v);
    }

    void add(const RetKey& k, json&& v) {
        this->data_ret.insert({ k, v });
    }

    json dump() { return json(this->data_ret); }
};

template<>
struct list_json_ret_t<int> {
    typename std::vector<json> data_ret;

    void add(int i, json&& v) {
        this->data_ret.emplace(this->data_ret.begin() + i, v);
    }

    json dump() { return json(this->data_ret); }
};

std::optional<std::deque<json>>
Interface::handle_json_array(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    try {
        auto jreq = json::parse(req.body);

        if (!jreq.is_array()) {
            res = interface->build_json_crow(true, std::string("request body must be JSON array"),  {}, 400);
            res.end();
            return std::nullopt;
        }

        return jreq.get<std::deque<json>>();
    } catch (json::parse_error& e) {
        res = interface->build_json_crow(true, string("JSON parse error: ") + e.what(), {}, 400);
        res.end();
        return std::nullopt;
    }
}

template<typename InputItem, typename RetKey, typename RetVal, typename HandlerType>
void Interface::list_helper(
    const crow::request& req, 
    crow::response& res, 
    HandlerType handler_f,
    std::optional<std::function<void(int)>> finally_f 
) 
{
    auto interface = Interface::instance;
    
    auto json_deque_opt = Interface::handle_json_array(req, res);
    if (!json_deque_opt.has_value()) {
        return;
    }

        // assuming no exception when jreq.is_array() == true
    std::deque<json> json_deque = json_deque_opt.value();
    std::deque<InputItem> input_vec;

    json most_recent_conversion;
    try {
        std::transform(json_deque.begin(), json_deque.end(), std::back_inserter(input_vec),
        [&most_recent_conversion](json j) {
            most_recent_conversion = j;
            return j.get<InputItem>();
        });
    } catch (std::exception& e) {
        res = interface->build_json_crow(true, 
            string("encountered error during type conversion: ") + e.what(), 
            most_recent_conversion, 
        400);
        res.end();
        return;
    }


    // TODO catch possible conversion error for InputItem


    /*  string at position i is the error, if any, which occurred for 
        entry i in the config list
    */
    list_retmap_t<RetKey, RetVal> retmap;

    list_helper_adapter<InputItem, RetKey, RetVal, HandlerType> adapter;
    adapter(handler_f, interface, input_vec, retmap);

    if (finally_f.has_value()) {
        (finally_f.value())(retmap.size());
    }

    int error_count = std::accumulate(retmap.begin(), retmap.end(), false, 
        [](int acc, auto& pair) { 
            return acc + (std::holds_alternative<std::string>(pair.second) ? 1 : 0); 
        }
    );

    //std::deque<json> data_ret;
    //typename std::map<RetKey, json> data_ret;
    list_json_ret_t<RetKey> data_ret;

    for (auto& pair : retmap) {
        data_ret.add(pair.first, std::visit(
            //  this lambda is "polymorphic" and converts both possible types to 
            //  JSON using the json class's polymorphic constructor
            [](auto&& x) {
                return json(x);
            },
            pair.second
        ));
    }

    res = interface->build_json_crow(
        error_count > 0 ? true : false,
        error_count > 0 
            ? std::string("completed with ") + std::to_string(error_count) + std::string(" errors")
            : "completed without errors"
        ,
        data_ret.dump()
    );
    res.end();
}


// TODO WLOG, possibly use list_generator method which bundles list_helper_generator_t
// with list_helper to remove the redundancy in the call
// most likely could eliminate distinction b/w handler adapter and the function helper types
//  in the list_generator function, pass the adapter inline as a lambda
// current error is that you're missing an int template argument below

void Interface::crow__add_agents(const crow::request& req, crow::response& res) {

    Interface::list_generator_helper<agent_config_item, std::deque<Market::agentid_t>>
        (req, res, 
        [](auto interface, agent_config_item& spec) -> list_ret_t<std::deque<Market::agentid_t>>
    {
        auto factory_element = agent_factory.find(spec.type);

        try {
            std::deque<Market::agentid_t> ids;
            for (int i = 0; i < spec.count; i++) {

                try {
                    unique_ptr<Agent> agent = (factory_element->second)(spec.config);
                    ids.push_back(interface->market->add_agent(std::move(agent)));
                } 
                // something wrong with a value in AgentConfig (spec.config)
                catch (std::invalid_argument& e) {
                    return e.what();
                }
                // missing value from AgentConfig
                catch (json::out_of_range& e) {
                    return e.what();
                }

            }

            return ids;
        } catch (json::parse_error& e) {
            return std::string("JSON parse error: ")+ e.what();
        }
    });
}

void Interface::crow__del_agents(const crow::request& req, crow::response& res) {

    using agentid_t = Market::agentid_t;

    Interface::list_handler_helper<agentid_t, agentid_t, bool>
        (req, res, 
        [](auto interface, std::deque<agentid_t>& ids) -> list_retmap_t<agentid_t, bool>
    {
        auto result = instance->market->del_agents(ids);
        list_retmap_t<agentid_t, bool> ret;

        std::transform(result.begin(), result.end(), std::inserter(ret, ret.end()),
        [](auto& pair) -> std::pair<agentid_t, list_ret_t<bool>> {
            return {
                pair.first,
                pair.second == true ? 
                    list_ret_t<bool>(true) : list_ret_t<bool>(std::string("failed"))
            };
        });

        return ret;
    });
}


// list agents

void Interface::crow__list_agents(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    auto list = interface->market->list_agents();

    res = interface->build_json_crow(
        false,
        "success",
        json(list)
    );

    res.end();
}



// TODO - handle the case where the response is large enough to require streaming
void Interface::crow__get_agent_history(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;
    json jreq;
    try {
        jreq = json::parse(req.body);
    } catch (json::parse_error& e) {
        res = interface->build_json_crow(true, "error parsing JSON request body", {}, 400);
        res.end();
        return;
    }

    int id;
    try {
        id = jreq["id"].get<int>();
    } catch (json::out_of_range& e) {
        res = interface->build_json_crow(true, "agent ID not specified", {}, 400);
        res.end();
        return;
    }

    auto history = interface->market->get_agent_history(id, false);

    if (!history.has_value()) {
        res = interface->build_json_crow(true, "agent not found", {
            { "id", id }
        });
        res.end();
        return;
    }

    res = interface->build_json_crow(
        false, 
        "success",
        json::object({
            {"id", id },
            {"history", json(*(history.value()->to_map())) },
        })
    );
    res.end();
}


void Interface::crow__delete_agent_history(const crow::request& req, crow::response& res) {
    json jreq = json::parse(req.body);



}

void Interface::crow__emit_info(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;
    json jreq = json::parse(req.body);

    auto j_input_opt = Interface::handle_json_array(req, res);
    if (!j_input_opt.has_value()) {
        return;
    }

    std::deque<json> j_input = j_input_opt.value();

    std::deque<std::pair<std::string, json> > err_ret;

    Info::infoset_t infoset;

    int i = 0;
    for (json j : j_input) {
        try {
            auto iptr = j.get<std::shared_ptr<Info::Abstract>>();
            infoset.insert(infoset.begin(), iptr);

        } catch (json::parse_error& e) {
            std::ostringstream estr;
            estr << "invalid info object: " << e.what();
            err_ret.push_back({ estr.str(), j });
        }
        
        i++;
    }

    if (err_ret.size() > 0) {
        res = interface->build_json_crow(
            true, 
            "encountered errors parsing Info objects",
            json(err_ret),
            400
        );
    } else {
        auto ret = interface->market->emit_info(infoset);

        if (std::holds_alternative<std::string>(ret)) {
            res = interface->build_json_crow(
                true, 
                "Market::emit_info encountered error: " + std::get<std::string>(ret),
                std::nullopt,
                400
            );
        } else if (std::holds_alternative<timepoint_t>(ret)) {
            res = interface->build_json_crow(
                false, 
                "success",
                json::object({{ "timepoint", std::get<timepoint_t>(ret) }})
            );
        }
    }

    res.end();
}

void Interface::crow__add_subscribers(const crow::request& req, crow::response& res) {

    Interface::list_generator_helper<subscriber_config_item, Subscriber::id_t>
    (req, res, [](auto interface, subscriber_config_item& c) -> list_ret_t<Subscriber::id_t>
    {
        try {
            auto config = c.config.get<Subscriber::Config>();

            auto factory_factory = subscriber_factory_factory[config.t];
            std::shared_ptr<AbstractFactory> factory = factory_factory(c.parameter);

            auto ret_deque = Subscriber::Subscribers::add({{ factory, config }});
            return ret_deque.at(0);
        
        } catch (json::exception& e) {
            return std::string("invalid configuration: ") + e.what();
        } catch (std::invalid_argument& e) {
            return std::string("invalid configuration: ") + e.what();
        }
    });
}

void Interface::crow__del_subscribers(const crow::request& req, crow::response& res) {
    json jreq = json::parse(req.body);

    using s_t = Subscriber::Subscribers::delete_status_t;
    using Ss = Subscriber::Subscribers;

    Interface::list_handler_helper<Subscriber::id_t, Subscriber::id_t, bool>
        (req, res, 
        [](auto interface, std::deque<Subscriber::id_t>& ids) -> list_retmap_t<Subscriber::id_t, bool>
    {
        list_retmap_t<Subscriber::id_t, bool> ret;
        for (std::pair<Subscriber::id_t, s_t>& pair : Ss::del(ids)) {

            switch (pair.second) {
                case s_t::DELETED:
                case s_t::MARKED:
                    ret.insert({ pair.first, true });
                break;
                case s_t::DOES_NOT_EXIST:
                    ret.insert({ 
                        pair.first, 
                        std::string("provided ID does not exist: ")+ std::to_string(pair.first)
                    });
                break;
            }
        }

        return ret;
    });

}


void Interface::crow__list_subscribers(const crow::request& req, crow::response& res) {
    using Ss = Subscriber::Subscribers;
    auto interface = Interface::instance;

    std::deque<Ss::list_entry_t> x = Ss::list();

    res = interface->build_json_crow(
        false, 
        "success",
        json(x)
    );
    res.end();
}

void Interface::crow__show_market_perf_data(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;
    auto m = interface->market->get_perf_map();

    std::map<std::string, std::map<timepoint_t, uintmax_t>> output;

    std::transform(m.begin(), m.end(), std::insert_iterator(output, output.begin()), 
    [](auto& pair) -> std::pair<std::string, std::map<timepoint_t, uintmax_t>> {
        auto oldmap = pair.second.to_map();
        std::map<timepoint_t, uintmax_t> newmap;

        std::transform(oldmap->begin(), oldmap->end(), std::insert_iterator(newmap, newmap.begin()), 
        [](auto& pair) -> std::pair<timepoint_t, uintmax_t> {
            return {
                pair.first,
                pair.second.count()
            };
        });

        return {
            pair.first,
            newmap
        };
    });

    res = interface->build_json_crow(
        false, 
        "success",
        json(output)
    );
    res.end();
}

void Interface::crow__reset_market_perf_data(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;
    interface->market->clear_perf_map();

    res = interface->build_json_crow(
        false, 
        "success",
        {}
    );
    res.end();

}

//void crow_handler(std::function )

Interface::Interface(std::shared_ptr<Market::Market> m) :
    market(m)
{

        //this->crow_handlers = new Crow_handlers(shared_from_this());
        //this->crow_handlers = std::make_shared<Crow_handlers>(x);

/*
    CROW_ROUTE(this->crow_app, "/version")([](){
        crow::json::wvalue x({{"version", 0.1}});
        return x;
    });

    CROW_ROUTE(this->crow_app, "/market/run")
    .methods("POST"_method)([this](const crow::request& req) {
    });
    */

    
    //const Crow_handlers *crow_handlers = &this->crow_handlers;

    CROW_ROUTE(this->crow_app, "/market/run")
    .methods("POST"_method)(&Interface::crow__market_run);

    CROW_ROUTE(this->crow_app, "/market/stop")
    .methods("POST"_method)(&Interface::crow__market_stop);

    CROW_ROUTE(this->crow_app, "/market/wait_for_stop")
    .methods("GET"_method)(&Interface::crow__market_wait_for_stop);

    CROW_ROUTE(this->crow_app, "/market/configure")
    .methods("POST"_method)(&Interface::crow__market_configure);

    CROW_ROUTE(this->crow_app, "/market/start")
    .methods("POST"_method)(&Interface::crow__market_start);

    CROW_ROUTE(this->crow_app, "/market/reset")
    .methods("POST"_method)(&Interface::crow__market_reset);

    CROW_ROUTE(this->crow_app, "/market/price_history")
    .methods("GET"_method)(&Interface::crow__get_price_history);

    CROW_ROUTE(this->crow_app, "/agent/add")
    .methods("POST"_method)(&Interface::crow__add_agents);

    CROW_ROUTE(this->crow_app, "/agent/delete")
    .methods("POST"_method)(&Interface::crow__del_agents);

    CROW_ROUTE(this->crow_app, "/agent/list")
    .methods("GET"_method)(&Interface::crow__list_agents);

    CROW_ROUTE(this->crow_app, "/agent/history")
    .methods("GET"_method)(&Interface::crow__get_agent_history);

    CROW_ROUTE(this->crow_app, "/agent/history/delete")
    .methods("POST"_method)(&Interface::crow__delete_agent_history);

    CROW_ROUTE(this->crow_app, "/info/emit")
    .methods("POST"_method)(&Interface::crow__emit_info);

    CROW_ROUTE(this->crow_app, "/subscribers/add")
    .methods("POST"_method)(&Interface::crow__add_subscribers);

    CROW_ROUTE(this->crow_app, "/subscribers/delete")
    .methods("POST"_method)(&Interface::crow__del_subscribers);

    CROW_ROUTE(this->crow_app, "/subscribers/list")
    .methods("GET"_method)(&Interface::crow__list_subscribers);

    CROW_ROUTE(this->crow_app, "/market/showperf")
    .methods("GET"_method)(&Interface::crow__show_market_perf_data);

    CROW_ROUTE(this->crow_app, "/market/resetperf")
    .methods("POST"_method)(&Interface::crow__reset_market_perf_data);

}

bool Interface::start() {
    try {
        this->crow_app.port(18080).multithreaded().run();
        return true;
    } catch (std::runtime_error& e) {
        std::printf("Interface::start: %s", e.what());
        return false;
    }
}

json Interface::build_json(bool is_error, std::string msg, std::optional<json> data) {
    return {
        { "is_error", is_error },
        { "message", msg },
        { "api_version", this->api_version },
        { "data", (data ? *data : json {})}
    };
};

crow::response Interface::build_json_crow(
    bool is_error, std::string msg, 
    std::optional<json> data, std::optional<int> http_code 
    ) {

        json j = this->build_json(is_error, msg, data);

        return crow::response(
            http_code ? *http_code : 200,
            j.dump()
        );
}