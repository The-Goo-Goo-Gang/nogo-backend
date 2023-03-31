#include <iostream>
#include <ranges>
#include <vector>

#include "network.hpp"
#include "contest.hpp"

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