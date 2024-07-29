

#include "../ts.h"
#include "../json_conversion.h"

#include "../Market.h"

#include "common.h"

#include <iostream>
#include <optional>
#include <memory>
#include <variant>

#include <boost/program_options.hpp>

#include <nlohmann/json.hpp>

using json = nlohmann::json;


/*  agent.cpp - for experimenting with and testing specific agents
    not for testing agent functionality in general: TODO in market.cpp and elsewhere
*/

// create a 'dummy' AgentRecord object to allow us to call Market::do_evaluate
// no longer used - we have Market::test_evaluate
/*
AgentRecord get_agentrecord(std::unique_ptr<Agent> a) {

    return std::move(AgentRecord {
        std::move(a),
        0
        0
        std::unique_ptr<ts<AgentAction>>(),
        {}
    });
}
*/

namespace po = boost::program_options;

int main(int argc, char* argv[]) {

    FLAGS_stderrthreshold=0;
    FLAGS_logtostderr=1;
    FLAGS_v=9;

    google::InitGoogleLogging(argv[0]);

	int N;

    po::options_description desc("Options");
    desc.add_options()
        ("agent-config-path", po::value<std::string>(), "")
        ("agent-config-key", po::value<std::string>(), "")

        ("info-config-path", po::value<std::string>(), "")
        ("info-config-key", po::value<std::string>(), "")

        ("iteration-count", po::value<int>()->default_value(10), 
            "Execute the agent over how many iterations"
        )
    ;

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("count")) {
			N = vm["count"].as<int>();
        }

        po::notify(vm);

    } catch (po::error& e) {
        std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
        return 1;
    }



/*  No longer manually specifying
	auto info = std::shared_ptr<Info::Info<Info::Types::Subjective>>(
		new Info::Info<Info::Types::Subjective>()
	);

	info->subjectivity_extent = vm["s"].as<float>();
	info->price_indication = price_t(vm["p"].as<double>());
	info->is_relative = false;
    */

    std::shared_ptr<Info::Abstract> info = info_from_file(
        vm["info-config-path"].as<std::string>(),
        vm["info-config-key"].as<std::string>()
    );

    // cast only needed when we construct the derived type manually
    //Info::infoset_t infoset = { std::static_pointer_cast<Info::Abstract>(info) };

    Info::infoset_t infoset = { info };


    std::shared_ptr<Market::Market> market(new Market::Market());

	try {
		
		//std::unique_ptr<ts<Info::infoset_t>::sparse_view> info_view_ret;


/*
		float external_force = 1;
		int schedule_every = 1;
		double initial_variance = 0.1;
		double variance_multiplier = 1;
		double force_threshold = 10;
*/

            /*
		auto a = std::shared_ptr<ModeledCohortAgent_v1>(
			new ModeledCohortAgent_v1(json {
				{ "external_force", external_force },
				{ "schedule_every", schedule_every },
				{ "default_variance", default_variance },
				{ "variance_multiplier", variance_multiplier },
				{ "force_threshold", force_threshold },
		//		{ "factor_base", max_variance }
			})
		);
            */
        

        
        auto agent = agent_from_file(
            vm["agent-config-path"].as<std::string>(),
            vm["agent-config-key"].as<std::string>()
        );

        std::shared_ptr<ModeledCohortAgent_v2> agent_ptr(&agent);

        /*

		auto a = std::shared_ptr<ModeledCohortAgent_v2>(
			new ModeledCohortAgent_v2(json {
				{ "external_force", external_force },
				{ "schedule_every", schedule_every },
				{ "initial_variance", initial_variance },
				{ "variance_multiplier", variance_multiplier },
				{ "force_threshold", force_threshold },
                { "distribution_parameters", json::array({
                    0,  0.1,    0.1,    1,  0.1,    0.5,    0.01,   0
                    
                //  e_0 i_0     r_0     r_1 r_2     i_1     i_2     e_1
                })}
            })
        );
        */

        // no longer needed
        //auto record = get_agentrecord(std::move(a));

		price_t price = 1;

        std::optional<Info::infoset_t> infoset_opt(infoset);


		for (int i = 0; i < N; i++) {
            // use when there are multiple agent instances being tested
            price_t existing_price = price;

			auto [new_price, act] = market->test_evaluate(agent_ptr, existing_price, price, infoset_opt);
            print_distribution(agent_ptr, price);

            infoset_opt.reset();

			print_agentaction(price, act);

            price = new_price;
		}
	} catch (std::invalid_argument& e) {
		std::cout << e.what() << "\n";
	}
}
