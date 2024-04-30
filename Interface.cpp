

// https://github.com/CrowCpp/Crow
#include <crow.h>
#include <nlohmann/json.hpp>
#include "Agent/Agent.h"
#include "Agent/Factory.h"
#include "Interface.h"
#include "Subscriber/Subscriber.h"
#include "Subscriber/Subscribers.h"
#include "Subscriber/json_conversion.h"
#include "Subscriber/Factory.h"
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
    using namespace Subscriber;

    // constructing Subscriber instances from data provided over the HTTP interface


    // subscriber_factory_factory_generator generates an element in the 
    //  subscriber_factory_factory map. The subscriber_factory_factory map
    //  provides a subscriber_factory_factory given a subscriber type (RecordType),
    //  which in turn provides a Subscriber Factory (cast to AbstractFactory), given
    //  a parameter. The Factory will create the unique Subscriber associated with 
    //  that parameter.
    //      
    //
    // RecordType (as enum)     ->      function which returns an AbstractFactory 
    //                                  given a FactoryParameter<RecordType> in JSON form
    template<typename RecordType>
    std::pair<
        record_type_t, 
        std::function<std::shared_ptr<AbstractFactory>(json)>
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
        record_type_t, 
        std::function<std::shared_ptr<AbstractFactory>(json)>
    > subscriber_factory_factory = 
    {
        subscriber_factory_factory_generator<Agent::AgentAction>(),
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

std::shared_ptr<Interface> Interface::get_instance() {
    if (Interface::instantiated != true) {
        throw std::logic_error("get_instance() without arguments requires existing instance");
    }

    return Interface::instance;
}

/*
 ****************************************************************************************
 Generic list/array handling routines (see Interface.h)
*/

// this type allows the list_helper handler to return:
//  - a JSON object when RetKey is string, 
//  - a "bare" list with just the RetVal objects when RetKey is an int, 
//  - a list of pairs when RetKey is some other type.
//
// in the second case, by "bare" list, I mean that a value's index in the returned list
//  is just equal to the integer key for that value.
//  eg. { 0: A, 1: B } returns the JSON array: [ A, B ]

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


std::optional<json> Interface::handle_json(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    try {
        return json::parse(req.body);
    } catch (json::parse_error& e) {
        res = interface->build_json_crow(
            InterfaceErrorCode::Json_parse_error, 
            string("JSON parse error: ") + e.what(), {}, 
            std::nullopt,
            400
        );
        res.end();
        return std::nullopt;
    }
}

void Interface::handle_json_wrapper(
    const crow::request& req, crow::response& res,
    std::function<void(const crow::request& req, crow::response& res, json&)> f
) {
    auto interface = Interface::instance;

    auto jreq_opt = interface->handle_json(req, res);
    if (!jreq_opt.has_value()) {
        return;
    }

    auto jreq = jreq_opt.value();

    try {
        f(req, res, jreq);
    } catch (json::type_error& e) {
        res = interface->build_json_crow(
            InterfaceErrorCode::Json_type_error, 
            std::string("json::type_error caught: may indicate that client is passing JSON as string;")
            + std::string("error: ") + e.what(),
            {}, 
            std::nullopt,
            400);
    }

}

std::optional<std::deque<json>>
Interface::handle_json_array(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    auto jreq_opt = interface->handle_json(req, res);
    if (!jreq_opt.has_value()) {
        return std::nullopt;
    }
    auto jreq = jreq_opt.value();

    if (!jreq.is_array()) {
        res = interface->build_json_crow(
            InterfaceErrorCode::Json_type_error, 
            std::string("request body must be JSON array"),  {}, 
            std::nullopt,
            400);
        res.end();
        return std::nullopt;
    }

    return jreq.get<std::deque<json>>();
}

void Interface::handle_json_array_wrapper(
    const crow::request& req, crow::response& res,
    std::function<void(const crow::request& req, crow::response& res, std::deque<json>&)> f
) {
    auto interface = Interface::instance;

    auto jreq_opt = interface->handle_json_array(req, res);
    if (!jreq_opt.has_value())
        return;

    auto jreq = jreq_opt.value();

    // TODO shared boilerplate with handle_json_wrapper
    try  {
        f(req, res, jreq);
    } catch (json::type_error& e) {
        res = interface->build_json_crow(
            InterfaceErrorCode::Json_type_error, 
            std::string("json::type_error caught: may indicate that client is passing JSON as string;")
            + std::string("error: ") + e.what(),
            {}, 
            std::nullopt,
            400);
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
        res = interface->build_json_crow(InterfaceErrorCode::Json_type_error, 
            string("encountered error during type conversion: ") + e.what(), 
            most_recent_conversion, 
            std::nullopt, 
            400
        );
        res.end();
        return;
    }

    list_retmap_t<RetKey, RetVal> retmap;

    list_helper_adapter<InputItem, RetKey, RetVal, HandlerType> adapter;
    adapter(handler_f, interface, input_vec, retmap);

    if (finally_f.has_value()) {
        (finally_f.value())(retmap.size());
    }

    std::deque<RetKey> error_keys = std::accumulate(retmap.begin(), retmap.end(), std::deque<RetKey>{}, 
        [](std::deque<RetKey> acc, auto& pair) { 
            if (std::holds_alternative<list_error_t>(pair.second)) {
                acc.push_back(pair.first);
            }
            return acc;
        }
    );
    int error_count = error_keys.size();

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
        error_count > 0 
            ? std::optional<enum InterfaceErrorCode>(InterfaceErrorCode::Multiple) 
            : std::nullopt,
        error_count > 0 
            ? std::string("completed with ") + std::to_string(error_count) + std::string(" errors")
            : "completed without errors"
        ,
        json {
            { "error_keys", json(error_keys) },
            { "data", data_ret.dump() }
        },
        detect_multi_response_type<RetKey>::value
    );
    res.end();
}

/*
 ****************************************************************************************
*/

void Interface::crow__market_run(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    interface->handle_json_wrapper(req, res, 
    [&interface](const crow::request& req, crow::response& res, json& jreq) {

        std::optional<int> iter_count;
        try {
            iter_count = jreq["iter_count"];
        } catch (json::exception& e) {
            iter_count = std::nullopt;
        }

        interface->market->queue_op(
            std::shared_ptr<Market::op<Market::op_t::RUN>> { 
                new Market::op<Market::op_t::RUN> (iter_count) 
            }
        );

        res = interface->build_json_crow(std::nullopt, "run request queued", std::nullopt);
        res.end();
    });
}


void Interface::crow__market_pause(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    interface->market->queue_op(
        std::shared_ptr<Market::op<Market::op_t::PAUSE>> { new Market::op<Market::op_t::PAUSE> {} }
    );

    res = interface->build_json_crow(std::nullopt, "pause request queued", std::nullopt);
    res.end();
}


void Interface::crow__market_wait_for_pause(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    interface->handle_json_wrapper(req, res, 
    [&interface](const crow::request& req, crow::response& res, json& jreq) {

        std::optional<timepoint_t> tp;
        try {
            uintmax_t tmp = jreq["timepoint"];
            tp = timepoint_t(tmp);
        } catch (json::out_of_range& e) {}
        // type_error is thrown when the request body is empty - interpreted as null, so 
        // we can't look up keys as if it were a JSON object
        catch (json::type_error& e) {}

        std::optional<timepoint_t> actual_tp = interface->market->wait_for_pause(tp);

        if (actual_tp.has_value()) {
            res = interface->build_json_crow(std::nullopt, "paused", json::object({
                { "timepoint", json(actual_tp.value().to_numeric()) }
            }));
        } else {

            if (tp.has_value()) {
                res = interface->build_json_crow(
                    InterfaceErrorCode::General_error, "timed out", json::object({
                    { "limit", json(tp.value().to_numeric()) },
                }));
            } 
            // currently this is "impossible"
            else {
                res = interface->build_json_crow(
                    InterfaceErrorCode::General_error, 
                    "Market::wait_for_pause unexpectedly returned", {}
                );
            }

        }

        res.end();
    });
}

void Interface::crow__market_configure(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    interface->handle_json_wrapper(req, res, 
    [&interface](const crow::request& req, crow::response& res, json& jreq) {
        auto config = jreq.get<Market::Config>();

        interface->market->configure(config);

        res = interface->build_json_crow(std::nullopt, "success", {});
        res.end();
    });
}

void Interface::crow__market_start(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    try {
        interface->market->start();
        res = interface->build_json_crow(std::nullopt, "successfully started", {});
    } catch (std::logic_error& e) {
        res = interface->build_json_crow(
            InterfaceErrorCode::Already_started, 
            "already started", {}, 
            std::nullopt,
            200
        );
    }

    res.end();
}

void Interface::crow__market_reset(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;
    
    interface->market->reset();

    res = interface->build_json_crow(std::nullopt, "success", {});
    res.end();
}

// finished 
// not tested
void 
Interface::crow__get_price_history(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    interface->handle_json_wrapper(req, res, 
    [&interface](const crow::request& req, crow::response& res, json& jreq) {

        bool erase = false;
        try {
            erase  = jreq["erase"];
        } catch (json::out_of_range& e) {
            res = interface->build_json_crow(
                InterfaceErrorCode::General_error, "missing `erase` argument", {}
            );
        }

        auto history = interface->market->get_price_history(erase);
        json data = json(*(history->to_map()));
        res = interface->build_json_crow(std::nullopt, "success", data);
        res.end();
    });
}

void 
Interface::crow__add_agents(const crow::request& req, crow::response& res) {

    Interface::list_generator_helper<agent_config_item, std::deque<Market::agentid_t>>
        (req, res, 
        [](auto interface, agent_config_item& spec) -> list_ret_t<std::deque<Market::agentid_t>>
    {


        auto t_it = Agent::str_agenttype.find(spec.type);
        std::optional<std::string> errstr;

        if (t_it == Agent::str_agenttype.end()) {
            errstr = std::string("unknown agent type: "+ spec.type);
        } else {
            Agent::AgentType t = t_it->second;
            auto factory_element = Agent::factory.find(t);

            if (factory_element == Agent::factory.end()) {
                errstr = std::string("factory not implemented: ") + spec.type;
            } else {
                std::deque<Market::agentid_t> ids;
                for (int i = 0; i < spec.count; i++) {

                    try {
                        unique_ptr<Agent::Agent> agent = (factory_element->second)(spec.config);
                        ids.push_back(interface->market->add_agent(std::move(agent)));
                    } 
                    // something wrong with a value in AgentConfig (spec.config)
                    catch (std::invalid_argument& e) {
                        return std::tuple<enum InterfaceErrorCode, std::string> ({
                            InterfaceErrorCode::Agent_config_error,
                            e.what()
                        });
                    }
                    // missing value from AgentConfig (tried to access non-existent JSON object key)
                    catch (json::out_of_range& e) {
                        return std::tuple<enum InterfaceErrorCode, std::string> ({
                            InterfaceErrorCode::Agent_config_error,
                            e.what()
                        });
                    }

                }

                return ids;
            }
        }

        if (errstr.has_value()) {
            return std::tuple<enum InterfaceErrorCode, std::string> ({ 
                InterfaceErrorCode::Agent_not_implemented,
                errstr.value()
            });
        } else {
            throw std::logic_error("crow__add_agents: no value was returned");
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
                    list_ret_t<bool>(true) : list_ret_t<bool>(
                        std::tuple<enum InterfaceErrorCode, std::string>({ 
                            InterfaceErrorCode::Not_found, "agent not found" 
                        })
                    )
            };
        });

        return ret;
    });
}


void Interface::crow__list_agents(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    auto list = interface->market->list_agents();

    res = interface->build_json_crow(
        std::nullopt,
        "success",
        json(list)
    );

    res.end();
}



// TODO - handle the case where the response is large enough to require streaming
void Interface::crow__get_agent_history(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    interface->handle_json_wrapper(req, res, 
    [&interface](const crow::request& req, crow::response& res, json& jreq) {

        int id;
        try {
            id = jreq["id"].get<int>();
        } catch (json::out_of_range& e) {
            res = interface->build_json_crow(
                InterfaceErrorCode::General_error, "agent ID not specified", {}, 
                std::nullopt, 400
            );
            res.end();
            return;
        }

        auto history = interface->market->get_agent_history(id, false);

        if (!history.has_value()) {
            res = interface->build_json_crow(InterfaceErrorCode::Not_found, "agent not found", {
                { "id", id }
            });
            res.end();
            return;
        }

        res = interface->build_json_crow(
            std::nullopt, 
            "success",
            json::object({
                {"id", id },
                {"history", json(*(history.value()->to_map())) },
            })
        );
        res.end();

    });

}


// TODO
// Not a high priority item, since this is taken care of with market_reset
void Interface::crow__delete_agent_history(const crow::request& req, crow::response& res) {
    json jreq = json::parse(req.body);



}

// this is done manually instead with list_generator_helper so that we can
// submit the entire list of Info together as one infoset_t
void Interface::crow__emit_info(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;

    interface->handle_json_array_wrapper(req, res, 
    [&interface](const crow::request& req, crow::response& res, std::deque<json>& j_input) {
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
                InterfaceErrorCode::Json_parse_error, 
                "encountered errors parsing Info objects",
                json(err_ret),
                std::nullopt,
                400
            );
        } else {
            auto ret = interface->market->emit_info(infoset);

            if (std::holds_alternative<std::string>(ret)) {
                res = interface->build_json_crow(
                    InterfaceErrorCode::General_error,
                    "Market::emit_info encountered error: " + std::get<std::string>(ret),
                    std::nullopt,
                    std::nullopt,
                    400
                );
            } else if (std::holds_alternative<timepoint_t>(ret)) {
                res = interface->build_json_crow(
                    std::nullopt, 
                    "success",
                    json::object({{ "timepoint", std::get<timepoint_t>(ret) }})
                );
            }
        }

        res.end();
    });
}


template<class> inline constexpr bool false_helper = false;
void Interface::crow__add_subscribers(const crow::request& req, crow::response& res) {

    Interface::list_generator_helper<subscriber_config_item, Subscriber::id_t>
    (req, res, [](auto interface, subscriber_config_item& c) -> list_ret_t<Subscriber::id_t>
    {
        try {
            auto config = c.config.get<Subscriber::Config>();

            auto factory_factory = subscriber_factory_factory[config.t];
            std::shared_ptr<AbstractFactory> factory = factory_factory(c.parameter);

            auto ret = Subscriber::Subscribers::add(factory, config);

            return std::visit([](auto&& entry) -> list_ret_t<Subscriber::id_t> {
                // cppreference.com
                using T = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<T, Subscriber::id_t>) 
                    return entry;
                else if constexpr (std::is_same_v<T, std::string>)
                    return std::tuple<enum InterfaceErrorCode, std::string> ({
                        InterfaceErrorCode::General_error, entry 
                    });
                else
                    static_assert(false_helper<T>, "Unexpected type returned from Subscribers::add");
            }, ret);
        
        } catch (json::exception& e) {

            return std::tuple<enum InterfaceErrorCode, std::string> ({
                InterfaceErrorCode::Subscriber_config_error,
                std::string("JSON error when processing configuration: ") + e.what()
            });
        } catch (std::invalid_argument& e) {
            return std::tuple<enum InterfaceErrorCode, std::string> ({
                InterfaceErrorCode::Subscriber_config_error,
                std::string("invalid configuration: ") + e.what()
            });
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

                        std::tuple<enum InterfaceErrorCode, std::string> ({
                            InterfaceErrorCode::Not_found,
                            std::string("provided ID does not exist: ")+ pair.first.to_string()
                        })
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
        std::nullopt, 
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
        std::nullopt, 
        "success",
        json(output)
    );
    res.end();
}

void Interface::crow__reset_market_perf_data(const crow::request& req, crow::response& res) {
    auto interface = Interface::instance;
    interface->market->clear_perf_map();

    res = interface->build_json_crow(
        std::nullopt, 
        "success",
        {}
    );
    res.end();

}

Interface::Interface(std::shared_ptr<Market::Market> m) :
    market(m)
{

    CROW_ROUTE(this->crow_app, "/market/run")
    .methods("POST"_method)(&Interface::crow__market_run);

    CROW_ROUTE(this->crow_app, "/market/pause")
    .methods("POST"_method)(&Interface::crow__market_pause);

    CROW_ROUTE(this->crow_app, "/market/wait_for_pause")
    .methods("GET"_method)(&Interface::crow__market_wait_for_pause);

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

bool Interface::start(std::optional<asio::ip::address> listen_addr, int port) {
    try {
        if (!listen_addr.has_value()) {
            listen_addr = asio::ip::address::from_string("0.0.0.0");
        }
        this->crow_app.bindaddr(listen_addr->to_string()).port(port)
            .multithreaded().signal_clear().run();
        return true;
    } catch (std::runtime_error& e) {
        std::printf("Interface::start: %s", e.what());
        return false;
    }
}

void Interface::stop() {
    this->crow_app.stop();
}

// TODO json return object with setter methods to incrementally 
// build response without worrying about argument position

json Interface::build_json(
    std::optional<enum InterfaceErrorCode> error_code, 
    std::string msg, 
    std::optional<json> data,
    std::optional<InterfaceResponseType> data_type
) {

    // data type defaults to "Data" if data is not nullopt; 
    auto data_type_final = (
        data_type.has_value() ? data_type.value()
        : (
            data.has_value()
                ? std::optional<InterfaceResponseType>(InterfaceResponseType::Data) 
                : std::nullopt
        )
    );

    return {
        { "error_code", error_code.has_value() ? json(error_code.value()) : json {} },
        { "message", msg },
        { "api_version", this->api_version },
        { "data_type", data_type_final.has_value() ? json(data_type_final.value()) : json {} },
        { "data", (data ? *data : json {}) }
    };
};

crow::response Interface::build_json_crow(
    std::optional<enum InterfaceErrorCode> error_code,
    std::string msg, 
    std::optional<json> data, 
    std::optional<InterfaceResponseType> data_type,
    std::optional<int> http_code 
) {

        json j = this->build_json(error_code, msg, data, data_type);

        return crow::response(
            http_code ? *http_code : 200,
            j.dump()
        );
}

