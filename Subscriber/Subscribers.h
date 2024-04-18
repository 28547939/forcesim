#ifndef SUBSCRIBERS_H
#define SUBSCRIBERS_H

#include "../types.h"
#include "../Market.h"
#include "../Info.h"
#include "../json_conversion.h"

#include "common.h"

#include <glog/logging.h>
#include <nlohmann/json.hpp>

#include <asio.hpp>


#include <chrono>
#include <thread>
#include <variant>
#include <queue>
#include <memory>


#include "Subscriber.h"


namespace Subscriber {




// static class which manages all the subscribers and calls their update and JSON conversion
// methods, also handling the sending of JSON records to Endpoints (with its separate manager_thread)
// the intention is that clients never actually touch subscriber objects directly (AbstractSubscriber
// and its descendants).
class Subscribers {
    // subscribers are stored here
    static inline std::unordered_map<id_t, std::unique_ptr<AbstractSubscriber>, id_t::Key> 
    idmap;
    // mutex for idmap
    static inline std::recursive_mutex 
    it_mtx;

    public:

    //  milliseconds between scans over the subscriber map (idmap member in this class), to check 
    //      for any pending records (and convert them to JSON)
    //      the program can change the poll interval during runtime by modifying this variable 
    static inline std::atomic<int> manager_thread_poll_interval;

    static inline std::atomic<bool> shutdown_signal;

    // call update on all the subscribers
    static uintmax_t update(std::shared_ptr<Market::Market>, const timepoint_t&);

    // add one or more subscribers
    // variant contains either the ID associated with the successfully added subscribers, 
    // or a string error message
    static std::variant<id_t, std::string>
    add(std::shared_ptr<AbstractFactory>, Config);
    static std::deque< std::variant<Subscriber::id_t, std::string> >
    add(std::deque<std::pair<std::shared_ptr<AbstractFactory>, Config>> c);

    // result of a delete operation
    enum class delete_status_t { DELETED, MARKED, DOES_NOT_EXIST };

    // delete one or more subscribers
    // if sync is true, then call wait_flag(subscriber_flags_t::Flushed) on the subcriber
    // first, to wait for all of its remaining records to be "released" (emitted to Endpoints by
    // our manager thread)
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
    static bool wait(const id_t& id, const std::optional<timepoint_t>& tp) {
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



};


#endif
