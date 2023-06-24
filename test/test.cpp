#include <algorithm>
#include <deque>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using std::string;
using std::string_view;
using std::vector;
using namespace std::literals::chrono_literals;
using namespace std::literals::string_view_literals;

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

#include "../utility.hpp"

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
    socket.connect({ asio::ip::make_address(ip), integer_cast<asio::ip::port_type>(port) });
    return std::make_shared<Session>(std::move(socket));
}

struct MessageFormat {
    string format;
    static constexpr auto placeholder { "{TIMESTAMP}"sv };
    static constexpr auto timestamp_len { 13 };
    constexpr MessageFormat(const char* format)
        : format { format }
    {
    }
    auto operator==(std::string message) const
    {
        if (!format.contains(placeholder)) {
            return message == format;
        }
        auto message_it { message.begin() };
        return std::ranges::all_of(std::ranges::views::split(format, placeholder), [&](auto format_part) {
            bool match = std::equal(format_part.begin(), format_part.end(), message_it);
            message_it += format_part.size() + (format_part.end() != format.end()) * timestamp_len;
            return match;
        }) && message_it == message.end();
    }
    operator const char*() const
    {
        return format.data();
    }
};

const vector<vector<string>> send_msgs1 {
    { R"({"op":100011,"data1":"Player1","data2":"30"})" },
    { R"({"op":100015,"data1":"","data2":""})" },

    { R"({"op":200002,"data1":"A1","data2":"1683446065123"})" },
    { R"({"op":200002,"data1":"B2","data2":"1683446067123"})" },

    {},
    {}
};
const vector<vector<string>> send_msgs2 {
    { R"({"op":200000,"data1":"Player2","data2":"w"})" },
    {},

    { R"({"op":200002,"data1":"A2","data2":"1683446066123"})" },
    { R"({"op":200002,"data1":"B1","data2":"1683446068123"})" },

    { R"({"op":200005,"data1":"","data2":""})" },
    { R"({"op":200007,"data1":"","data2":""})" }
};

const vector<vector<MessageFormat>> recv_msgs1 {
    {},
    { R"({"data1":"Player2","data2":"w","op":100014})",
        R"({"data1":"{TIMESTAMP}","data2":"{\"game\":null,\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":false,\"status\":0}","op":100001})" },
    { R"({"data1":"{TIMESTAMP}","data2":"{\"game\":{\"chessboard\":[[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[],\"encoded\":\"\",\"end_time\":0,\"is_replaying\":false,\"last_move\":null,\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"Player2\",\"type\":1},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"Player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":0,\"now_playing\":1,\"should_giveup\":false,\"start_time\":{TIMESTAMP},\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})" },
    {},

    { R"({"data1":"{TIMESTAMP}","data2":"{\"game\":{\"chessboard\":[[1,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[],\"encoded\":\"A1\",\"end_time\":0,\"is_replaying\":false,\"last_move\":{\"x\":0,\"y\":0},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"Player2\",\"type\":1},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"Player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":1,\"now_playing\":-1,\"should_giveup\":false,\"start_time\":{TIMESTAMP},\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})" },
    { R"({"data1":"{TIMESTAMP}","data2":"{\"game\":{\"chessboard\":[[1,-1,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[],\"encoded\":\"A1 A2\",\"end_time\":0,\"is_replaying\":false,\"last_move\":{\"x\":0,\"y\":1},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"Player2\",\"type\":1},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"Player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":2,\"now_playing\":1,\"should_giveup\":false,\"start_time\":{TIMESTAMP},\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})",
        R"({"data1":"A2","data2":"1683446066123","op":200002})" },
    { R"({"data1":"{TIMESTAMP}","data2":"{\"game\":{\"chessboard\":[[1,-1,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[{\"x\":1,\"y\":0}],\"encoded\":\"A1 A2 B2\",\"end_time\":0,\"is_replaying\":false,\"last_move\":{\"x\":1,\"y\":1},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"Player2\",\"type\":1},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"Player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":3,\"now_playing\":-1,\"should_giveup\":false,\"start_time\":{TIMESTAMP},\"statistics\":[]},\"game_result\":{\"win_type\":0,\"winner\":0},\"is_gaming\":true,\"status\":1}","op":100001})" },
    { R"({"data1":"2","data2":"","op":100006})",
        R"({"data1":"{TIMESTAMP}","data2":"{\"game\":{\"chessboard\":[[1,-1,0,0,0,0,0,0,0],[-1,1,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[{\"x\":0,\"y\":2},{\"x\":2,\"y\":0}],\"encoded\":\"A1 A2 B2 B1\",\"end_time\":{TIMESTAMP},\"is_replaying\":false,\"last_move\":{\"x\":1,\"y\":0},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"Player2\",\"type\":1},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"Player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":4,\"now_playing\":1,\"should_giveup\":false,\"start_time\":{TIMESTAMP},\"statistics\":[]},\"game_result\":{\"win_type\":2,\"winner\":1},\"is_gaming\":false,\"status\":2}","op":100001})",
        R"({"data1":"B1","data2":"1683446068123","op":200002})" },

    {},
    { R"({"data1":"{TIMESTAMP}","data2":"{\"game\":{\"chessboard\":[[1,-1,0,0,0,0,0,0,0],[-1,1,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0],[0,0,0,0,0,0,0,0,0]],\"disabled_positions\":[{\"x\":0,\"y\":2},{\"x\":2,\"y\":0}],\"encoded\":\"A1 A2 B2 B1\",\"end_time\":{TIMESTAMP},\"is_replaying\":false,\"last_move\":{\"x\":1,\"y\":0},\"metadata\":{\"player_opposing\":{\"avatar\":\"\",\"chess_type\":-1,\"name\":\"Player2\",\"type\":1},\"player_our\":{\"avatar\":\"\",\"chess_type\":1,\"name\":\"Player1\",\"type\":0},\"size\":9,\"turn_timeout\":30},\"move_count\":4,\"now_playing\":1,\"should_giveup\":false,\"start_time\":{TIMESTAMP},\"statistics\":[]},\"game_result\":{\"win_type\":2,\"winner\":1},\"is_gaming\":false,\"status\":2}","op":100001})" },
    {},
    { R"({"data1":"Player2","data2":"","op":200007})" },
};
const vector<vector<string>> recv_msgs2 {
    {},
    {},
    { R"({"data1":"Player1","data2":"","op":200000})" },
    {},

    { R"({"data1":"A1","data2":"1683446065123","op":200002})" },
    {},
    { R"({"data1":"B2","data2":"1683446067123","op":200002})" },
    { R"({"data1":"","data2":"","op":200005})" },

    {},
    {},
    {},
    {},
};

TEST(nogo, server)
{
    ServerProcess process {};

    std::this_thread::sleep_for(3s);

    auto c1 = launch_client(io_context, host, port1);
    auto c2 = launch_client(io_context, host, port2);

    std::this_thread::sleep_for(1s);

    auto round { send_msgs1.size() * 2 };
    for (auto i : ranges::views::iota(0uZ, round)) {
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

        for (auto [msg, expect] : ranges::views::zip(recv_msg1, recv_msgs1[i])) {
            EXPECT_EQ(msg, expect) << "message not match in round " << i + 1;
        }
        for (auto [msg, expect] : ranges::views::zip(recv_msg2, recv_msgs2[i])) {
            EXPECT_EQ(msg, expect) << "message not match in round " << i + 1;
        }
    }
}
int main(int argc, char* argv[])
{
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}