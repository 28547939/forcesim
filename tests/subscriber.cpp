#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <deque>
#include <random>
#include <ostream>


#include "../forcesim.h"

#include "../json_conversion.h"

#include <boost/asio.hpp>

std::string ids_str(std::set<Subscriber::id_t> ids) {
    std::ostringstream ids_str;
    ids_str << "size=" << ids.size() << " ";
    for (auto id : ids) {
        ids_str << id.to_string() << " ";
    }

    return ids_str.str();
}


// TODO split each test into a separate function


int main (int argc, char* argv[]) {

    forcesim_client client ({ forcesim_component::Market, forcesim_component::Subscribers });

    // add agent_count
    client.options_desc.add_options()
        ("agent-count", po::value<int>()->default_value(10), "")
        ("portrange-low", po::value<int>()->default_value(5000), "")
        ("portrange-high", po::value<int>()->default_value(5010), "")
        ("subscriber-count", po::value<int>()->default_value(256), "")
    ;

    client.parse_cli(argc, argv);

    // generate a random number of subscribers for each port in a range
    std::random_device dev; 
    std::mt19937 engine(dev());
    std::uniform_int_distribution<> dist(
        client.options_vm["portrange-low"].as<int>(), 
        client.options_vm["portrange-high"].as<int>()
    );

    // subscribers on a given endpoint
    std::unordered_map<Subscriber::EndpointConfig, std::set<Subscriber::id_t>, Subscriber::EndpointConfig::Key> 
    endpoint_ids;

    for (int i = 0; i < client.options_vm["subscriber-count"].as<int>(); ++i) {
        int port = dist(engine);

        // currently only tests price_t subscribers; meant to test the parts of the subscriber
        // functionality that are generic across subscriber types, and price_t has a trivial 
        // FactoryParameter
        Subscriber::id_t id = 
            client.add_subscriber<price_t>("127.0.0.1", port, {});

        Subscriber::EndpointConfig ec("127.0.0.1", port);
        auto it = endpoint_ids.find(ec);
        int count = 0;
        if (it == endpoint_ids.end()) {
            endpoint_ids.insert({ ec, { id } });
        } else {
            it->second.insert(id);
        }
    }

    auto desc_map = Subscriber::Endpoints::describe();

    // test: total-count
    try {
        for (auto& [ec, data] : desc_map) {
            auto [emitted, use_count] = data;
            int added_count = endpoint_ids[ec].size();

            VLOG(1) 
                << "total-count: "
                << "EndpointConfig=" << ec 
                << " added_count=" << added_count 
                << " use_count=" << use_count
            ;

            // Endpoint's shared_ptr use_count should be equal to the number of subscribers, plus 1
            // for the entry in the Endpoints::endpoints map
            if (use_count != added_count + 1) {

                LOG(ERROR) 
                    << "endpoint use_count mismatch: use_count=" << use_count 
                    << " added_count+1=" << added_count+1;
            }
        }
    } catch (std::out_of_range& e) {
        LOG(ERROR) << "could not check use_count: Subscriber not present in endpoint_ids map";
    }

    // check that the Factory idmap contains the correct number of entries, and also
    // that the most recently deleted element in absent
    auto check_factory_idmap = [](
        std::set<Subscriber::id_t> local_ids, std::size_t required_size, Subscriber::id_t absent) 
    {
        auto idmap = Subscriber::Factory<price_t>::get_idmap();
        std::set<Subscriber::id_t> factory_ids = idmap[Subscriber::FactoryParameter<price_t>({})];

        VLOG(1) << ids_str(factory_ids);
        VLOG(1) << ids_str(local_ids);

        // consider only those factory_ids associated with this EndpointConfig
        std::erase_if(factory_ids, [&local_ids](auto& id) { return ! local_ids.contains(id); });

        if (factory_ids.size() != required_size) {
            LOG(ERROR) << "check_factory_idmap:"
                << " size=" << factory_ids.size()
                << " required_size=" << required_size
                << " factory_ids=" << ids_str(factory_ids)
            ;


        }

        if (factory_ids.contains(absent)) {
            LOG(ERROR) 
                << "check_ids:"
                << " subscriber with ID=" << absent
                << " is present but should not be"
            ;
        }

    };

    // final-count
    for (auto& [ec, local_ids] : endpoint_ids) {
        desc_map = Subscriber::Endpoints::describe();
        VLOG(1) 
            << "final-count: before deletion: "
            << "EndpointConfig=" << ec 
            << " use_count=" << desc_map[ec].second
            << " local_ids=" << ids_str(local_ids)
        ;

        int i = 0;
        Subscriber::id_t last_id;
        for (auto id : local_ids) {
            i++;
            last_id = id;
            // treat the final removal separately
            if (i == local_ids.size()) {
                break;
            }

            Subscriber::Subscribers::del(id);
            check_factory_idmap(local_ids, local_ids.size()-i, id);
        }

        desc_map = Subscriber::Endpoints::describe();
        auto use_count = desc_map[ec].second;
        

        VLOG(1) 
            << "final-count: after deletion: "
            << "EndpointConfig=" << ec 
            << " use_count=" << use_count
        ;

        if (use_count != 2) {
            LOG(ERROR) << "one endpoint remaining, but use_count=" << use_count;
            continue;
        }

        Subscriber::Subscribers::del(last_id);
        desc_map = Subscriber::Endpoints::describe();

        check_factory_idmap(local_ids, 0, last_id);

        if (desc_map.find(ec) != desc_map.end()) {
            LOG(ERROR) << "all subscribers deleted for EndpointConfig=" << ec
                << " but still present in Endpoints::endpoints";
        }
    }

    for (int i = 0; i < client.options_vm["agent-count"].as<int>(); ++i) {
        client.add_agent<Agent::AgentType::BasicNormalDist>(
            {
                { "mean", 0 },
                { "stddev", 10 },
                { "schedule_every", 2 },
                { "external_force", 0.01 }
            }
        );
    }

    // TODO run Market and check data as it passes through Subscriber

}