#include "asio.hpp"
#include "windows.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <thread>

using asio::ip::tcp;
using namespace std;

bool all_connected = false;
class chat_client {
public:
    chat_client(asio::io_context& io_context, const tcp::resolver::results_type& endpoints)
        : io_context_(io_context)
        , socket_(io_context)
    {
        do_connect(endpoints);
    }
    void write(string msg)
    {
        asio::post(io_context_,
            [this, msg]() {
                bool write_in_progress = !write_msgs_.empty();
                write_msgs_.push_back(msg);
                if (!write_in_progress)
                    do_write();
            });
    }
    void close()
    {
        all_connected = false;
        asio::post(io_context_, [this]() { socket_.close(); });
    }
    void do_connect(const tcp::resolver::results_type& endpoints)
    {
        asio::async_connect(socket_, endpoints, [this](std::error_code ec, tcp::endpoint) {
            if (!ec)
                do_read();
            else
                all_connected = false;
        });
    }
    void do_read()
    {
        asio::async_read_until(socket_, asio::dynamic_buffer(read_msg_, 4096), "\n", [this](std::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                read_msgs_.push_back(read_msg_);
                read_msg_.erase();
                do_read();
            } else {
                all_connected = false;
                socket_.close();
            }
        });
    }
    void do_write()
    {
        asio::async_write(socket_, asio::buffer(write_msgs_.front()), [this](std::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                write_msgs_.pop_front();
                if (!write_msgs_.empty())
                    do_write();
            } else {
                all_connected = false;
                socket_.close();
            }
        });
    }
    asio::io_context& io_context_;
    tcp::socket socket_;
    string read_msg_;
    deque<string> write_msgs_;
    vector<string> read_msgs_;
};
