import nogo.network;
import nogo.contest;
import std;

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: server <port> [<port> ...]\n";
        return 1;
    }
    auto ports = std::vector(argv + 1, argc + argv)
        | std::views::transform([](auto s) -> unsigned short { return std::atoi(s); })
        | std::ranges::to<std::vector>();
    launch_server(ports);
}