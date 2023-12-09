#ifndef MARKET_H
#define MARKET_H 1

#include "Agent.h"
#include "types.h"

#include <stack>
#include <queue>
#include <optional>
#include <chrono>

#include "Info.h"
#include "ts.h"


#include <condition_variable>
#include <semaphore>
#include <thread>
#include <compare>
#include <set>

#include <glog/logging.h>

namespace Market {


const price_t INITIAL_PRICE = 1;

namespace {


    typedef std::map<std::string, ts<std::chrono::milliseconds>> perf_map_t;
};


struct market_numeric_id_tag {};
typedef numeric_id<market_numeric_id_tag> agentid_t;

// ID for Agent instances
/*
class agentid_t {
    protected:
        unsigned int id;
        inline static unsigned int last_id = 0;
    public:
    agentid_t() {
        this->id = last_id++;
    }

    unsigned int to_numeric() const {
        return this->id;
    }

    agentid_t(const agentid_t &_id) : id(_id.id) {}

    agentid_t(unsigned int id) : id(id) {}

    std::string str() const {
        return std::to_string(id);
    }

    auto operator<=> (const agentid_t& x) const = default;
};
*/




// metadata and history data that the Market maintains for each Agent instance
class AgentRecord {

    public:

    // check whether this Agent is supposed to be run at a specific timepoint_t,
    // based on its schedule configuration
    bool is_scheduled(const timepoint_t& t) {

        // for now we specify this using schedule_every in the config
        // later on there can be more expressive ways of specifying when the agents run,
        // especially relative to one another
        return
            (t - this->created) % this->agent->config.schedule_every == 0;
    }

    std::unique_ptr<Agent> agent;
    const agentid_t id;
    const timepoint_t created;

    // Intended to be accessed directly by the Market class, so no encapsulation
    std::unique_ptr<ts<AgentAction>> history;


    enum class Flags {
        // don't take into account the Agent's info cursor when deciding whether to 
        // delete old info history (normally it's preserved if an Agent hasn't read it)
        Ignore_info,    
    };

    std::set<Flags> flags;
};
// used when reporting information about agents
struct agentrecord_desc_t {
    const agentid_t id;
    const timepoint_t created;
    uintmax_t history_count;
    std::set<AgentRecord::Flags> flags;
};


// part of a mechanism to send asynchronous commands to a market instance (without a 
// separate thread), see `op` classes below
enum class op_t {
    ADD_AGENT,
    DEL_AGENT,
    RUN,
    STOP,
};


class Market;
struct op_abstract;


enum class state_t {
    RUNNING,
    STOPPED
};



// TODO
struct Config {
    std::optional<uintmax_t> iter_block;

/*
    std::optional<uintmax_t> agent_history_prune_age;
    std::optional<uintmax_t> price_history_prune_age;
    std::optional<uintmax_t> info_history_prune_age;
    */
};


/*
Config default_config = { 
    100,
    50000,
    50000
};
*/


class Market : public std::enable_shared_from_this<Market> {
    private:
        //Config config;
        std::map<agentid_t, AgentRecord> agents;

        // see op_t, and the op classes below
        std::queue<std::shared_ptr<op_abstract>> op_queue;
        std::mutex op_queue_mtx;
        std::condition_variable op_queue_cv;
        void op_execute_helper();

        /*  protects our internal data from simultaneous access 
            by multiple client threads
        */
        std::recursive_mutex api_mtx;

        // "current time" - the point in time where the next iteration
        // will take place (i.e. 1 step in time after the most recent iteration)
        // initialized to 0 - when we have not iterated at all
        timepoint_t timept;

        // the most recent price (price generated from most recent iteration)
        price_t current_price;
        std::unique_ptr<ts<price_t>> price_history;

        // history of Info objects that have been sent to the market with 
        // emit_info, to be made available to the Agents
        std::unique_ptr<ts<Info::infoset_t>> info_history;
        // earliest unread info among all the Agents
        // remains at std::nullopt until info is actually emitted
        std::optional<timepoint_t> global_agent_info_cursor;


        // testing performance of certain Market compnoents
        perf_map_t perf_map;

        enum state_t state;
        std::optional<std::atomic<unsigned int>> remaining_iter;

        // can be overridden to provide an alternate "evaluation model"
        virtual std::tuple<std::optional<AgentAction>, price_t, std::optional<info_view_t>> 
            do_evaluate(AgentRecord&, price_t, price_t, std::optional<info_view_t>);

        // iter_block is the number of time steps which take place "contiguously" (in an "iteration
        // block"), with no other activity
        // between iteration blocks, the Market does things like updating Subscribers, etc,
        // and it releases the API mutex which allows for client programs (such as Interface)
        // to make modifications via the public Market methods
        std::atomic<unsigned int> iter_block;

        // managing startup
        // the client program needs to call the `launch` method first, to start the Market's 
        // thread. afterwards, to actually begin the simulation, `start` must be called.
        // both methods can be called only once 
        std::atomic<bool> launched;
        std::atomic<bool> started;
        std::binary_semaphore start_sem;

        // whether this instance's Market::configure has been called at least once
        std::atomic<bool> configured;

        void main_loop();

        void perf_measurement(std::string key, 
            std::chrono::time_point<std::chrono::steady_clock> s,
            std::chrono::time_point<std::chrono::steady_clock> f) 
        {
            auto x = std::chrono::duration_cast<std::chrono::milliseconds>(f-s);
            this->perf_map[key].append(x);
        }

    public:

        Market() :
            state(state_t::STOPPED),
            current_price(INITIAL_PRICE)
        {
            this->price_history = std::unique_ptr<ts<price_t>> {
                new ts<price_t>(this->timept)
            };

            this->info_history = std::unique_ptr<ts<Info::infoset_t>> {
                new ts<Info::infoset_t>(this->timept)
            };

            this->initialize_perf_map();
        }
        ~Market() {}

        static inline std::atomic<bool> shutdown_signal;

        std::thread launch(bool auto_start = false) {

            if (this->launched == true) 
                throw std::logic_error("Market::launch should only be called once");

            this->launched = true;

            std::thread t ([this]() {

                if (!this->started == true) {
                    this->start_sem.acquire();
                }

                this->main_loop();
            });

            if (auto_start == true)
                this->start();
            
            return std::move(t);
        }

        ts<AgentAction>::view 
        agent_action_iterator(const timepoint_t&, agentid_t);

        ts<price_t>::view 
        price_iterator(const timepoint_t&);

        std::optional<info_view_t>
        info_iterator(const std::optional<timepoint_t>&);


        void initialize_perf_map() {
            auto perf_keys = {
                "info_map", "iter_group"
            };
            for (auto k : perf_keys) {
                this->perf_map.insert({ k, ts<std::chrono::milliseconds>(this->timept) });
            }
        }

        perf_map_t
        get_perf_map() {
            return this->perf_map;
        }

        void clear_perf_map() {
            this->perf_map.clear();
            this->initialize_perf_map();
        }

        /*  API 
            All of these methods lock on Market::api_mtx
        */

        void queue_op(std::shared_ptr<op_abstract> op);

        void configure(Config);

        void start() { 
            if (this->started == true)
                throw std::logic_error("Market::start should only be called once");

            if (this->configured == false) {
                this->configure({
                    100     // iter_block default
                });
            }

            this->started.store(true);

            this->start_sem.release();

            // check for any "ops" which may have been queued before start
            this->op_execute_helper();

        }

        
        // Run for the specified number of iterations, or run indefinitely (until stop 
        // is called).
        // While running, the Market allows incoming API commands, including run and stop,
        // every iter_block iterations.
        // If run is called again, `count` more iterations are performed, instead of however
        // many had been remaining at that time.

        // Iteration does not begin until Market::start is called; it only needs to be called
        //  once
        void run(std::optional<int> count = std::nullopt);

        
        // when the current iteration block completes, this will remove pending iterations 
        // and stop iterating (unless a RUN 'op' is queued)
        void stop();

        // destroy all `ts` structures, set time to 0, set price to INITIAL_PRICE,
        // and destroy all Subscribers
        void reset();



        // wait for the Market to enter the state_t::STOPPED state
        // this will happen when either
        // - after the specified number of iterations (via Market::run) has completed
        // - when a 'STOP' op_t is queued and processed between iteration blocks
        //
        // this can be useful to an HTTP client, for example, to ensure that the market 
        // is stopped and/or a certain number of iterations have occurred
        //
        // timepoint_t argument is the latest point in time (inclusive) that we will 
        // try to wait; std::nullopt is returned if we 'timed out' waiting beyond this
        // threshold
        std::optional<timepoint_t> 
        wait_for_stop(const std::optional<timepoint_t>& tp) {
            using namespace std::chrono_literals;
            std::unique_lock L { this->api_mtx, std::defer_lock };
            while ( (tp.has_value() ? this->timept <= tp : true) ) {
                L.lock();
                if (this->state == state_t::STOPPED) {
                    return this->timept;
                }
                L.unlock();

                std::this_thread::sleep_for(100ms);
            }

            return std::nullopt;
        }


        /* 
        */
        agentid_t add_agent(std::unique_ptr<Agent>);

        /* 
            std::nullopt argument => delete all 

            return:
                bool is false if a provided agentid_t could not be deleted (eg was not found)
                    true otherwise
                array contains the IDs of all those agents which were successfully deleted
                TODO change 
                
        */
        std::map<agentid_t, bool>
        del_agents(std::optional<std::deque<agentid_t>> = std::nullopt);

        std::deque<agentrecord_desc_t> 
        list_agents();



        /*
            Retrieves an agent's history of AgentAction structs: the buy/sell action they have 
                taken at each timepoint (see Agent.h for AgentAction)

            If erase is true, the unique_ptr holding the history is std::move'd to the return value.
            If erase is false, the current contents of the history are copied.

            std::nullopt is returned if the agent is not present in the map
        */
        std::optional<std::unique_ptr<ts<AgentAction> >>
        get_agent_history(const agentid_t&, bool erase);


        /*
            If erase is true, the price history unique_ptr is std::move'd to the return value.
            If erase is false, the current contents of the history are copied.
        */
        std::unique_ptr<ts<price_t>>
        get_price_history(bool erase);


        /*
            Inserts one or more Info structs into info_history at the current timepoint

            Returns either the current timepoint_t (where the infoset was inserted) or,
            in the case of failure, a string explaining the failure
        */
        std::variant<timepoint_t, std::string> 
        emit_info(Info::infoset_t& x);

        // used to test/simulate what price would result from an iteration on one Agent
        // 
        std::pair<price_t, AgentAction>
        test_evaluate(std::shared_ptr<Agent>, price_t, price_t, std::optional<Info::infoset_t>);

};



// The "op" classes provide an asynchronous way to use the Market API.
// The "op" describes and encapsulates the operation to be performed on the 
// Market object; the Market object is unaware of the "op" internals, and just
// calls execute on the op_abstract pointer.
// It technically does block on insertion of the op into the op_queue, but the idea is
// that this is less time consuming than the actual op itself, so it's asynchronous
// in that sense.
//
// Since the Crow webserver is multithreaded, blocking on the Market API is OK, 
// so this "op" interface will probably mostly not be needed.
// The one instance where it must be used is when the Market is not in the RUN state;
// in that case, the Market is waiting on the op queue for an op that starts it.
//
// Aside from that, it could be used in the future as a way to independently organize
// together functionality that needs to operate on a Market object, and which
// is more complex than just a single call to one of the Market methods.

/* 
    From the standpoint of the Market class
*/
struct op_abstract {
    virtual void execute(Market &) = 0;
};

template<enum op_t>
struct op_ret {};


/*
    From the standpoint of the client of the Market class: op_base and
    its derived classes
*/
template<enum op_t T>
class op_base : public op_abstract {
    private:
    std::condition_variable ret_cv;
    std::mutex ret_mtx;
    std::optional<op_ret<T>> ret;
    virtual op_ret<T> do_execute(Market &) = 0;

    public:
    op_base() = default;
    virtual op_ret<T> wait_ret() final {
        std::unique_lock<std::mutex> L(this->ret_mtx);
        if (this->ret.has_value()) {
            return this->ret.value();
        } else {
            this->ret_cv.wait(L);
            return this->ret.value();
            // TODO std::bad_optional_access
        }
        // lock is destroyed/unlocked
    }

    virtual void execute(Market &m) final {
        op_ret<T> ret = this->do_execute(m);

        std::unique_lock<std::mutex> L(this->ret_mtx);
        this->ret = ret;
        this->ret_cv.notify_all();
        // lock is destroyed/unlocked
    }
};

template<enum op_t> struct op {};



// This is the only op class that is strictly necessary - it needs to be used to 
// set a Market into the RUN state once it's in the STOPPED state.

template<>
struct op_ret<op_t::RUN> {};
template<>
class op<op_t::RUN> : public op_base<op_t::RUN> {
    private:
    op_ret<op_t::RUN> do_execute (Market &m) {
        m.run(this->count);
        return {};
    }
    public:
    op(std::optional<int> count = std::nullopt) : count(count) {}
    std::optional<int> count;
};



template<>
struct op_ret<op_t::ADD_AGENT> {
    agentid_t value;
};
template<>
class op<op_t::ADD_AGENT> : public op_base<op_t::ADD_AGENT> {
    private:
        op_ret<op_t::ADD_AGENT> do_execute (Market &m) {
            return {
                m.add_agent(std::move(this->agent))
            };
        }
    public:
    op(std::unique_ptr<Agent> agent) : agent(std::move(agent)), op_base() {}
    std::unique_ptr<Agent> agent;
};


template<>
struct op_ret<op_t::STOP> {};
template<>
class op<op_t::STOP> : public op_base<op_t::STOP> {
    private:
    op_ret<op_t::STOP> do_execute(Market &m) {
        m.stop();
        return {};
    }
};

}



#endif