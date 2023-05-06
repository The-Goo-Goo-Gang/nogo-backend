#ifndef _EXPORT
#define _EXPORT
#endif
#include <iostream>
#include <ranges>
#include <vector>

#include "contest.hpp"
#include "network.hpp"

auto main(int argc, char* argv[]) -> int
{
    if (argc < 2) {
        std::cerr << "Usage: server <port> [<port> ...]\n";
        return 1;
    }
    auto ports = std::ranges::subrange(argv + 1, argv + argc)
        | std::views::transform([](auto s) -> unsigned short { return std::atoi(s); })
        | ranges::to<std::vector>();
    launch_server(ports);
}