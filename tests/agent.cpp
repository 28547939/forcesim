

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

namespace {
std::map<int, std::function<int(int)>
> x = {{}};
};

/*
typedef std::variant<Info::Info<Info::Types::Subjective>, Info::Info<Info::Types::Test1>> var_t;

void visit(Info::Info<Info::Types::Subjective>) {}
void visit(Info::Info<Info::Types::Test1>) {}

template<typename T>
void do_visit(std::function<void(T)> f, var_t v) {
	std::visit([&f](auto&& x) { f(x); }, v);
}
*/

int main(int argc, char* argv[]) {
	int N;

    po::options_description desc("Options");
    desc.add_options()
        ("count", po::value<int>()->required(), "")
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




	// also works
	//auto d = new D(D { {}, 0, 1.0 });
	//auto d = new D { {}, 0, 1.0 };


	auto info = std::shared_ptr<Info::Info<Info::Types::Subjective>>(
		new Info::Info<Info::Types::Subjective>()
	);

    Info::infoset_t infoset = { info };

	info->subjectivity_extent = 0.9;
	info->price_indication = 2;
	info->is_relative = false;


    std::shared_ptr<Market::Market> market(new Market::Market());

	try {
		
		//std::unique_ptr<ts<Info::infoset_t>::sparse_view> info_view_ret;


		float external_force = 1;
		int schedule_every = 1;
		double default_variance = 0.1;
		double variance_multiplier = 1;
		double force_threshold = 10;

		while (auto x = 1) { break; }

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

        //auto record = get_agentrecord(std::move(a));

		price_t price = 1;


		for (int i = 0; i < N; i++) {
            price_t existing_price = price;

/*
			auto [act, info_view_ret] = a->evaluate(existing_price, std::move(info_view));
			info_view = std::move(info_view_ret.value());

			auto force = (act->internal_force / 100 * a->config().external_force);
			double factor = act->direction == Direction::UP ? 1 + force : 1 - force;
			price = price * factor;

std::pair<price_t, AgentAction>
Market::test_evaluate(
    std::shared_ptr<Agent> agent, 
    price_t p_existing, 
    price_t p_current,
    std::optional<Info::infoset_t>& info)
            */

			auto [new_price, act] = market->test_evaluate(a, existing_price, price, infoset);


			print_agentaction(price, act);
		}
	} catch (std::invalid_argument& e) {
		std::cout << e.what() << "\n";
	}


}
