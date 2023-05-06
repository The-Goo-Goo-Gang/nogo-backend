#include <cstdlib>
#include <string>
#include <deque>
#include <iostream>
#include <thread>
#include "asio.hpp"
#include "message.hpp"
using asio::ip::tcp;

class chat_client
{
  public:
    chat_client(asio::io_context& io_context,const tcp::resolver::results_type& endpoints):
        io_context_(io_context),
        socket_(io_context)
    {
        do_connect(endpoints);
    }
    void write(const string& msg)
    {
    asio::post(io_context_,
        [this, msg]()
        {
            bool write_in_progress = !write_msgs_.empty();
            write_msgs_.push_back(msg);
            if (!write_in_progress) do_write();
        });
    }
    void close()
    {
        asio::post(io_context_, [this]() { socket_.close(); });
    }
  private:
    void do_connect(const tcp::resolver::results_type& endpoints)
    {
        asio::async_connect(socket_, endpoints,[this](std::error_code ec, tcp::endpoint)
        {
            if (!ec) do_read();
        });
    }
    void do_read()
    {
    asio::async_read(socket_,asio::buffer(&read_msg_, sizeof(read_msg_)),[this](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            read_msgs_.push_back(read_msg_);
            do_read();
          }
          else socket_.close();
        });
    }
    void do_write()
    {
        asio::async_write(socket_,asio::buffer(write_msgs_.front()),[this](std::error_code ec, std::size_t /*length*/)
        {
            if (!ec)
            {
                std::cout<<"send "<<write_msgs_.front()<<"\n";
                write_msgs_.pop_front();
                if (!write_msgs_.empty())  do_write();
            }
            else socket_.close();
        });
    }
  private:
    asio::io_context& io_context_;
    tcp::socket socket_;
    string read_msg_;
    std::deque<string> write_msgs_;
    std::vector<string> read_msgs_;
};
