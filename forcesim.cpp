#include "forcesim.h"

int main (int argc, char* argv[]) {
    using fc = forcesim_component;
    forcesim_client client {{ fc::Subscribers, fc::Market, fc::Interface }};
    client.parse_cli(argc, argv);
    client.run();
    client.exit(0);
}
