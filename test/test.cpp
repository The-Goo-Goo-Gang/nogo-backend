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

#include <fmt/format.h>
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

constexpr auto host = "127.0.0.1",
               port1 = "2333", port2 = "2334";

struct ServerProcess {
#ifdef _WIN32
    constexpr static auto exec_cmd { "cmd /c start ./nogo-server 2333 2334" };
    constexpr static auto kill_cmd { "taskkill /f /im nogo-server.exe" };
#else
    constexpr static auto exec_cmd { "screen -dmS nogo-server ./nogo-server 2333 2334" };
    constexpr static auto kill_cmd { "screen -S nogo-server -X stuff \"^C\"" };
#endif

    ServerProcess()
    {
        auto ret = system(exec_cmd);
    }
    ~ServerProcess()
    {
        auto ret = system(kill_cmd);
    }
};

asio::io_context io_context { 1 };

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket)
        : socket(std::move(socket))
    {
    }

    ~Session()
    {
        socket.close();
    }

    auto do_read()
    {
        io_context.restart();
        asio::steady_timer timer { io_context, 1000ms };
        timer.async_wait([&](auto ec) {
            if (!ec)
                throw std::runtime_error { fmt::format("timeout reading from {}", to_string()) };
        });
        asio::async_read_until(socket, buffer, '\n', [&](auto ec, auto) {
            if (ec != asio::error::operation_aborted)
                timer.cancel();
        });
        io_context.run();

        std::istream stream(&buffer);
        std::string message;
        std::getline(stream, message);
        return message;
    }

    auto do_read(size_t count)
    {
        return ranges::views::iota(0uL, count)
            | ranges::views::transform([this](auto) { return do_read(); })
            | ranges::to<vector<string>>();
    }

    void do_write(string msg)
    {
        asio::write(socket, asio::buffer(msg + '\n'));
    }

    auto to_string() const -> string
    {
        return socket.remote_endpoint().address().to_string() + ":" + std::to_string(socket.remote_endpoint().port());
    }

    tcp::socket socket;
    asio::streambuf buffer;
    bool timeout { false };
};

auto launch_client(asio::io_context& io_context, string_view ip, string_view port)
{
    tcp::socket socket { io_context };
    socket.connect({ asio::ip::make_address(ip), stoi(port) });
    return std::make_shared<Session>(std::move(socket));
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
};

const vector<vector<string>> recv_msgs1 {
    { R"({"data1":"{TIMEOUT}","data2":"{\"game\":null,\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":false,\"status\":0}","op":100001})" },
    { R"({"data1":"player2","data2":"","op":200000})",
        R"({"data1":"{TIMEOUT}","data2":"{\"game\":{\"chessboard\":[[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[],\"last_move\":null,\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"player2\",\"type\":0},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"player1\",\"type\":0},\"size\":9,\"turn_timeout\":1918967901},\"move_count\":0,\"now_playing\":1,\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})" },

    { R"({"data1":"{TIMEOUT}","data2":"{\"game\":{\"chessboard\":[[1,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[],\"last_move\":{\"x\":0,\"y\":0},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"player2\",\"type\":0},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":1,\"now_playing\":-1,\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})" },
    { R"({"data1":"A2","data2":"1683446066123","op":200002})",
        R"({"data1":"{TIMEOUT}","data2":"{\"game\":{\"chessboard\":[[1,-1,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[],\"last_move\":{\"x\":0,\"y\":1},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"player2\",\"type\":0},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":2,\"now_playing\":1,\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})" },
    { R"({"data1":"{TIMEOUT}","data2":"{\"game\":{\"chessboard\":[[1,-1,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[{\"x\":1,\"y\":0}],\"last_move\":{\"x\":1,\"y\":1},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"player2\",\"type\":0},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":3,\"now_playing\":-1,\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})" },
    { R"({"data1":"B1","data2":"1683446068123","op":200002})",
        R"({"data1":"{TIMEOUT}","data2":"{\"game\":{\"chessboard\":[[1,-1,0,0,0,0,0,0,0],[-1,1,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[{\"x\":0,\"y\":2},{\"x\":2,\"y\":0}],\"last_move\":{\"x\":1,\"y\":0},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"player2\",\"type\":0},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":4,\"now_playing\":1,\"statistics\":[]},\"game_result\":{\"win_type\":2,\"winner\":1},\"is_gaming\":false,\"status\":2}","op":100001})" },

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

TEST(nogo, server)
{
    ServerProcess process {};

    std::this_thread::sleep_for(3s);

    auto c1 = launch_client(io_context, host, port1);
    auto c2 = launch_client(io_context, host, port2);

    std::this_thread::sleep_for(1s);

    int round { send_msgs1.size() * 2 };
    for (auto i : ranges::views::iota(0, round)) {
        auto c { i % 2 ? c2 : c1 };
        auto send_msg { i % 2 ? send_msgs2[i / 2] : send_msgs1[i / 2] };

        for (auto& msg : send_msg)
            c->do_write(msg);

        vector<string> recv_msg1, recv_msg2;

        try {
            recv_msg1 = c1->do_read(recv_msgs1[i].size());
        } catch (const std::exception& e) {
            FAIL() << e.what();
        }
        // fmt::print("\033[31mrecv_msg1: {}\033[0m\n", recv_msg1);

        try {
            recv_msg2 = c2->do_read(recv_msgs2[i].size());
        } catch (const std::exception& e) {
            FAIL() << e.what();
        }

        // fmt::print("\033[32mrecv_msg2: {}\033[0m\n", recv_msg2);

        constexpr string_view placeholder = "{TIMEOUT}";
        constexpr auto timestamp_len = 10;
        for (auto [msg, expect] : ranges::views::zip(recv_msg1, recv_msgs1[i])) {
            auto pos = expect.find(placeholder);
            if (pos == string::npos) {
                EXPECT_EQ(msg, expect);
            } else {
                EXPECT_EQ(msg.substr(0, pos), expect.substr(0, pos));
                EXPECT_EQ(msg.substr(pos + timestamp_len), expect.substr(pos + placeholder.size()));
            }
        }
        for (auto [msg, expect] : ranges::views::zip(recv_msg2, recv_msgs2[i])) {
            auto pos = expect.find(placeholder);
            if (pos == string::npos) {
                EXPECT_EQ(msg, expect);
            } else {
                EXPECT_EQ(msg.substr(0, pos), expect.substr(0, pos));
                EXPECT_EQ(msg.substr(pos + timestamp_len), expect.substr(pos + placeholder.size()));
            }
        }
    }
    // TODO: Send LEAVE_OP
}
int main(int argc, char* argv[])
{
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}