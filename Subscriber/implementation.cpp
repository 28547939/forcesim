

#include "../types.h"
#include "../Market.h"
#include <glog/logging.h>
#include <nlohmann/json.hpp>

#include "../json_conversion.h"

#include <iostream>

#include <chrono>
#include <thread>
#include <variant>
#include <queue>
#include <memory>


#include "common.h"
#include "Subscriber.h"
#include "Factory.h"
#include "Subscribers.h"
#include "json_conversion.h"


using asioudp = asio::ip::udp;



namespace Subscriber {

using namespace Subscriber;

// *******************************************************yy
// json conversion 
std::string record_type_t_str(const record_type_t& t) {
    if (t == record_type_t::AGENT_ACTION)
        return "AGENT_ACTION";

    if (t == record_type_t::PRICE)
        return "PRICE";

    if (t == record_type_t::INFO)
        return "INFO";

    throw std::invalid_argument("Subscriber::record_type_t_str implementation incomplete");
    return "";
}

// currently, for object -> struct conversions, checks are not made for extraneous fields
//
void to_json(json& j, const record_type_t t) {
    j = record_type_t_str(t);
}

void from_json(const json& j, record_type_t& t) {
    auto str = j.get<std::string>();

    if (str == "AGENT_ACTION") {
        t = record_type_t::AGENT_ACTION;
    } else if (str == "PRICE") {
        t = record_type_t::PRICE;
    } else if (str == "INFO") {
        t = record_type_t::INFO;
    } else {
        throw std::invalid_argument("`t` must be one of AGENT_ACTION, PRICE, INFO; provided: " + str);
    }
}

/*
namespace boost::asio::ip {
void from_json(const json& j, asio::ip::address& addr) {
    std::string addr_str = j.get<std::string>();
    addr = asio::ip::make_address(addr_str);
}
};
*/


void from_json(const json& j, Config& c) {
    j.at("type").get_to(c.t);
    j.at("endpoint").get_to(c.endpoint);
    j.at("granularity").get_to(c.granularity);
}

void from_json(const json& j, EndpointConfig& c) {
    std::string addr_str = j.at("remote_addr").get<std::string>();
    c.remote_addr = asio::ip::make_address(addr_str);
    j.at("remote_port").get_to(c.remote_port);
}

void to_json(json& j, const EndpointConfig c) {
    j = json();
    j["remote_addr"] = c.remote_addr.to_string();
    j["remote_port"] = json(c.remote_port);
}


void from_json(const json& j, FactoryParameter<Agent::AgentAction>& x) {
    j.at("id").get_to(x.id);
}
void from_json(const json& j, FactoryParameter<price_t>& x) {
    x = {}; 
}


void to_json(json& j, const Subscribers::list_entry_t x) {
    j = json();
    j["id"] = x.id;
    j["pending_records"] = x.pending_records;
    j["endpoint"] = x.endpoint;
    j["record_type"] = x.record_type;
}

//
//
// *******************************************************



// static initialization


template<> std::map<FactoryParameter<Agent::AgentAction>, std::set<id_t>> Factory<Agent::AgentAction>::idmap = {};
template<> std::map<FactoryParameter<price_t>, std::set<id_t>> Factory<price_t>::idmap = {};

std::unordered_map<EndpointConfig, std::shared_ptr<Endpoint>, EndpointConfig::Key> 
Endpoints::endpoints;


// *******************************************************


Endpoint::Endpoint(EndpointConfig c) 
    :   config(c), socket(this->io_context),
        endpoint(c.remote_addr, c.remote_port),
        emitted(0)
{
   socket.open(asioudp::v4());
}

void Endpoint::emit(std::unique_ptr<json> j) {
    std::string s(j->dump());
    ++this->emitted;
    socket.send_to(asio::const_buffer(s.c_str(), s.size()), this->endpoint);
}



uintmax_t Subscribers::update(std::shared_ptr<Market::Market> m, const timepoint_t& tp) {
    std::lock_guard L { Subscribers::it_mtx };
    uintmax_t total = 0;

    for (auto& pair : Subscribers::idmap) {
        auto& s = pair.second;

        auto logex = [&tp, &s](std::string e) {
            LOG(ERROR)   
                << "update failed: (" 
                << " subscriber cursor=" << std::to_string(s->cursor.to_numeric())
                << " now=" << std::to_string(tp.to_numeric()) << "): "
                << e
            ;
        };

        try {
            // Subscriber object pulls latest data from temporary ts::view objects
            auto retpair = s->update(m, tp);
            total += retpair.second;
        } 
        // thrown by ts::view 
        catch (std::out_of_range& e) {
            logex(e.what());
            continue;
        }
        // thrown by Base_subscriber::update
        catch (std::invalid_argument& e) {
            logex(e.what());
            continue;

        }

    }

    return total;
}


std::variant<id_t, std::string>
Subscribers::add(std::shared_ptr<AbstractFactory> factory, Config config) {
    try {
        auto s_ptr = (*factory)(config);

        auto id = s_ptr->id;

        s_ptr->flags({{ subscriber_flag_t::Flushed }});

        Subscribers::idmap.insert({ id, std::move(s_ptr) });
        VLOG(5) << "added subscriber with ID " << id.to_string();


        return id;
    } catch (std::exception& e) {
        return e.what();
    }
}

std::deque< std::variant<id_t, std::string> >
Subscribers::add(std::deque<std::pair<std::shared_ptr<AbstractFactory>, Config>> c) {
    std::lock_guard L { Subscribers::it_mtx };

    std::deque< std::variant<id_t, std::string> > ret = {};

    // get id from instance and return 
    for (auto& pair : c) {
        ret.push_back(Subscribers::add(pair.first, pair.second));
    }

    return ret;
}


Subscribers::delete_status_t 
Subscribers::del(id_t id, bool sync) {
    using Ss = Subscribers;
    std::lock_guard L { Ss::it_mtx };
    auto it = Ss::idmap.find(id);

    if (it == Ss::idmap.end()) {
        return Ss::delete_status_t::DOES_NOT_EXIST;
    }

    if (sync == true) {
        (it->second)->wait_flag(subscriber_flag_t::Flushed);
    }

    // the subscriber's pending_records_count is an atomic type
    if ((it->second)->pending_records_count > 0) {
        (it->second)->flags({{ subscriber_flag_t::Dying }});
        return Ss::delete_status_t::MARKED;
    } else {
        Ss::idmap.erase(it);
        return Ss::delete_status_t::DELETED;
    }
}


std::deque<std::pair<id_t, Subscribers::delete_status_t>>
Subscribers::del(std::deque<id_t> ids, bool sync) {
    std::lock_guard L { Subscribers::it_mtx };

    std::deque<std::pair<id_t, delete_status_t>> ret;

    for (auto& id : ids) {
        ret.push_back({ id, Subscribers::del(id, sync) });
    }

    return ret;
}

// TODO add parameter
std::deque<Subscribers::list_entry_t> 
Subscribers::list() {
    using Ss = Subscribers;
    std::deque<Ss::list_entry_t> r;

    for (auto& [id, s] : Subscribers::idmap) {
        std::ostringstream os;
        os << s->config.endpoint;
        r.push_back({
            id,
            s->pending_records_count,
            os.str(),
            s->config.t
        });
    }

    return r;
}

void Subscribers::launch_manager_thread(int max_record_split) {
    VLOG(5) << "Subscribers::launch_manager_thread";

    std::unique_lock L { Subscribers::it_mtx, std::defer_lock };
    while (true) {

        L.lock();

        auto& idmap = Subscribers::idmap;

        if (Subscribers::shutdown_signal == true) {
            VLOG(4) << "Subscribers manager thread shutting down";
            return;
        }

        //VLOG(9) << "about to check Subscribers::idmap with " << idmap.size() << " elements";


        // check if the subscriber has been marked with the Dying flag (for shutdown)
        // if so, flush its remaining records regardless of count and then destroy it
        auto is_dying = [](std::unique_ptr<AbstractSubscriber>& s) {
            return s->flags().contains(subscriber_flag_t::Dying);
        };

        // 
        for (auto& [id,s] : idmap) {

            if (s->pending_records_count > s->config.chunk_min_records || is_dying(s)) {
                auto json_records = s->convert_pending(max_record_split);

                auto endpoint = Endpoints::endpoints.find(s->config.endpoint);
                if (endpoint == Endpoints::endpoints.end()) {
                    LOG(ERROR) << "did not find any endpoints for subscriber with ID=" 
                        << s->id.to_string();
                } else {
                    for (auto& json_ptr : json_records) {
                        (*endpoint).second->emit(std::move(json_ptr));
                    }

                    VLOG(9) << "emitted data from subscriber with ID=" << s->id.to_string();
                }
            }

        };

        std::erase_if(idmap, [&is_dying](auto& item) {
            return is_dying(item.second);
        });

        int poll_interval = manager_thread_poll_interval.load();

        // if the poll_interval is changed to an invalid value, we stop permanently
        // TODO - we could instead wait on a CV to restart
        if (poll_interval <= 0) {
            break;
        }

        L.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval));
    }
}



AbstractSubscriber::AbstractSubscriber(Config& c)
    : config(c), id(id_t {}) 
{
    EndpointConfig& ec = c.endpoint;
    auto it = Endpoints::endpoints.find(ec);

    if (it == Endpoints::endpoints.end()) {
        VLOG(7) << "creating new endpoint for subscriber with ID=" << this->id.to_string();

        this->endpoint = std::shared_ptr<Endpoint>(new Endpoint(ec));
        Endpoints::endpoints.insert({ ec, this->endpoint });
        // the shared_ptr now has use_count of 2
    } else {
        VLOG(7) << "using existing endpoint for subscriber with ID=" << this->id.to_string();
        this->endpoint = it->second;
    }
}

AbstractSubscriber::~AbstractSubscriber() {
    auto& config = this->endpoint->config;

    // if our copy of the endpoint is the only one remaining (aside from the copy in Endpoints)
    // delete it from the main container
    if (this->endpoint.use_count() == 2) {
        auto it = Endpoints::endpoints.find(config);
        if (it == Endpoints::endpoints.end()) {
            LOG(ERROR)  << "endpoint was prematurely removed from Endpoints::endpoints"
                        << "config=" << json(config).dump()
            ;

            return;
        }

        Endpoints::endpoints.erase(it);
    }
}

void AbstractSubscriber::reset(const timepoint_t& t) {
    std::lock_guard { this->mtx };
    this->cursor = t;
}





template<>
ts<price_t>::view get_iterator_helper(
    std::shared_ptr<Market::Market> m, const timepoint_t& tp, FactoryParameter<price_t> p)
{
    return m->price_iterator(tp);
}

template<>
ts<Agent::AgentAction>::view get_iterator_helper(
    std::shared_ptr<Market::Market> m, const timepoint_t& tp, FactoryParameter<Agent::AgentAction> p)
{
    return m->agent_action_iterator(tp, p.id);
}

};
