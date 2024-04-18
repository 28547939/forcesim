#ifndef SUBSCRIBERCOMMON_H
#define SUBSCRIBERCOMMON_H

#include "../types.h"
#include "../Market.h"
#include "../Info.h"
#include "../json_conversion.h"


#include <glog/logging.h>

#include <asio.hpp>


#include <chrono>
#include <thread>
#include <variant>
#include <queue>
#include <unordered_map>
#include <memory>


namespace Subscriber {

using agentid_t = Market::agentid_t;
using timepoint_t = timepoint_t;

using asioudp = asio::ip::udp;

// subscriber ID
typedef numeric_id<subscriber_numeric_id_tag> id_t;

// type of subscriber - corresponds to AgentAction, price_t, or Info::infoset_t as the 
// actual type of the records processed by the subscriber
enum class record_type_t { AGENT_ACTION, PRICE, INFO };

enum class subscriber_flag_t {
    // marked when the program has requested destruction/shutdown of a subscriber but
    // pending_records_count > 0, in which case those records need to be processed
    // and emitted to endpoints before the subscriber can be deleted
    Dying,

    // whether the subscriber has finished processing all available records
    // this is set to false when Base_subscriber::update processes new records and makes them 
    //  available to be emitted
    // this is set to true when all the currently processed records have been converted, removed
    //  from the deque of processed records, and ready to be emitted to the consumer / UDP endpoint
    // this flag is also used to keep track of when to add a trailing empty record
    //  at the end of the JSON to be emitted, to signal to the consumer that all records have been
    //  processed
    Flushed
};

std::string record_type_t_str(const record_type_t&);


// *************************

struct EndpointConfig {
    asio::ip::address remote_addr;
    int remote_port;

    EndpointConfig(std::string addr_str, int port) 
        :   remote_addr(asio::ip::make_address(addr_str)),
            remote_port(port)
    {}

    EndpointConfig(asio::ip::address addr, int port) 
        :   remote_addr(addr),
            remote_port(port)
    {}

    bool operator==(const EndpointConfig& e) const {
        return 
            e.remote_addr.to_string() == this->remote_addr.to_string()
            && this->remote_port == e.remote_port;
    }

    // for std::map in the Subscribers class
    struct Key {
        size_t operator()(const Subscriber::EndpointConfig& e) const {
            return std::hash<std::string>{}(
                e.remote_addr.to_string() + std::to_string(e.remote_port)
            );
        }
    };
};

inline std::ostream& operator<<(std::ostream& os, const EndpointConfig& c) {
    return os << (c.remote_addr.to_string() + ":" + std::to_string(c.remote_port));
}

class Endpoints;


struct Endpoint {
    private:
    asio::io_context io_context;
    protected:
    asioudp::socket socket;
    asioudp::endpoint endpoint;
    std::size_t emitted;

    public:
    EndpointConfig config;

    Endpoint(EndpointConfig c);
    void emit(std::unique_ptr<json> j);

    friend class Endpoints;
};

// *************************


// configuration for subscriber objects
struct Config {
    record_type_t t;

    EndpointConfig endpoint;

    // Send data to the subscriber at every `granularity` steps of timepoint_t 
    uintmax_t granularity;

    // wait until thre are chunk_min_records which are converted to JSON before emitting
    // to the endpoint
    // (exception: when we shutdown the subscriber, any remaining pending records are sent)
    uintmax_t chunk_min_records;
};


class AbstractSubscriber;


// base class for Factory classes - see definition of Factory template later on
struct AbstractFactory {
    // construct a subscriber object, which immediately becomes abstract
    virtual std::unique_ptr<AbstractSubscriber> operator()(Config config) = 0;

    // when a Factory has been associated with a (unique) subscriber, we can
    // wait for that subscriber to reach a certain point in time in its consumption 
    // of data (see Factory definitions later on)
    virtual bool wait(const timepoint_t&) = 0;
};

template<typename T>
struct Factory;


struct Endpoints {
    private:
    /*
        Endpoints are kept in a global map and subscribers check an endpoint's refcount during 
        subscriber destruction. An endpoint that is no longer used will be deleted. Multiple uses
        of the same UDP endpoint will reference the single Endpoint object representing that 
        UDP endpoint, as stored here.

        We use this separate "Endpoints" class to allow both the AbstractSubscriber and Subscribers classes 
        to access the endpoints map. AbstractSubscriber should have access to allow automatic Endpoint 
        refcount decrementing when a subscriber is destroyed, since that functionality is the same
        across all subscribers.
        The static Subscribers class (see below) needs access in order to create the Endpoint when an 
        AbstractSubscriber instance is added, and in order to emit the  JSON records that are processed 
        by the subscribers.

        Keeping the map in this separate class allows us to share the data without giving AbstractSubscriber
        or Subscribers friend access to one another.
    */
    static std::unordered_map<EndpointConfig, std::shared_ptr<Endpoint>, 
        EndpointConfig::Key> endpoints;


    friend class AbstractSubscriber;
    friend class Subscribers;

    typedef std::unordered_map<EndpointConfig, std::pair<std::size_t, std::size_t>, EndpointConfig::Key>
    descret_t;

    public:
    // EndpointConfig maps to (total emitted, refcount)
    static inline descret_t
    describe() {
        descret_t ret;

        std::transform(endpoints.begin(), endpoints.end(), std::inserter(ret, ret.end()),
        [](auto&& pair) -> descret_t::value_type {
            
            return {
                pair.first, 
                {
                    pair.second->emitted,
                    pair.second.use_count()
                }
            };
        });
        return ret;
    }
};




};


#endif
