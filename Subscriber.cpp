

#include "types.h"
#include "Market.h"
#include <glog/logging.h>
#include <nlohmann/json.hpp>

#include "json_conversion.h"

#include <boost/asio.hpp>

#include <iostream>

#include <chrono>
#include <thread>
#include <variant>
#include <queue>
#include <memory>


#include "Subscriber.h"


using boostudp = boost::asio::ip::udp;
namespace ba = boost::asio;



namespace Subscriber {


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
void from_json(const json& j, ba::ip::address& addr) {
    std::string addr_str = j.get<std::string>();
    addr = ba::ip::make_address(addr_str);
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
    c.remote_addr = ba::ip::make_address(addr_str);
    j.at("remote_port").get_to(c.remote_port);
}


void from_json(const json& j, FactoryParameter<AgentAction>& x) {
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


template<> std::map<FactoryParameter<AgentAction>, std::set<id_t>> Factory<AgentAction>::idmap = {};
template<> std::map<FactoryParameter<price_t>, std::set<id_t>> Factory<price_t>::idmap = {};

//std::atomic<int> 
//Subscribers::manager_thread_poll_interval;

std::unordered_map<EndpointConfig, std::shared_ptr<Endpoint>, EndpointConfig::Key> 
Endpoints::endpoints;


// *******************************************************


Endpoint::Endpoint(EndpointConfig c) : config(c) {
    ba::io_context io_context;
    this->endpoint = boostudp::endpoint(c.remote_addr, c.remote_port);
    this->socket = std::shared_ptr<boostudp::socket>(
        new boostudp::socket(io_context)
    );
    socket->open(boostudp::v4());
}

void Endpoint::emit(std::unique_ptr<json> j) {
    std::string s(j->dump());
    socket->send_to(ba::const_buffer(s.c_str(), s.size()), this->endpoint);
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
Subscribers::add(std::pair<std::shared_ptr<AbstractFactory>, Config> pair) {
    auto factory = pair.first;
    auto config = pair.second;

    try {
        auto s_ptr = (*factory)(config);

        auto id = s_ptr->id;

        s_ptr->flags({{ subscriber_flag_t::Flushed }});

        Subscribers::idmap.insert({ id, std::move(s_ptr) });
        VLOG(5) << "added subscriber with ID " << id;

        EndpointConfig& ec = config.endpoint;
        auto it = Endpoints::endpoints.find(ec);

        if (it == Endpoints::endpoints.end()) {
            VLOG(7) << "creating new endpoint for subscriber with ID=" << id;

            std::shared_ptr<Endpoint> endpoint(new Endpoint(ec));
            Endpoints::endpoints.insert({ ec, endpoint });
        } else {
            VLOG(7) << "using existing endpoint for subscriber with ID=" << id;
        }

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
        ret.push_back(Subscribers::add(pair));
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

    if ((it->second)->pending_records_count > 0) {
        (it->second)->flags({{ subscriber_flag_t::Dying }});
        return Ss::delete_status_t::MARKED;
    } else {
        Ss::idmap.erase(it);
        return Ss::delete_status_t::DELETED;
    }
}


// TODO mutex lock - error is because it's already locked by the thread
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

/*
deleting a subscriber

need set<flag> on subscriber
if Dying flag / Flushed flag, manager thread should ignore minimum record config - send all records
then delete the subscriber after emitting
*/

void Subscribers::launch_manager_thread(int max_record_split) {
    VLOG(5) << "Subscribers::launch_manager_thread";

    std::unique_lock L { Subscribers::it_mtx, std::defer_lock };
    while (true) {

        L.lock();

        auto& idmap = Subscribers::idmap;

        /*  Subscriber manager_thread shutdown does not free resources owned by the Subscribers
            class or by individual Subscribers
        */
       /*
        if (Subscribers::shutdown_signal == true) {
            VLOG(4) << "Subscribers manager_thread shutting down";
            return;
        }
        */

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
                    LOG(ERROR) << "did not find any endpoints for subscriber with ID=" << s->id;
                } else {
                    for (auto& json_ptr : json_records) {
                        (*endpoint).second->emit(std::move(json_ptr));
                    }

                    VLOG(9) << "emitted data from subscriber with ID=" << s->id;
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
    : config(c), id(AbstractSubscriber::next_id++) 
{
}

AbstractSubscriber::~AbstractSubscriber() {
    // TODO check endpoint refcounts; if =1 then delete
}

void AbstractSubscriber::reset(const timepoint_t& t) {
    std::lock_guard { this->mtx };
    this->cursor = t;
}

id_t AbstractSubscriber::next_id = 0;






template<>
ts<price_t>::view get_iterator_helper(
    std::shared_ptr<Market::Market> m, const timepoint_t& tp, FactoryParameter<price_t> p)
{
    return m->price_iterator(tp);
}

template<>
ts<AgentAction>::view get_iterator_helper(
    std::shared_ptr<Market::Market> m, const timepoint_t& tp, FactoryParameter<AgentAction> p)
{
    return m->agent_action_iterator(tp, p.id);
}




/*
template<typename T, typename Param>
FactoryBase<T, Param>::~FactoryBase() {
        // only call delete_id if `associate` has been called; otherwise, 
        // calling delete_id with an empty optional results in all IDs
        // being deleted
        if (this->id.has_value()) {
            delete_id(this->id);
        }
}
*/




/*
template<typename T, typename R>
Base_subscriber<T,R>::Base_subscriber(Config& c, std::unique_ptr<Factory<T>> f) 
    
{

}

~Base_subscriber<T>::Base_subscriber() {

}

std::pair<uintmax_t, std::optional<std::string>>
Base_subscriber<T>::update(const Market::timepoint_t& now) {
}
*/

};