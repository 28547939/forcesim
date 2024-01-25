

#include "Market.h"
#include "Subscriber.h"

#include <glog/logging.h>
#include <iostream>
#include <memory>

/*
                {

                    // remains without a value until the first Info item exists, ie, when
                    // Market::emit_info is called for the first time
                    if (this->agent_info_cursor.has_value()) {
                        auto info_cursor = this->agent_info_cursor.value();

                        // calculate Market::agent_info_cursor, which is the greatest lower bound across
                        // the agents' latest read entry in Market::info_history
                        for (auto& agent_record : this->agents) {

                            if (agent_record.info_cursor.has_value()) {

                            } 
                            
                            else {
                                if () 
                            }



                            if (agent_record.info_cursor < ) {
                                info_cursor = agent_record.info_cursor;
                            }
                        }

                        VLOG(8) << "global_agent_info_cursor updated to " << info_cursor;
                        this->agent_info_cursor = info_cursor;

                    }

                }





        if (this->_info_cursor.has_value()) {
            // truncate earlier entries from the info timeseries that we have already read
            try {
                this->info_view->seek_to(this->_info_cursor)
            } catch (std::out_of_range& e) {
                throw std::out_of_range("agent cursor is behind earliest info_history entry");
            }

        // initialize our cursor the first time 
        } else {
            this->_info_cursor = info_view->cursor();
        }

        */




namespace Market {

/*
std::ostream& operator<<(std::ostream& os, const timepoint_t& tp) {
    return os << std::to_string(tp.to_numeric());
}
*/


inline std::tuple<std::optional<AgentAction>, price_t, std::optional<info_view_t>> 
Market::do_evaluate(
    AgentRecord& agent, 
    price_t p_existing, 
    price_t p_current,
    std::optional<info_view_t> info_view
) {
    // TODO info_view unique_ptr or optional
    auto [act, info_view_ret] = agent.agent->evaluate(p_existing, std::move(info_view));

    // if the agent experiences a failure (produces an exception)
    if (! act.has_value()) {
        return { std::nullopt, p_current, std::move(info_view_ret) };
    }

    if (act->internal_force > 100)
        act->internal_force = 100;

    double force = (act->internal_force / 100) * agent.agent->config.external_force;

    double factor = act->direction == Direction::UP ? 1 + force : 1 - force;

    price_t p_new = p_current * factor;
    VLOG(10) << p_current << "\t*\t" << factor << "\t=\t" << p_new;

    return { act, p_new, std::move(info_view_ret) };
}

//Market::subscribers


/*
    The main loop is single-threaded
    Inside the inner loop, the api_mtx mutex is locked to prevent other threads (such as from Interface)
    from making any modifications to the structures that are accessed here, such as the agents or
    their history. (As a result, the iter_block property of the Market will change the "granularity"
    with which clients can make modifications using the API, such as emitting Info objects)

    After an iter_block completes:
    
    - Subscribers::update is called to get the subscribers to pull their data
        produced by the recent iterations; Subscribers::update locks its own internal mutex to protect
        access to its map holding the subscriber objects. That map is also accessed by the Market API 
        (via the Subscribers class), when adding/deleting subscribers, for example.

    - 

*/
void Market::main_loop() {
    std::unique_lock L_op { this->op_queue_mtx, std::defer_lock };

    while (true) {

        // Lock before the if statement to protect the statements in the condition
        // locks API access - applies to any client program accessing most methods in the Market class 
        //  for example the HTTP interface, via the Interface class
        std::unique_lock L_api { this->api_mtx };

        if (this->state == state_t::RUNNING && this->agents.size() > 0) {
            // number of "sub-" iterations remaining to be considered in this loop iteration 
            unsigned int r = std::min(
                this->iter_block,
                this->remaining_iter.has_value() 
                    ? this->remaining_iter.value() 
                    : this->iter_block
            );

            if (r > 0) {

                VLOG(8) << "about to execute block of " << r << " iterations; " 
                    << (this->remaining_iter.has_value() ? 
                        std::to_string(this->remaining_iter.value()) 
                        : "[unlimited]")
                    << " total remaining";


                const auto p1s = std::chrono::steady_clock::now();


// TODO 
/*
info_view can be nullopt, but info_history should never be uninitialized

*/

                // info_view: ts::view object to convey Info objects to Agents 
                // Possible values
                // if Market::info_history is empty (i.e. no Info objects ever emitted, or the history
                //      has been deleted), this is a std::nullopt
                // if global_agent_info_cursor is nullopt, info_view will begin at the beginning of
                //      info_history
                auto info_view = this->info_iterator(this->global_agent_info_cursor);

                const auto p1f = std::chrono::steady_clock::now();
                const auto p2s = std::chrono::steady_clock::now();
                
                // iteration block - exactly r iterations, which is no more than the iter block
                // value configured in the Market instance
                for (int i = r; i > 0; i--) {
                    auto existing_price = this->current_price;
                    auto current_price = existing_price;
                    
                    for (auto& [agent_id, agent_record] : this->agents) {
                        if (! agent_record.is_scheduled(this->timept)) {
                            continue;
                        }

                        try {
                            auto info_cursor = agent_record.agent->info_cursor();

                            // if applicable, adjust the position in the Info history to wherever this 
                            // particular agent left it
                            if (info_view.has_value() && info_cursor.has_value()) {
                                auto info_cursor_v = info_cursor.value();

                                auto& [a,b] = (*info_view)->bounds();

                                // check that the agent's cursor actually lies within the range 
                                // of values contained in the info_view
                                if (info_cursor_v >= a && info_cursor_v <= b) {
                                    try {
                                        (*info_view)->seek_to(info_cursor_v);
                                    } 
                                    // should not happen - we have checked the bounds
                                    catch (std::out_of_range& e) {
                                        LOG(ERROR) << "info_view->seek_to failed "
                                            << "(agentid=" << agent_record.id.str() << ", "
                                            << "begin=" << a.to_numeric() << ", "
                                            << "end=" << b.to_numeric() << "): " << e.what();
                                    }
                                }
                            }

                            // Agent computation
                            auto [agent_action, current_price_new, info_view_ret] = this->do_evaluate(
                                agent_record, existing_price, current_price, std::move(info_view)
                            );
                            // update price
                            current_price = current_price_new;
                            info_view = std::move(info_view_ret);

                            // nullopt info_view means it was never initialized before our iteration 
                            // so info_history must be empty
                            if (info_view.has_value()) {
                                (*info_view)->reset_cursor();
                            }
                            
                            // nullopt AgentAction means there was an exception during the Agent computation
                            // (see Agent class)
                            if (agent_action.has_value()) {
                                agent_record.history->append(agent_action.value());
                            } else {
                                LOG(ERROR) << "agent_action not set, skipping history entry";
                                agent_record.history->skip(1);
                            }

                        } catch(std::system_error& e) {
                            LOG(ERROR) << "system_error: " << e.code() << " " << e.what();
                            std::cout << "system_error: " << e.code() << " " << e.what();
                            break;
                        }
                    }

                    this->current_price = current_price;
                    this->price_history->append(current_price);

                    // increment time regardless of any exceptions
                    // this->timept now refers to the next, upcoming point in time / iteration
                    this->timept += 1;
                }

                const auto p2f = std::chrono::steady_clock::now();


                // ****************
                // the global info cursor is the (inclusive) least upper bound on already-read 
                // info_history entries, taken among all agents

                // is_empty is only true during our first iteration, when there are no entries 
                // (not even any empty entries)
                if (!this->info_history->is_empty()) {

                    // info_view will be nullopt if info_history ONLY has empty entries
                    // (the ts::sparse_view can't be created if the underlying ts (info_history)
                    //  has only empty entries)
                    if (info_view.has_value()) {

                        // TODO make sure you handle the situation where info_history is cleared
                        // agents will have non-empty cursors

                        
                        std::optional<timepoint_t> new_cursor;

                        for (auto& [id, agent_record] : this->agents) {

                            // if Ignore_info is set, the agent's cursor may be reset further than it has 
                            // read, and the cursor is not used when calculating global_agent_info_cursor.
                            if (agent_record.flags.contains(AgentRecord::Flags::Ignore_info)) {
                                continue;
                            } 
                            else if (! agent_record.agent->info_cursor().has_value()) {
                                // (see Agent::read_next_info)
                                // empty info_cursor means the agent has not read any info_history entries.
                                // unless the agent has the Ignore_info flag, it will keep the global cursor
                                // from moving forward

                                break;
                            }

                            // comparison begins against the first agent that we check
                            if (! new_cursor.has_value()) {
                                new_cursor = agent_record.agent->info_cursor().value();
                                continue;
                            }

                            // find the minimum among
                            if (agent_record.agent->info_cursor() < new_cursor) {
                                new_cursor = agent_record.agent->info_cursor();
                            }
                        }

                        // if none of the agents had a non-empty info_cursor, keep the 
                        // TODO should not set it to the info_view bounds-  the info_view lower bound is
                        // incremented before the iter block, to reflect an unread entry (check)

                        auto new_cursor_v = new_cursor.value_or((*info_view)->bounds().first);
                        VLOG(8) << "global_agent_info_cursor updated to " << new_cursor_v;
                        this->global_agent_info_cursor = new_cursor;
                    } else {

                        // if the info_view is nullopt, we know that there are no non-empty entries
                        //  so far in info_history
                        // therefore, we can safely fast-forward the global agent cursor to
                        //  skip the iterations that we've just done

                        // in addition, if info_history is ever deleted, info_view will be nullopt
                        // until new info is emitted.
                        // if any agent cursors still point to deleted info entries (though they 
                        //  shouldn't, since there should be a wait mechanism to prevent this),
                        //  those cursors need to be reset anyway.

                        this->global_agent_info_cursor = std::nullopt;
                    }
                }

                // ****************

                // during the preceding iteration block, there were no additions to info_history
                this->info_history->skip(r);


                //const timepoint_t info_cursor = std::transform_reduce(this->agents.begin())

                if (this->remaining_iter.has_value()) {
                    VLOG(11) << (this->remaining_iter.value()) << " " << r << " " << this->iter_block;
                    this->remaining_iter = std::max(0U, (unsigned int) this->remaining_iter.value() - r);
                    VLOG(11) << (this->remaining_iter.value()) << " " << r << " " << this->iter_block;
                }

                VLOG(8) << "end of iter_block: price is now " << this->current_price;

                this->perf_measurement("info_map", p1s, p1f);
                this->perf_measurement("iter_block", p2s, p2f);
            }  // if r > 0 (i.e. iterations remain)
            else {
                VLOG(8) << "exiting loop without any iterations; no more iterations remain; "
                    << "setting state=STOPPED";
                this->state = state_t::STOPPED;
            }

            // release api_mtx
            L_api.unlock();

            // update Subscribers with any new data from iterations just completed (if any)
            // only run this if there have been iterations - otherwise ts objects will 
            // have no further records to process
            if (r > 0) {
                const auto p3s = std::chrono::steady_clock::now();
                // Locks internal Subscriber mutex for access to the subscriber map
                uintmax_t period = Subscriber::Subscribers::update(this->shared_from_this(), this->timept);
                const auto p3f = std::chrono::steady_clock::now();

                this->perf_measurement("subscriber_update", p3s, p3f);
            }

            if (L_op.try_lock()) {
                VLOG(9) << "op_execute_helper() after iteration";
                this->op_execute_helper();
                L_op.unlock();
            }

        } // if state == RUNNING  AND  agents exist
        
        else {

            if (this->state == state_t::RUNNING) {
                VLOG(5) << "state=RUNNING, but no agents are loaded";
                this->state = state_t::STOPPED;
            }

            // api_mtx only applies to functionality in the true side of the if statement
            L_api.unlock();

            // when not in the RUNNING state, the Market goes "idle", waiting for an `op` to (re-)enter
            // the RUNNING state with additional iterations
            L_op.lock();
            VLOG(8) << "state=STOPPED, waiting on op_queue";
            this->op_queue_cv.wait(L_op);
            while (true) {
                // try to process another op on the queue (if any)
                bool nonempty = this->op_execute_helper();

                L_op.unlock();
                // allow additional ops to be queued while we don't hold the lock

                // only try processing another op if there was one on the queue on the previous attempt
                // if not, any ops that have just been queued (just after we unlocked above) can
                // wait until the next opportunity
                if (nonempty == true) {
                    // attempt to try reading from the op queue again, but if the lock is being held
                    // at this moment, don't bother; any pending ops can wait until later
                    if (! L_op.try_lock()) {
                        break;
                    }
                } else {
                    break;
                }
            }
        }
    }

}


/* API */


/*

// TODO move this to the Factory class(es), likewise for del_subscribers
std::map<Subscriber::id_t, bool>
void Market::del_subscribers(std::vector<Subscriber::id_t> ids) {
    auto ret = std::map<Subscriber::id_t, bool> {};
    std::for_each(c.begin(), c.end(), [this](auto id) {
        auto x = Subscriber::managed_subscribers.erase(id);
        ret[id] = (x == 0 ? false : true);
    });

    return ret;
}
*/

ts<AgentAction>::view 
Market::agent_action_iterator(const timepoint_t& tp, agentid_t id) {
    std::lock_guard L(this->api_mtx);

    auto record = this->agents.find(id);
    if (record == this->agents.end()) {
        throw std::out_of_range("agent not found");
    }

    return ts<AgentAction>::view(*((record->second).history), tp);
}

ts<price_t>::view 
Market::price_iterator(const timepoint_t& tp) {
    std::lock_guard L(this->api_mtx);

    return ts<price_t>::view(*(this->price_history), tp);
}

std::optional<info_view_t>
Market::info_iterator(const std::optional<timepoint_t>& tp) {
    std::lock_guard L(this->api_mtx);

    if (this->info_history->is_empty()) {
        return std::nullopt;
    } else {
        try {
            return std::make_unique<ts<Info::infoset_t>::sparse_view>(*(this->info_history), tp);

        // thrown by sparse_view constructor when there are no non-empty entries
        } catch (std::invalid_argument& e) {
            return std::nullopt;
        } catch (std::out_of_range& e) {
            LOG(ERROR) << "info_iterator sparse_view creation failed: " << e.what();
            return std::nullopt;
        }
    }
}



/******************************************
 * 
 *  API 
 * All of these methods lock this->api_mtx
*/


void 
Market::queue_op(std::shared_ptr<op_abstract> op) {
    const std::lock_guard<std::mutex> lock(this->op_queue_mtx);
    this->op_queue.push(op);
    this->op_queue_cv.notify_one();
}


void 
Market::configure(Config c) {
    if (c.iter_block.has_value()) {
        this->iter_block.store(c.iter_block.value());
    }

    this->configured.store(true);
}


void 
Market::run(std::optional<int> count) {
    std::lock_guard L(this->api_mtx);

    if (count.has_value()) {
        this->remaining_iter = 
            this->remaining_iter.has_value() ? 
                this->remaining_iter.value() + count.value()
            : count.value();
    } else {
        this->remaining_iter = std::nullopt;
    }

    this->state = state_t::RUNNING;
}

void 
Market::stop() {
    std::lock_guard L(this->api_mtx);

    this->state = state_t::STOPPED;
    this->remaining_iter = 0;
}

void 
Market::reset() {
    std::lock_guard L(this->api_mtx);
    using Ss = Subscriber::Subscribers;
    this->stop();

    this->del_agents();


    for (auto list_entry : Ss::list()) {
        auto id = list_entry.id;
        VLOG(5) << "Market::reset: waiting for subscriber " << id.to_string();
        Ss::del(id, true);
    }

    this->timept = 0;
    this->current_price = INITIAL_PRICE;

    this->price_history->clear();
    this->info_history->clear();

    return;
}

agentid_t 
Market::add_agent(std::unique_ptr<Agent> a) {
    std::lock_guard L(this->api_mtx);
    const agentid_t id;
    VLOG(5) << "added agent (id=" << id.str() << ")";

    this->agents.insert({ id, AgentRecord {
        std::move(a),
        id,
        this->timept,
        std::unique_ptr<ts<AgentAction>>(
            new ts<AgentAction>(this->timept)
        ),
        {}
    }});

    return id;
}


// false is indicated when the agent is not found
std::map<agentid_t, bool>
Market::del_agents(std::optional<std::deque<agentid_t>> ids) {
    std::lock_guard L(this->api_mtx);
    auto& agents = this->agents;

    auto deleted = std::map<agentid_t, bool> {};

    if (! ids.has_value()) {
        std::for_each(agents.begin(), agents.end(), [&deleted, this](auto& pair) {

            const agentid_t& id = pair.first;

            VLOG(7) << "waiting for subscribers associated with agent ID " << id.str();
            Subscriber::Factory<AgentAction> f { {id} };
            f.wait(this->timept); // TODO this should be wait_flushed

            deleted[id] = true;
        });
        VLOG(5) << "std::nullopt provided to del_agents; deleting all agents";
        agents.clear();
    } else {
        auto ids_v = ids.value();


        std::for_each(ids_v.begin(), ids_v.end(), 
            [this, &agents, &deleted](const agentid_t& id) {

            auto it = agents.find(id);

            if (it == agents.end()) {
                deleted[id] = false;
            } else {
                // TODO wait on subscriber then delete

                VLOG(7) << "waiting for subscribers associated with agent ID " << id.str();
                Subscriber::Factory<AgentAction> f { {id} };
                f.wait(this->timept);

                VLOG(5) << "deleted agent (id=" << it->first.str() << ")";
                agents.erase(id);
                deleted[id] = true;
            }
        });

    }

    return deleted;
}


std::deque<agentrecord_desc_t>
Market::list_agents() {
    auto r = std::deque<agentrecord_desc_t> {};

    std::transform(this->agents.begin(), this->agents.end(), std::back_inserter(r),
    [](auto& pair) -> agentrecord_desc_t {
        auto& record = pair.second;
        return { 
            pair.first,
            record.created,
            record.history->size(),
            record.flags
        };
    });
    return r;
}



std::optional<std::unique_ptr<ts<AgentAction> >>
Market::get_agent_history(const agentid_t& id, bool erase) {
    auto record = this->agents.find(id);
    if (record == this->agents.end()) {
        LOG(WARNING) << "get_agent_history: could not find agent (id=" 
            << std::to_string(id.to_numeric()).c_str() << ")"
        ;

        return std::nullopt;
    }

    auto& history = (record->second).history;

    if (erase) {
        Subscriber::Factory<AgentAction> f { {id} };
        f.wait(history->first_tp());

        auto ret = std::move(history);
        history.reset(new ts<AgentAction>(this->timept));
        return ret;
    } else {
        auto r = new ts<AgentAction>(*history);
        return std::unique_ptr< ts<AgentAction> > (r);
    }
}

std::unique_ptr<ts<price_t>>
Market::get_price_history(bool erase) {
    if (erase) {
        // TODO construct Subscriber::Factory<price_t> and call wait
        auto ret = std::move(this->price_history);
        this->price_history.reset(new ts<price_t>(this->timept));
        return ret;
    } else {
        auto r = new ts<price_t>(*( this->price_history ));
        return std::unique_ptr< ts<price_t> > (r);
    }
}

std::variant<timepoint_t, std::string>
Market::emit_info(Info::infoset_t& x) {
    std::lock_guard L(this->api_mtx);

    if (! this->info_history) {
        this->info_history = std::unique_ptr<ts<Info::infoset_t>> {
            new ts<Info::infoset_t>(this->timept, mark_mode_t::MARK_PRESENT)
        };
    }

    try {
        auto c = this->info_history->cursor();

        // merge an existing entry, if any (happens if there are mulitple calls to emit_info
        // on the same timepoint). existing entry must be at the end (i.e. current timepoint)
        if (c.has_value() && c == this->timept) {
            auto existing = this->info_history->at(c.value());
            this->info_history->pop();

            if (existing.has_value()) {
                x.merge(existing.value());
            } 

            this->info_history->append(x);
        } else {

            // add a new entry
            this->info_history->append_at(x, this->timept);
        }

        return this->timept;
    } catch (std::out_of_range& e) {
        return e.what();
    } catch (std::invalid_argument& e) {
        return e.what();
    }
}

std::pair<price_t, AgentAction>
Market::test_evaluate(
    std::shared_ptr<Agent> agent, 
    price_t p_existing, 
    price_t p_current,
    std::optional<Info::infoset_t> info)
{
    std::optional<info_view_t> info_view;

    // needs to remain in-scope so that it's not destroyed before the agent instance
    // uses the sparse_view
    ts<Info::infoset_t> info_history(0, mark_mode_t::MARK_PRESENT);

    if (info.has_value()) {
        info_history.append(info.value());

        info_view = std::unique_ptr<ts<Info::infoset_t>::sparse_view>(
            new ts<Info::infoset_t>::sparse_view(info_history)
        );
    }


    // Market::do_evaluate requires that Agents be in a unique_ptr as usual; 
    // we fake that situation here
    std::unique_ptr<Agent> agent_uniq(agent.get());

    AgentRecord record = AgentRecord {
        std::move(agent_uniq),
        0,
        0,
        std::unique_ptr<ts<AgentAction>>(),
        {}
    };

    auto [act, newprice, infoview_ret] = this->do_evaluate(
        record, p_existing, p_current, std::move(info_view)
    );

    record.agent.release();

    return { newprice, act.value() };
}



// returns true if there were any items on the queue
bool 
Market::op_execute_helper() {
    bool ret = false;
    while (this->op_queue.size() > 0) {
        std::shared_ptr<op_abstract> op = this->op_queue.front();
        this->op_queue.pop();
        op->execute(*this);
        ret = true;
    }

    return ret;
}

};