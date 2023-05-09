#include <charconv>
#include <deque>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

using std::string;
using std::string_view;
using std::vector;
using namespace std::literals::chrono_literals;

#include <asio.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <range/v3/all.hpp>

template <typename T>
constexpr auto stoi_base(string_view str)
{
    T result;
    auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
    switch (ec) {
    case std::errc::invalid_argument:
        throw std::invalid_argument { "no conversion" };
    case std::errc::result_out_of_range:
        throw std::out_of_range { "out of range" };
    default:
        return result;
    };
}
constexpr auto stoi = stoi_base<int>;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket)
        : socket(std::move(socket))
        , timer(socket.get_executor())
    {
        timer.expires_at(std::chrono::steady_clock::time_point::max());
    }

    void start()
    {
        co_spawn(
            socket.get_executor(), [self = shared_from_this()] { return self->reader(); }, detached);
        co_spawn(
            socket.get_executor(), [self = shared_from_this()] { return self->writer(); }, detached);
    }

    void deliver(string msg)
    {
        write_msgs.push_back(msg);
        timer.cancel_one();
    }

    void stop()
    {
        socket.close();
        timer.cancel();
    }

    awaitable<void> reader()
    {
        try {
            for (std::string read_msg;;) {
                std::size_t n = co_await asio::async_read_until(socket, asio::dynamic_buffer(read_msg, 1024), "\n", use_awaitable);
                string msg { read_msg.substr(0, n) };
                read_msgs.push_back(msg);
                read_msg.erase(0, n);
            }
        } catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << "\n";
            stop();
        }
    }

    awaitable<void> writer()
    {
        try {
            while (socket.is_open()) {
                if (write_msgs.empty()) {
                    asio::error_code ec;
                    co_await timer.async_wait(redirect_error(use_awaitable, ec));
                } else {
                    co_await asio::async_write(socket, asio::buffer(write_msgs.front() + "\n"),
                        use_awaitable);
                    write_msgs.pop_front();
                }
            }
        } catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << "\n";
            stop();
        }
    }

    tcp::socket socket;
    asio::steady_timer timer;
    std::deque<string> write_msgs;
    std::deque<string> read_msgs;
};

auto connect(asio::io_context& io_context,
    string_view ip,
    string_view port) -> awaitable<std::shared_ptr<Session>>
{
    tcp::socket socket { io_context };
    try {
        tcp::endpoint endpoint { asio::ip::make_address(ip), (asio::ip::port_type)stoi(port) };
        co_await socket.async_connect(endpoint, use_awaitable);
        co_return std::make_shared<Session>(std::move(socket));
    } catch (std::exception& e) {
        std::cout << e.what() << "\n";
    }
}

bool is_run = false;

auto launch_client(asio::io_context& io_context,
    string_view ip,
    string_view port) -> awaitable<std::shared_ptr<Session>>
{
    auto session = co_await connect(io_context, ip, port);
    session->start();
    co_return session;
}

const vector<vector<string>> send_msgs1 {
    { R"({"op":200000,"data1":"player1","data2":"b"})" },

    { R"({"op":200002,"data1":"A1","data2":"1683446065123"})" },
    { R"({"op":200002,"data1":"B2","data2":"1683446067123"})" },

    {},
};
const vector<vector<string>> send_msgs2 {
    { R"({"op":200000,"data1":"player2","data2":""})" },

    { R"({"op":200002,"data1":"A2","data2":"1683446066123"})" },
    { R"({"op":200002,"data1":"B1","data2":"1683446068123"})" },

    { R"({"op":200005,"data1":"","data2":""})" },

    {},
};

const vector<vector<string>> recv_msgs1 {
    {},
    { R"({"data1":"player2","data2":"","op":200000})" },

    {},
    { R"({"data1":"A2","data2":"1683446065123","op":200002})" },
    {},
    { R"({"data1":"B1","data2":"1683446068123","op":200002})", R"({"data1":"","data2":"","op":100006})" },

    {},
    { R"({"data1":"","data2":"","op":200005})" },
};
const vector<vector<string>> recv_msgs2 {
    { R"({"data1":"player1","data2":"b","op":200000})" },
    {},

    { R"({"data1":"A1","data2":"1683446065123","op":200002})" },
    {},
    { R"({"data1":"B2","data2":"1683446067123","op":200002})" },
    { R"({"data1":"","data2":"","op":200005})" },

    {},
    {},
};

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

string host, port1, port2;

TEST(nogo, server)
{
#ifdef _WIN32
    auto server_cmd = "cmd /c start ./nogo-server " + port1 + " " + port2;
#else
    auto server_cmd = "screen -dmS nogo-server ./nogo-server " + port1 + " " + port2;
#endif
    int ret = system(server_cmd.c_str());

    std::this_thread::sleep_for(3s);

    asio::io_context io_context { 1 };

    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(
        io_context, [&]() -> awaitable<void> {
            auto c1 = co_await launch_client(io_context, host, port1);
            auto c2 = co_await launch_client(io_context, host, port2);

            for (auto i { 0 }; auto [send_msg1, send_msg2] : ranges::views::zip(send_msgs1, send_msgs2)) {
                for (const auto& msg : send_msg1)
                    std::cout << "msg: " << msg << "\n", c1->deliver(msg);
                for (auto [read_msg, recv_msg] : ranges::views::zip(c1->read_msgs, recv_msgs1[i]))
                    EXPECT_EQ(read_msg, recv_msg) << "p1 recv is wrong after p1 send" << fmt::format("{}", send_msg1);
                c1->read_msgs.clear();
                for (auto [read_msg, recv_msg] : ranges::views::zip(c2->read_msgs, recv_msgs2[i]))
                    EXPECT_EQ(read_msg, recv_msg) << "p2 recv is wrong after p1 send" << fmt::format("{}", send_msg1);
                c2->read_msgs.clear();
                std::this_thread::sleep_for(3s);
                ++i;

                for (const auto& msg : send_msg2)
                    c2->deliver(msg);
                for (auto [read_msg, recv_msg] : ranges::views::zip(c1->read_msgs, recv_msgs1[i]))
                    EXPECT_EQ(read_msg, recv_msg) << "p1 recv is wrong after p2 send" << fmt::format("{}", send_msg2);
                c1->read_msgs.clear();
                for (auto [read_msg, recv_msg] : ranges::views::zip(c2->read_msgs, recv_msgs2[i]))
                    EXPECT_EQ(read_msg, recv_msg) << "p2 recv is wrong after p2 send" << fmt::format("{}", send_msg2);
                c2->read_msgs.clear();
                std::this_thread::sleep_for(3s);
                ++i;
            }
            co_return;
        },
        asio::detached);

    io_context.run();

    // c2.write(sendmsg[6]);
    // c2.close();

    // c1.write(sendmsg[6]);
    // c1.close();
}
int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <local_port> <remote_port>\n";
        return 1;
    }
    host = argv[1];
    port1 = argv[2];
    port2 = argv[3];
    std::cout << "port1 " << port1 << std::endl;
    std::cout << "port2 " << port2 << std::endl;

    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}