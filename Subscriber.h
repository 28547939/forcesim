#ifndef SUBSCRIBER_H
#define SUBSCRIBER_H

#include "types.h"
#include "Market.h"
#include "Info.h"
#include "json_conversion.h"


#include <glog/logging.h>
#include <nlohmann/json.hpp>

#include <boost/asio.hpp>


#include <chrono>
#include <thread>
#include <variant>
#include <queue>
#include <memory>


/*
Subscribers operate as follows:
- Pull timeseries data out of the Market instance: 
    - Base_subscriber::update on each subscriber instance, which is called by Subscribers::update,
        retrieves a ts::view or ts::sparse_view from one of the Market "iterator" methods
    - Data ("records", one record for each point in time) is placed in a container for conversion 
        by another thread
- Convert the data to JSON and send: 
    - Base_subscriber::convert_chunk converts a certain number of records
        - Called by a separate thread - Subscribers::launch_manager_thread    
    - Send to the Endpoint(s) configured for that subscriber

*/

namespace Subscriber {

using agentid_t = Market::agentid_t;
using timepoint_t = timepoint_t;

using boostudp = boost::asio::ip::udp;
namespace ba = boost::asio;

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
    ba::ip::address remote_addr;
    int remote_port;

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


struct Endpoint {
    protected:
    std::shared_ptr<boostudp::socket> socket;
    boostudp::endpoint endpoint;

    public:
    EndpointConfig config;

    Endpoint(EndpointConfig c);
    void emit(std::unique_ptr<json> j);

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
};




// base class for all subscriber objects
// permits us to keep all subscribers in a single data structure (see static Subscribers class)
// without using variants (using dynamic dispatch instead)
class AbstractSubscriber {
    std::set<enum subscriber_flag_t> _flags;
    std::recursive_mutex flags_cv_mtx;
    std::condition_variable_any flags_cv;

    protected:

    std::shared_ptr<Endpoint> endpoint;
    std::recursive_mutex mtx;
    std::condition_variable_any wait_cv;

    public:
    const Config config;
    const id_t id;

    AbstractSubscriber(Config& c);
    // decrement Endpoint refcount / destroy Endpoint
    virtual ~AbstractSubscriber();


    // the cursor is the timepoint associated with the most recent record that has been
    //  processed by the subscriber; the next timepoint to processed will be 
    //  this->cursor + `granularity`, unless the cursor is nullopt, in which case
    //  the next timepoint to be processed will be 0
    // where `granularity` is the granularity specified in this->config.
    timepoint_t cursor;

    // records which have been obtained by the subscriber but which await conversion and 
    // emitting to the endpoint; the data structure holding the records is specific to the
    // template/subscriber type, but this count needs to be available to the program (which
    // only has access to this abstract type) to signal that the subscriber is ready to convert 
    // the records to JSON and release them.
    std::atomic<uintmax_t> pending_records_count;

    // reset the subscriber's cursor
    virtual void reset(const timepoint_t& t);

    // manage flags set on this subscriber
    std::set<enum subscriber_flag_t> 
    flags(
        std::optional<std::set<enum subscriber_flag_t>> flags_arg = std::nullopt,
        // if true, use flags_arg to toggle; if false, only use flags_arg to add flags
        bool toggle = true
    ) {
        std::lock_guard L { this->mtx }; 

        if (flags_arg.has_value()) {
            auto flags_arg_v = flags_arg.value();

            if (toggle == true) {
                std::erase_if(this->_flags, [&flags_arg_v](auto const& f) {
                    if (flags_arg_v.contains(f)) {
                        // facilitate further usage of flags_arg_v to add, instead of delete, flags
                        flags_arg_v.erase(f);
                        return true;
                    }

                    return false;
                });
            }

            this->_flags.merge(flags_arg.value());
        }

        return this->_flags; 
    }
    void reset_flags() { 
        std::lock_guard L { this->mtx };
        this->_flags.clear(); 
    }
    // wait until a specific flag is set
    void wait_flag(enum subscriber_flag_t f) {
        if (this->flags().contains(f)) {
            return;
        } else {
            std::unique_lock L(this->flags_cv_mtx);
            this->flags_cv.wait(L, [&f, this]() {
                return this->flags().contains(f);
            });
        }
    }

    // wait until the Subscriber has processed all records that are currently available, 
    //  i.e., until the Subscriber is completely up to date.
    //  specifically, this happens directly after the subscriber stores its newly obtained records
    //      in its internal data structure, but before the records have been converted to JSON
    //      and emitted to an endpoint.
    //      to wait further, for the point in time directly after the subscriber converted the records
    //          to JSON and relinquished control of them to the client (Subscribers class manager thread),
    //              use wait_flag(subscriber_flag_t::Flushed)
    //
    // the optional argument to wait() will cause us to additionally wait until the subscriber has processed
    //  records up to and including the specified timepoint
    //      note that, when the Market stops, the subscriber will stop reading at a point less than
    //          the Market's current time iff $granularity > 1, in which case providing the Market's
    //          current time to wait() will
    //          result in waiting indefinitely (until the Market restarts and the Subscriber passes that time)
    virtual void wait(const std::optional<timepoint_t> t) final {
        std::unique_lock L(this->mtx);

        this->wait_cv.wait(L, [t, this]() {
            // cursor represents next value to be read
            return t.has_value() ? this->cursor - this->config.granularity >= t : true;
        });
    }


    // the exact way that the subscriber obtains and stores its records depends on the 
    // subscriber type; the client code (Market class) needs access to this method to trigger 
    // this process (Market does this between iteration blocks)
    virtual std::pair<uintmax_t, uintmax_t>
    update(std::shared_ptr<Market::Market>, const timepoint_t&) = 0;

    // when appropriate, the program will trigger the conversion of a subscriber's pending
    // records (their existence being reflected in pending_record_count) to a sequence of JSON
    // objects, which are intended to be ready to send to the UDP endpoint as-is (i.e. each JSON
    // object will be exactly the payload of a UDP message)
    //      currently this conversion process is triggered by the Subscibers class manager thread
    //
    // part of this process is the same across subscribers, which we are able to define here;
    // the type-specific part of the process is handled by convert_chunk, below.
    //
    // each JSON object is referred to as a 'chunk', and it is a self-contained JSON data structure,
    //  possibly with some metadata
    //
    // max_records should be chosen somehow to ensure that no chunk will exceed
    // the maximum length of the underlying Endpoint transport (theoretically ~64k 
    // for UDP, but should be smaller)
    std::deque<std::unique_ptr<json>> 
    convert_pending(int max_records) {
        std::unique_lock L { this->mtx, std::defer_lock };

        std::deque<std::unique_ptr<json>> ret = {};
        while (true) {
            L.lock();
            auto ptr = this->convert_chunk(max_records);
            L.unlock();

            if (ptr.has_value()) {
                ret.push_back(std::move(ptr.value()));
            } else {
                break;
            }
        }

        // TODO set value based on actual number of records processed, eg in case
        // of exception
        this->pending_records_count = 0;

        // set Flushed flag
        this->flags({{ subscriber_flag_t::Flushed }}, false);

        return ret;
    }

    // Convert multiple remaining records, assembling them into a JSON object that
    //  is ready to be sent to an endpoint in its own packet (i.e. one packet/datagram per chunk)
    // Takes records from pending_records (see Base_subscriber, below)
    virtual std::optional<std::unique_ptr<json>> 
    convert_chunk(int) = 0;

};




// static class which manages all the subscribers and calls their update and JSON conversion
// methods
class Subscribers {
    // subscribers are stored here
    static inline std::unordered_map<id_t, std::unique_ptr<AbstractSubscriber>, id_t::Key> idmap;
    // mutex for idmap
    static inline std::recursive_mutex it_mtx;

    public:

    //  milliseconds between scans over the subscriber map (idmap member here), to check 
    //      for any pending records (and convert them to JSON)
    //      the program can change the poll interval during runtime by modifying this variable 
    static inline std::atomic<int> manager_thread_poll_interval;

    // TODO not yet implemented
    static inline std::atomic<bool> shutdown_signal;

    // call update on all the subscribers
    static uintmax_t update(std::shared_ptr<Market::Market>, const timepoint_t&);

    // add one or more subscribers
    // variant contains either the ID associated with the successful operation, or a string error message
    static std::variant<id_t, std::string>
    add(std::pair<std::shared_ptr<AbstractFactory>, Config> pair);
    static std::deque< std::variant<Subscriber::id_t, std::string> >
    add(std::deque<std::pair<std::shared_ptr<AbstractFactory>, Config>> c);

    // result of a delete operation
    enum class delete_status_t { DELETED, MARKED, DOES_NOT_EXIST };

    // delete one or more subscribers
    // if sync is true, then call wait_flag(subscriber_flags_t::Flushed) on the subcriber
    // first, to wait for it to release all of its remaining records
    static std::deque<std::pair<id_t, delete_status_t>>
    del(std::deque<id_t>, bool sync = false);
    static delete_status_t 
    del(id_t, bool sync = false);

    // list/describe all subscribers
    struct list_entry_t {
        id_t id;
        uintmax_t pending_records;
        std::string endpoint;
        record_type_t record_type;
    };
    static std::deque<list_entry_t> 
    list();

    // call the wait method on the subscriber with a given id (see AbstractSubscriber::wait)
    static bool wait(const id_t& id, const timepoint_t& tp) {
        auto it = idmap.find(id);
        if (it == idmap.end()) {
            return false;
        }

        (it->second)->wait(tp);
        return true;
    }

    // to be called once; the manager thread periodically calls subscribers' JSON conversion
    // methods, and then emits the data to the endpoints
    // max_record_split is the maximum number of records that should be included in a single JSON
    //  conversion (UDP message)
    static void launch_manager_thread(int max_record_split);
};




// static association between the template type parameters and our enum values
template<typename T>
struct constraint {};
template<> struct constraint<AgentAction> { inline static const record_type_t t = record_type_t::AGENT_ACTION; };
template<> struct constraint<price_t> { inline static const record_type_t t = record_type_t::PRICE; };
template<> struct constraint<Info::Abstract> { inline static const record_type_t t = record_type_t::INFO; };


// templated mix-in sitting below AbstractSubscriber and above the specific Subscriber (Impl) classes
template<typename RecordType, 
    typename = std::enable_if_t<
        std::is_same<RecordType, AgentAction>::value || 
        std::is_same<RecordType, price_t>::value
>>
class Base_subscriber : public AbstractSubscriber {
    protected:

    // records that the subscriber has obtained from the Market but which have not yet been
    // converted to JSON to be sent to the UDP endpoints
    std::map<timepoint_t, const RecordType> pending_records;

    // set during convert_chunk to maintain state during multiple calls to convert_chunk
    // from a caller's loop
    std::atomic<bool> flush_ready;

    // A subscriber stores a copy of a factory to make it possible for other classes to identify
    // this specific subscriber instance based on its ID and the parameters used to create
    // the subscriber.
    // The factory class has a static map which tracks the existence of this subscriber instance
    // with its ID; this map is automatically updated when the subscriber is destroyed
    std::unique_ptr<Factory<RecordType>> factory; 

    public:
    Base_subscriber(Config& c, std::unique_ptr<Factory<RecordType>> f) 
        : AbstractSubscriber(c), factory(std::move(f))
    {

        // runtime check to make sure we have a Config of the correct "type", in the absence
        // of Config itself being a template
        if (constraint<RecordType>::t != this->config.t) {
            std::ostringstream e;
            e   << "called with type " 
                << record_type_t_str(constraint<RecordType>::t)
                << " on an Endpoint of record_type_t="
                << record_type_t_str(this->config.t);

            throw std::invalid_argument(e.str());
        }

        // our unique Factory instance is now aware of our ID
        // destruction of this Base_subscriber instance will trigger the Factory instance's 
        //  destructor
        this->factory->associate(this->id);
    }

    // this function is called by the Subscribers class manager thread
    // most of the chunk conversion process is the same for all subscribers
    // part that is specific to specific subscriber classes is in convert_chunk_impl
    virtual std::optional<std::unique_ptr<json>> 
    convert_chunk(int max_records) final {
        std::lock_guard L { this->mtx };

        auto& m = this->pending_records;
        auto size = m.size();
        auto begin = m.begin();
        auto end = begin;
        std::advance(end, max_records > size ? size : max_records);

        // move the records out of the main pending_records map and into this map
        // one alternative would be to iterate over pending_records, constructing JSON
        // for each key, then combine the JSON entries into the resulting JSON array
        // it's not possible to provide begin/end iterators for a container to the json 
        //      constructor
        std::map<timepoint_t, const RecordType> output;

        // we have already processed all the records
        // convert_chunk_impl is provided an empty JSON object; when the consumer receives 
        // this, it signals that we are finished sending
        if (end == begin && !this->flush_ready) {
            this->flush_ready = true;
        } 
        // we have already send the empty JSON object - signal to the caller that 
        // convert_chunk does not need to be called again
        else if (this->flush_ready == true) {
            this->flush_ready = false;
            return std::nullopt;
        }
        // otherwise move the pending records to the output map
        // TODO use map::extract
        //  or, alternatively, use a queue of pairs instead of a map
        else {
            std::move(begin, end, 
                std::insert_iterator<std::map<timepoint_t, const RecordType>>(output, output.end()));
            m.erase(begin, end);
        }

        return this->convert_chunk_impl(std::move(json(output)));
    }

    // take the raw JSON chunk (sequence of records) and prepare it for transmission;
    // specific to the subscriber type
    // generally just adds some metadata / an appropriate JSON object key
    virtual std::unique_ptr<json> convert_chunk_impl(json&&) = 0;

    // Returns 
    //  { period of time processed, total new records processed (new_pending_records) }
    //
    // Period of time processed is granularity * new_pending_records, unless there 
    // were errors during the loop, or empty entries.
    //
    // The period of time processed is always 
    //      ending cursor  -  (starting cursor   -   granularity)
    // since the period of time begins from the last element that we processed, and that element
    // is always granularity steps away from the first elemen that we process here (starting cursor)
    virtual std::pair<uintmax_t, uintmax_t>
    update(std::shared_ptr<Market::Market> m, const timepoint_t& m_now) final {
        std::unique_lock L { this->mtx, std::defer_lock };
        L.lock();

        auto granularity = this->config.granularity;

        // cursor is the next unread element, as set during the previous call to update, or
        /// 0 if this is the first time
        timepoint_t tp = this->cursor;
        // TODO check in range

        if (tp > m_now) {
            std::ostringstream e;
            e << "update: subscriber cursor is ahead of timepoint_t argument:"
                << " cursor=" << std::to_string(tp.to_numeric()) 
                << " m_now=" << std::to_string(m_now.to_numeric())
            ; 

            throw std::invalid_argument(e.str());
            // caught in Subscribers::update
        }

        auto live_cursor = this->factory->get_iterator(m, tp);

        int new_pending_records = 0;
        for ( ; tp < m_now; tp += granularity, live_cursor += granularity) {

            if (live_cursor.has_value()) {
                this->pending_records.insert(
                    this->pending_records.end(), 
                    { tp, *live_cursor }
                );
                new_pending_records++;
            }
        }

        // need to add granularity to account for the period elapsed between the 
        // last record processed during the previous invocation and the first record
        // processed during this invocation - this is exactly equal to granularity
        uintmax_t period = tp - this->cursor + granularity;

        // cursor points to the next element (in the future) to be read
        // it's set in the `tp += granularity` expression in the for loop above,
        // so the cursor is always progressing in steps of `granularity`
        this->cursor = tp;

        VLOG(9) << "subscriber updated " << new_pending_records << " records";

        // new_pending_records is positive (unless there were exceptions/empty entries in the loop)
        // so there are definitely new records pending, and we are not in the 'flushed'
        // state (regardless of whether we were to begin with)
        // also: at this point, this subscriber is now completely up to date with new records.
        if (new_pending_records > 0) {
            this->flags({{ subscriber_flag_t::Flushed }});

            // conversion thread checks this 
            this->pending_records_count += new_pending_records;

            // waiting threads can check if we have reached their specified timepoint
            this->wait_cv.notify_all();
        }

        L.unlock();

        return { period, new_pending_records };
    }

};



// `Impl` are the actual Subscriber classes, and are stored in the Subscribers::idmap
// datastructure.
// Impl should not be instantiated directly by the program; Factory (see below) must 
// be used; if not, the subscriber's ID and/or parameter will not be tracked correctly.

template<typename T>
class Impl;

template<>
class Impl<AgentAction> : public Base_subscriber<AgentAction>
{
    public:
    Impl(Config& config, std::unique_ptr<Factory<AgentAction>> f)  
        : Base_subscriber<AgentAction>(config, std::move(f))
    {}

    const agentid_t agent_id;

    virtual std::unique_ptr<json>
    convert_chunk_impl(json&& json_map) {
        json r_t(this->config.t);

        // chunk will be a JSON object of the form
        // { "AGENT_ACTION": {
        //      (agent ID): (list of timestamp_t, AgentAction pairs)
        //  }}
        json* jptr = new json(json::object_t({
            { r_t.get<std::string>(), {
                { this->agent_id,  json_map }
            }}
        }));

        std::unique_ptr<json> chunk(jptr);

        return std::move(chunk);
    }
};

template<>
class Impl<price_t> : public Base_subscriber<price_t>
{
    public:
    Impl(Config& config, std::unique_ptr<Factory<price_t>> f)
        : Base_subscriber<price_t>(config, std::move(f))
    {}

    // chunk will be a JSON object of the form
    // { "PRICE": (list of timestampt_t, price_t pairs) }
    virtual std::unique_ptr<json>
    convert_chunk_impl(json&& json_map) {
        json r_t(this->config.t);

        json* jptr = new json(json::object_t({
            { r_t.get<std::string>(), json_map }
        }));

        auto juniq = std::unique_ptr<json>(jptr);
        return std::move(juniq);
    }
};



// a subscriber instance, depending on its RecordType, is associated with a parameter
// that is used to select the data that it obtains from the Market and transmit to endpoints.
// the parameter must be provided to the Factory to create the subscriber object (see 
// Factory::operator(), below). A subscriber instance always has exactly one parameter
// throughout its lifetime.
//
// the Factory class, used as the factory for subscribers, also maintains a static internal map 
// to track which subscriber instances are associated with which parameters (see Factory::idmap,
// below), using the subscriber IDs; and subscriber instances can be found from their IDs using the 
// main map in the Subscribers class.
//
// in general the FactoryParameter can take any form, so we represent it as a struct; a null/trivial
// parameter is just represented by an empty struct, as in the case of price_t.
// 
// in the future, we might have RecordType = Info (with parameter being the type of Info) or 
// RecordType = AgentTrace (parameter being agent ID)

template<typename RecordType>
struct FactoryParameter;

template<>
struct FactoryParameter<AgentAction> {
    Market::agentid_t id;
    auto operator<=> (const FactoryParameter<AgentAction>&) const = default;
};

template<>
struct FactoryParameter<price_t> {
    auto operator<=> (const FactoryParameter<price_t>&) const = default;
};


// get_iterator_helper mediates between calls to get_iterator made to a Factory instance, and the 
// corresponding generic iterator-getter method present in Market
// it allows the get_iterator calls to be indexed by RecordType, whereas the Market API specifies the 
//  method names explicitly (and with different argument lists) - price_iterator, agent_action_iterator,
//  and info_iterator
template<typename RecordType>
typename ts<RecordType>::view 
get_iterator_helper(std::shared_ptr<Market::Market>, const timepoint_t&, FactoryParameter<RecordType>);


template<typename RecordType>
struct Factory : AbstractFactory
{
    protected:

    // ID of the subscriber that this factory instance is associated with, if any
    // the Base_subscriber constructor calls Factory::associate to ensure that,
    // upon destruction of that subscriber instance (and destruction of the Base_subscriber),
    // the Factory will know which ID to clear from its map when it itself is destroyed.
    // (see associate, ~Factory, delete_id below)
    //
    // The Factory is destroyed because an instance of it is stored in the Base_subscriber
    // during construction of the subscriber. (see operator(), below)

    std::optional<id_t> id;
    inline static typename std::map<FactoryParameter<RecordType>, std::set<id_t>> idmap;
    inline static std::mutex id_mtx;

    FactoryParameter<RecordType> param; 


    // called from the destructor to remove the idmap entry that we created in the `associate` 
    // method below
    static inline void delete_id(std::optional<id_t>& id) {
        std::lock_guard L(id_mtx);

        if (id.has_value()) {
            VLOG(7) << "deleting subscriber from idmap  ID=" << id.value().to_string();

            for (auto it = idmap.begin() ; it != idmap.end() ; it++) {
                auto& set = it->second;
                auto it_ = set.find(id.value());

                if (it_ != set.end()) {
                    set.erase(it_);
                }
            }
        } else {
            VLOG(7) << "deleting all subscribers from idmap";
            idmap.clear();
        }
    }

    public:
    Factory(FactoryParameter<RecordType> p) 
        : param(p) 
    {}


    /*  When the unique factory instance stored in a subscriber (via Base_subscriber) is destroyed
        (which should only happen when the subscriber is destroyed), the 
        factory instance updates the status map to remove the subscriber's
        ID from the type-specific map.

        If this instance is not associated with a Subscriber, no IDs are deleted
        from the idmap.
    */
    virtual inline ~Factory() {
        // only call delete_id if `associate` has been called; otherwise, 
        // calling delete_id with an empty optional results in all IDs
        // being deleted
        if (this->id.has_value()) {
            delete_id(this->id);
        }
    }

    virtual std::unique_ptr<AbstractSubscriber> 
    operator()(Config config) final {
        auto copy = std::make_unique<Factory<RecordType>>(*this);
        auto ptr = new Impl<RecordType>(config, std::move(copy));

        return std::unique_ptr<AbstractSubscriber>(ptr);
    }


    virtual typename ts<RecordType>::view
    get_iterator(std::shared_ptr<Market::Market> m, const timepoint_t& tp) {
        return get_iterator_helper<RecordType>(m, tp, this->param);
    }

    // Make this factory instance the unique instance associated with/stored in 
    // a subscriber instance.
    // This allows our FactoryBase instance to be destroyed exactly when that 
    // subscriber is destroyed. When the FactoryBase is destroyed, delete_id 
    // is called, which removes that subscriber from the internal map for this
    // subscriber type.
    virtual bool associate(const id_t& id) final {
        if (this->id.has_value()) {
            return false;
        } else {
            this->id = id;

            // track the association of this subscriber with its parameter
            auto it = idmap.find(this->param);
            if (it == idmap.end()) {
                idmap.insert({ this->param, { id } });
            } else {
                (it->second).insert(id);
            }
            return true;
        }
    }

    virtual bool wait(const timepoint_t& tp) {
        return wait_matching(this->param, tp);
    }

    // wait for all subscribers which have our parameter
    static bool wait_matching(FactoryParameter<RecordType> x, const timepoint_t& tp) {
        auto idmap = Factory<RecordType>::idmap;
        auto it = idmap.find(x);

        if (it == idmap.end()) {
            return false;
        } else {
            std::for_each(idmap.begin(), idmap.end(), [](auto pair) {
                auto idset = pair.second;
                std::for_each(idset.begin(), idset.end(), [](id_t id) {
                    
                    // TODO 
                });
            });
        }

        return true;
    }

    static bool delete_matching(FactoryParameter<RecordType> x) {
        auto it = idmap.erase(x);
        if (it == idmap.end()) {
            return false;
        }

        return true;
    }
};


// json conversion 
//
void to_json(json& j, const record_type_t t);

/*
namespace ::boost::asio::ip {
    void from_json(const json& j, ba::ip::address& addr);
};
*/

void from_json(const json&, Config&);

void from_json(const json&, EndpointConfig&);
void to_json(json& j, const EndpointConfig c);

void from_json(const json&, FactoryParameter<AgentAction>&); 
void from_json(const json&, FactoryParameter<price_t>&); 


void to_json(json& j, const Subscribers::list_entry_t x);


//
//

};


#endif