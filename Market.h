#ifndef MARKET_H
#define MARKET_H 1

#include "Agent/Agent.h"
#include "types.h"

#include <stack>
#include <queue>
#include <optional>
#include <variant>
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


typedef numeric_id<market_numeric_id_tag> agentid_t;



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

    std::unique_ptr<Agent::Agent> agent;
    const agentid_t id;
    const timepoint_t created;

    // Intended to be accessed directly by the Market class
    std::unique_ptr<ts<Agent::AgentAction>> history;


    enum class Flags {
        // Ignore_info: don't take into account the Agent's info cursor when deciding whether to 
        // delete old info history (by default (ie when this flag is not set), it's preserved 
        // if an Agent hasn't read it)
        Ignore_info,    
    };

    std::set<Flags> flags;
};
// used when reporting information about agents (Market::list_agents)
struct agentrecord_desc_t {
    const agentid_t id;
    const timepoint_t created;
    uintmax_t history_count;
    std::set<AgentRecord::Flags> flags;
};


// part of a mechanism to send asynchronous commands to a Market instance (without a 
// separate thread), see `op` classes below
struct op_abstract;
enum class op_t {
    ADD_AGENT,
    DEL_AGENT,
    RUN,
    PAUSE,
    START,
    SHUTDOWN,
};
template<enum op_t> struct op {};


class Market;


enum class state_t {
    RUNNING,
    PAUSED
};



struct Config {
    std::optional<uintmax_t> iter_block;

// TODO - not yet implemented; may go with a different approach for these tasks (eg let client do it)
/*
    std::optional<uintmax_t> agent_history_prune_age;
    std::optional<uintmax_t> price_history_prune_age;
    std::optional<uintmax_t> info_history_prune_age;
    */
};



class Market : public std::enable_shared_from_this<Market> {
    private:
        std::map<agentid_t, AgentRecord> agents;

        // see op_t, and the op classes below
        std::deque<std::shared_ptr<op_abstract>> op_queue;
        std::mutex op_queue_mtx;
        std::condition_variable op_queue_cv;
        std::map<enum op_t, std::size_t>
        op_execute_helper(std::optional<std::set<enum op_t>> = std::nullopt);

        // protects certain parts of internal state
        std::recursive_mutex api_mtx;

        // "current time" - the point in time where the next iteration
        // will take place (i.e. 1 step in time after the most recent iteration)
        // initialized to 0 - when we have not iterated at all; the zeroth
        // iteration is the first iteration
        timepoint_t timept;

        // the most recent price (price generated from most recent iteration)
        // so current_price is the price associated with this->timept-1
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
        // how many more iterations remain until the Market stops automatically; nullopt means 
        // run indefinitely (until manually stopped)
        std::optional<std::atomic<unsigned int>> remaining_iter;

        // invoke an Agent's computation 
        // can be overridden to provide an alternate "evaluation model"
        virtual std::tuple<std::optional<Agent::AgentAction>, price_t, std::optional<Agent::info_view_t>> 
            do_evaluate(AgentRecord&, price_t, price_t, std::optional<Agent::info_view_t>);

        // iter_block is the number of time steps which take place "contiguously" (in an "iteration
        // block"), with no other activity.
        // between iteration blocks, the Market does things like updating Subscribers, 
        // allowing API calls (for client programs such as the Interface singleton), and processing 
        // `op` objects.
        std::atomic<unsigned int> iter_block;

        // see below, above the `launch` method
        std::atomic<bool> launched;
        std::atomic<bool> started;
        std::atomic<bool> configured;

        // set by shutdown() (used by OP_SHUTDOWN) - needed in order to escape from main_loop
        std::atomic<bool> shutdown_signal;

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
            state(state_t::PAUSED),
            current_price(INITIAL_PRICE)
        {
            this->price_history = std::unique_ptr<ts<price_t>> {
                new ts<price_t>(this->timept)
            };

            this->info_history = std::unique_ptr<ts<Info::infoset_t>> {
                new ts<Info::infoset_t>(this->timept, mark_mode_t::MARK_PRESENT)
            };

            this->initialize_perf_map();
        }
        virtual ~Market();

        // managing startup
        // the client program needs to call the `launch` method first, to start the Market's 
        // thread. 
        // afterwards, to actually begin the simulation, `start` must be called.
        // both methods can be called only once
        // 
        // overall, the intention is to make it possible to start the Market's thread first and have
        // it wait for the initialization of relevant state by the client before entering the main loop
        std::thread launch(bool auto_start = false) {

            if (this->launched == true) 
                throw std::logic_error("Market::launch should only be called once");

            this->launched = true;
            std::thread t ([this]() {
                while (true) {
                    std::unique_lock L_op { this->op_queue_mtx };
                    this->op_queue_cv.wait(L_op, [this](){ return this->op_queue.size() > 0; });
                    auto processed = this->op_execute_helper(
                        {{ op_t::START, op_t::SHUTDOWN }}
                    );
                    if (processed.size() > 0) {
                        if (processed.contains(op_t::SHUTDOWN)) {
                            return;
                        }
                        // because of our filter, above, the op must have been START
                        break;
                    }
                }
                try {
                    this->main_loop();
                } catch (std::system_error& e) {
                    LOG(ERROR) << "Market::launch caught system_error: " 
                        << e.code() << ": " << e.what();
                } catch (std::exception& e) {
                    LOG(ERROR) << "Market::launch caught exception: " << e.what();
                }
                VLOG(5) << "Market thread exiting";
            });

            if (auto_start == true)
                this->start();
            
            return std::move(t);
        }

        timepoint_t now() {
            return this->timept;
        } 

        ts<Agent::AgentAction>::view 
        agent_action_iterator(const timepoint_t&, agentid_t);

        ts<price_t>::view 
        price_iterator(const timepoint_t&);

        std::optional<Agent::info_view_t>
        info_iterator(const std::optional<timepoint_t>&);

        void initialize_perf_map() {
            auto perf_keys = {
                "info_map", "iter_group", "subscriber_update"
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

        void queue_op(std::shared_ptr<op_abstract> op);
        void configure(Config);
        void start();

        void shutdown () {
            this->shutdown_signal.store(true);
        }

        //  API 
        //  Some of these methods lock on Market::api_mtx

        
        // Run for the specified number of iterations, or run indefinitely (until stop 
        // is called).
        // While running, the Market allows incoming API commands, including run and stop,
        // every iter_block iterations.
        // If run is called again, `count` more iterations are performed, instead of however
        // many had been remaining at that time.

        // Initially, iteration does not begin until Market::start is called; it only needs 
        // to be called once.
        void run(std::optional<int> count = std::nullopt);

        
        // when the current iteration block completes, `pause` will remove pending iterations 
        // and stop iterating. A 'RUN' `op` restarts iteration. If a 'RUN' op is already present 
        // in the queue, it will most likely be processed immediately after this stop invocation, 
        // even though the RUN op was queued before the stop invocation.
        // 
        // As a result, generally it is recommended to run/pause the Market instance asynchronously 
        // using `op` objects to ensure that requests are sequenced as intended
        void pause();

        // destroy all `ts` structures, set time to 0, set price to INITIAL_PRICE,
        // and destroy all Subscribers
        void reset();

        // wait for the Market to enter the state_t::PAUSED state
        // this will happen either
        // - after the specified number of iterations (via Market::run) has completed
        // - when a 'PAUSE' op_t is queued and processed between iteration blocks
        //
        // this can be useful to an HTTP client, for example, to ensure that the market 
        // is paused and/or a certain number of iterations have occurred
        //
        // timepoint_t `tp` argument is the latest point in time (inclusive) that we will 
        // try to wait; std::nullopt is returned if we 'timed out' waiting beyond this
        // threshold
        //
        // currently, we also return if the Market shuts down while we're waiting, even
        //  if it hasn't entered the PAUSED state
        //
        // if require_time is true, wait_for_pause will additionally wait until the
        // given timepoint is reached (or raise an exception if no timepoint was given),
        // rather than returning immediately when the PAUSED state is reached
        // TODO for now, we are not raising an exception - just returning nullopt
        std::optional<timepoint_t> 
        wait_for_pause(const std::optional<timepoint_t>& tp, bool require_time = false) {
            if (require_time == true && ! tp.has_value()) {
                return std::nullopt;
            }
            using namespace std::chrono_literals;
            std::unique_lock L { this->api_mtx, std::defer_lock };
            while ( (tp.has_value() ? this->timept <= tp : true) ) {
                if (this->shutdown_signal == true) {
                    return std::nullopt;
                }

                L.lock();
                if (this->state == state_t::PAUSED) {
                    if (!require_time || (require_time == true && this->timept >= tp.value())) {
                        return this->timept;
                    }
                }
                L.unlock();

                std::this_thread::sleep_for(100ms);
            }

            return std::nullopt;
        }

        // register and store an Agent instance (inside an AgentRecord object) and return the
        // generated ID
        agentid_t 
        add_agent(std::unique_ptr<Agent::Agent>);

        /* 
            std::nullopt argument => delete all 

            return:
                map each agentid_t to whether it was successfully deleted

            Note: blocks on the processing of AgentAction Subscriber instances associated with this
                Agent 
        */
        std::map<agentid_t, bool>
        del_agents(std::optional<std::deque<agentid_t>> = std::nullopt);

        std::deque<agentrecord_desc_t> 
        list_agents();

        /*
            Retrieves an agent's history of Agent::AgentAction structs: the buy/sell action it has
                taken at each timepoint (see Agent.h for Agent::AgentAction)

            If erase is true, the unique_ptr holding the history is std::move'd to the return value.
            If erase is false, the current contents of the history are copied.

            std::nullopt is returned if the given agent ID doesn't exist
        */
        std::optional<std::unique_ptr<ts<Agent::AgentAction> >>
        get_agent_history(const agentid_t&, bool erase);


        /*
            If erase is true, the price history unique_ptr is std::move'd to the return value and
                the price history is reinitialized.
            If erase is false, the current contents of the history are copied.
        */
        std::unique_ptr<ts<price_t>>
        get_price_history(bool erase);


        /*
            Inserts one or more Info structs into info_history at the current timepoint

            Returns either the current timepoint_t (where the infoset was inserted) or,
            in the case of failure, a string explaining the failure

            Since the Market API is only available between iteration blocks, Info objects
            can only be emitted with a certain granularity in time, i.e. only between
            iteration blocks.
        */
        std::variant<timepoint_t, std::string> 
        emit_info(Info::infoset_t& x);

        // used to test/simulate what price would result from an iteration on one Agent
        // 
        std::pair<price_t, Agent::AgentAction>
        test_evaluate(std::shared_ptr<Agent::Agent>, price_t, price_t, std::optional<Info::infoset_t>);

};



// The "op" classes provide an asynchronous way to use the Market API.
// The "op" describes and encapsulates the operation to be performed on the 
// Market object; the Market object is unaware of the "op" internals, and just
// calls execute on the op_abstract pointer.
//
// Queuing the op/inserting the op onto the op_queue is synchronous, but the actual execution of the 
// op's operation is asynchronous (from the standpoint of the client)
// 
// Since the Crow webserver is multithreaded, blocking on the Market API is OK, 
// so this "op" interface will probably mostly not be needed.
// The one instance where it must be used is when the Market is not in the RUN state;
// in that case, the Market is waiting on the op queue for an op that starts it.
// It's also best to use the PAUSE op instead of calling Market::stop directly.
//
// op objects can return values asynchronously to the client; the client maintains a
// shared_ptr of the op, and a condition variable is used to notify when the return value
// is available. 
//
// Aside from that, it could be used in the future as a way to independently organize
// together functionality that needs to operate on a Market object, and which
// is more complex than just a single call to one of the Market methods.
//
// 2024-04-04: SHUTDOWN is now another op that is required, like RUN. It signals to the Market
// thread that it needs to return from main_loop().


// Market is only aware of the abstract interface
struct op_abstract {
    virtual void execute(Market &) = 0;
    op_t t;
};

// return values
template<enum op_t>
struct op_ret {};


template<enum op_t T>
class op_base : public op_abstract {
    private:
    std::condition_variable ret_cv;
    std::mutex ret_mtx;
    std::optional<op_ret<T>> ret;
    virtual op_ret<T> do_execute(Market &) = 0;

    public:
    op_base() {
        this->t = T;
    }

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



template<>
struct op_ret<op_t::START> {};
template<>
class op<op_t::START> : public op_base<op_t::START> {
    private:
    op_ret<op_t::START> do_execute (Market &m) {
        return {};
    }
};


// This is one op class that is strictly necessary - it needs to be used to 
// set a Market into the RUN state once it's in the PAUSED state.
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

// This op class needs to be used to indicate to the Market thread that it needs to return
// from main_loop() to allow the program to stop.
template<>
struct op_ret<op_t::SHUTDOWN> {};
template<>
class op<op_t::SHUTDOWN> : public op_base<op_t::SHUTDOWN> {
    private:
    op_ret<op_t::SHUTDOWN> do_execute (Market &m) {
        m.shutdown();
        return {};
    }
};


// this op class is also recommeneded - it ensures that competing calls to RUN
// and PAUSE are processed in the order that they are invoked (since they are queued)
template<>
struct op_ret<op_t::PAUSE> {};
template<>
class op<op_t::PAUSE> : public op_base<op_t::PAUSE> {
    private:
    op_ret<op_t::PAUSE> do_execute(Market &m) {
        m.pause();
        return {};
    }
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
    op(std::unique_ptr<Agent::Agent> agent) : agent(std::move(agent)), op_base() {}
    std::unique_ptr<Agent::Agent> agent;
};


}



#endif