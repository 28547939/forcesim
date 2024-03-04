

#include "Market.h"
#include "Agent.h"
#include "Subscriber.h"
#include "Interface.h"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <deque>

#include "json_conversion.h"

#include <asio.hpp>
#include <boost/program_options.hpp>


namespace {
    volatile std::sig_atomic_t signal_status;
}
void signal_handler (int s) {
    signal_status = s;
    Subscriber::Subscribers::shutdown_signal.store(true);
    Market::Market::shutdown_signal.store(true);

    // TODO


};
namespace po = boost::program_options;

int main (int argc, char* argv[]) {
    FLAGS_stderrthreshold=0;
    FLAGS_logtostderr=1;
    FLAGS_v=9;

    google::InitGoogleLogging(argv[0]);




    po::options_description desc("Options");
    desc.add_options()
        ("help", "...")
        ("interface-address", po::value<std::string>()->default_value("127.0.0.1"), "")
        ("interface-port", po::value<int>()->default_value(18080), "")
        ("iter-block", po::value<int>()->default_value(1000), "")
        ("subscriber-poll-interval", po::value<int>()->default_value(5000), "")
        ("subscriber-max-records", po::value<int>()->default_value(1000), "")
    ;
    

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 1;
        }

        po::notify(vm);
    } catch (po::error& e) {
        std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }


    std::shared_ptr<Market::Market> m(new Market::Market());
    auto t0 = m->launch();

    m->configure({
        vm["iter-block"].as<int>()
    });

    Subscriber::Subscribers::manager_thread_poll_interval.store(
        vm["subscriber-poll-interval"].as<int>()
    );

    auto t1 = std::thread([&vm]() {
        Subscriber::Subscribers::launch_manager_thread(
            vm["subscriber-max-records"].as<int>()
        );
    });

    asio::ip::address listen_addr = asio::ip::address::from_string(
        vm["interface-address"].as<std::string>()
    );

    int listen_port = vm["interface-port"].as<int>();

    auto t2 = std::thread([m, listen_addr, listen_port]() {
        auto i = Interface::get_instance(m);

        i->start(listen_addr, listen_port);
    });


    t0.join();
    t1.join();
    t2.join();



}