#ifndef FACTORY_H
#define FACTORY_H

#include "../types.h"
#include "../Market.h"
#include "../Info.h"
#include "../json_conversion.h"

#include "common.h"
#include "Subscribers.h"


#include <glog/logging.h>

#include <asio.hpp>

#include <chrono>
#include <memory>



namespace Subscriber {


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
// it needs to be copyable
// 
// in the future, we might have RecordType = Info (with parameter being the type of Info) or 
// RecordType = AgentTrace (parameter being agent ID)

template<typename RecordType>
struct FactoryParameter;

template<>
struct FactoryParameter<Agent::AgentAction> {
    Market::agentid_t id;
    auto operator<=> (const FactoryParameter<Agent::AgentAction>&) const = default;
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
// defined in implementation.cpp
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

    inline static typename std::map<FactoryParameter<RecordType>, std::set<id_t>> 
    get_idmap() {
        return idmap;
    }


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

    // TODO if we continue with this approach, we will eventually need to implement a base 
    // class covering both ::view and ::sparse_view, for the case of Info::infoset_t subscribers
    virtual typename ts<RecordType>::view
    get_iterator(std::shared_ptr<Market::Market> m, const timepoint_t& tp) {
        return get_iterator_helper<RecordType>(m, tp, this->param);
    }


    virtual bool wait(const timepoint_t& tp) {
        return wait_matching(this->param, tp);
    }

    // wait for all subscribers which have our parameter
    // See AbstractSubscriber::wait
    static bool wait_matching(FactoryParameter<RecordType> x, const std::optional<timepoint_t> tp) {
        auto idmap = Factory<RecordType>::idmap;
        auto it = idmap.find(x);

        if (it == idmap.end()) {
            return false;
        } else {
            std::for_each(idmap.begin(), idmap.end(), [&tp](auto pair) {
                auto idset = pair.second;
                std::for_each(idset.begin(), idset.end(), [&tp](id_t id) {
                    Subscribers::wait(id, tp);
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

};


#endif
