#include <iostream>
#include <ranges>
#include <vector>

#include "contest.hpp"
#include "network.hpp"
#include "log.hpp"

auto main(int argc, char* argv[]) -> int
{
    init_log();
    for(int i=0;i<argc;i++) logger->info("argv[{}]:{}",i,argv[i]);
    if (argc < 2) {
        std::cerr << "Usage: server <port> [<port> ...]\n";
        logger->error("Usage: server <port> [<port> ...]\n");
        return 1;
    }
    auto ports = std::ranges::subrange(argv + 1, argv + argc)
        | std::views::transform([](auto s) -> unsigned short { return std::atoi(s); })
        | ranges::to<std::vector>();
    launch_server(ports);
}