

#include "../types.h"
#include "../Agent.h"
#include "../ts.h"
#include "../json_conversion.h"

#include "../Market.h"

#include <iostream>
#include <optional>
#include <memory>
#include <variant>

#include <boost/program_options.hpp>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

void print_agentaction(price_t p, AgentAction a) {
	std::cout 
		<< "price=" << std::to_string(static_cast<double>(p))
		<< " direction=" 
		<< (a.direction == Direction::UP ? "UP" : "DOWN")
		<< " internal_force=" << std::to_string(a.internal_force)
		<< "\n"
	;
}

void print_distribution(std::shared_ptr<ModeledCohortAgent_v2> a, price_t p) {
    auto pts = a->compute_distribution_points(p);

    std::ostringstream s;
    for (auto x = pts.first.begin(), y = pts.second.begin(); x != pts.first.end(); ++x, ++y) {
        s << "(" + std::to_string(*x) +", "+ std::to_string(*y) +")\n";
    }

    std::cout << s.str();
}


// create a 'dummy' AgentRecord object to allow us to call Market::do_evaluate
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
        ("count", po::value<int>()->default_value(10), "")
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




	auto info = std::shared_ptr<Info::Info<Info::Types::Subjective>>(
		new Info::Info<Info::Types::Subjective>()
	);


	info->subjectivity_extent = 0.9;
	info->price_indication = 2;
	info->is_relative = false;

    Info::infoset_t infoset = { std::static_pointer_cast<Info::Abstract>(info) };


    std::shared_ptr<Market::Market> market(new Market::Market());

	try {
		
		//std::unique_ptr<ts<Info::infoset_t>::sparse_view> info_view_ret;


		float external_force = 1;
		int schedule_every = 1;
		double initial_variance = 0.1;
		double variance_multiplier = 1;
		double force_threshold = 10;

		while (auto x = 1) { break; }

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

		auto a = std::shared_ptr<ModeledCohortAgent_v2>(
			new ModeledCohortAgent_v2(json {
				{ "external_force", external_force },
				{ "schedule_every", schedule_every },
				{ "initial_variance", initial_variance },
				{ "variance_multiplier", variance_multiplier },
				{ "force_threshold", force_threshold },
                { "distribution_parameters", json::array({
                    0.1,
                    0.1,
                    0.1,
                    0.1,
                    0.1,
                    0.1,
                    0.1,
                    0.1
                })}
            })
        );

        //auto record = get_agentrecord(std::move(a));

		price_t price = 1;

        std::optional<Info::infoset_t> infoset_opt(infoset);


		for (int i = 0; i < N; i++) {
            // use when there are multiple agent instances being tested
            price_t existing_price = price;

            print_distribution(a, price);

			auto [new_price, act] = market->test_evaluate(a, existing_price, price, infoset_opt);

            infoset_opt.reset();

			print_agentaction(price, act);

            price = new_price;
		}
	} catch (std::invalid_argument& e) {
		std::cout << e.what() << "\n";
	}
}
