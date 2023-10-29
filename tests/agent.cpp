

#include "../types.h"
#include "../Agent.h"
#include "../ts.h"
#include "../json_conversion.h"

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


	std::unique_ptr<ts<Info::infoset_t>> info_history(
		new ts<Info::infoset_t>()
	);


	// also works
	//auto d = new D(D { {}, 0, 1.0 });
	//auto d = new D { {}, 0, 1.0 };


	auto info = std::shared_ptr<Info::Info<Info::Types::Subjective>>(
		new Info::Info<Info::Types::Subjective>()
	);

	info->subjectivity_extent = 0.9;
	info->price_indication = 2;
	info->relative = false;


/*	
auto info = std::shared_ptr<Info::Abstract>(
	)
*/

/*
	auto j = std::static_pointer_cast<Info::Abstract>(i);
	info_history->append({ j });
	
	auto k = Info::get_cast<Info::Types::Subjective>(j);
	*/

/*
	var_t v;

	std::visit([](auto&& x) { visit(x); }, v);

	do_visit(visit, v);
	*/
	
	info_history->append({ info });



	try {
		std::unique_ptr<ts<Info::infoset_t>::sparse_view> info_view(
			new ts<Info::infoset_t>::sparse_view(*info_history)
		);
		
		std::unique_ptr<ts<Info::infoset_t>::sparse_view> info_view_ret;


		float external_force = 1;
		int schedule_every = 1;
		double default_variance = 0.1;
		double variance_multiplier = 1;
		double force_threshold = 10;

		while (auto x = 1) { break; }

		auto a = std::unique_ptr<ModeledCohortAgent_v1>(
			new ModeledCohortAgent_v1(json {
				{ "external_force", external_force },
				{ "schedule_every", schedule_every },
				{ "default_variance", default_variance },
				{ "variance_multiplier", variance_multiplier },
				{ "force_threshold", force_threshold },
		//		{ "factor_base", max_variance }
			})
		);

		price_t price = 1;


		for (int i = 0; i < N; i++) {

			auto [act, info_view_ret] = a->evaluate(price, std::move(info_view));
			info_view = std::move(info_view_ret.value());

			auto force = (act->internal_force / 100 * a->config().external_force);
			double factor = act->direction == Direction::UP ? 1 + force : 1 - force;
			price = price * factor;

			//auto act = m.do_evaluate(1);

			print(price, *act);
		}
	} catch (std::invalid_argument& e) {
		std::cout << e.what() << "\n";
	}


}
