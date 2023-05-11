//
// chat_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once
#ifndef _EXPORT
#define _EXPORT
#endif

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <chrono>
#include <deque>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "contest.hpp"
#include "log.hpp"
#include "message.hpp"
#include "uimessage.hpp"

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::redirect_error;
using asio::use_awaitable;
using asio::ip::tcp;

using namespace std::chrono_literals;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

constexpr auto TIMEOUT { 30s };

class Room;

void start_session(asio::io_context&, Room&, asio::error_code&, std::string_view, std::string_view);

class Room {
    Contest contest;
    std::deque<std::string> chats;

    void deliver_to_local(Message msg)
    {
        for (auto participant : participants_) {
            if (participant->is_local)
                participant->deliver(msg);
        }
    }

    void deliver_ui_state()
    {
        deliver_to_local(UiMessage { contest });
    }

public:
    Room(asio::io_context& io_context)
        : timer_ { io_context }
        , io_context_ { io_context }
    {
    }
    /*
    建立连接成功时，客户端是请求对局方，也就是服务端在收到客户端任何通信前不与客户端进行交流。
    对局结束后，或暂时拒绝后，想再来一局（重发）的是请求对局方。另外，如果已经收到对方想再来一局了，程序应该阻止本方重复发送请求，而引导先接受或拒绝。你可以不处理因信息传输导致的同时发送，你可以假设不存在这种情况。
    */
    void process_data(Message msg, Participant_ptr participant)
    {
        logger->info("process_data: {} from {}", msg.to_string(), participant->to_string());
        const string_view data1 { msg.data1 }, data2 { msg.data2 };

        switch (msg.op) {
        case OpCode::WIN_PENDING_OP: {
            break;
        }
        case OpCode::UPDATE_UI_STATE_OP: {
            break;
        }
        case OpCode::CONNECT_TO_REMOTE_OP: {
            asio::error_code ec;
            start_session(io_context_, *this, ec, data1, data2);
            if (ec) {
                logger->error("start_session failed: {}", ec.message());
                participant->deliver({ OpCode::CONNECT_RESULT_OP, "failed", ec.message() });
            } else {
                logger->info("start_session success: {}:{}", data1, data2);
                participant->deliver({ OpCode::CONNECT_RESULT_OP, "success", msg.data1 + ":" + msg.data2 });
            }
            break;
        }
        case OpCode::CONNECT_RESULT_OP: {
            // should not be sent by client
            break;
        }
        case OpCode::START_LOCAL_GAME_OP: {
            std::cout << "start local game: timeout = " << data1 << ", size = " << data2 << std::endl;
            if (contest.status != Contest::Status::NOT_PREPARED) {
                contest.clear();
            }
            // int timeout = std::stoi(msg.data1);
            // int rank_n = std::stoi(msg.data2);

            seconds duration { std::stoi(msg.data1) };
            contest.duration = duration;

            Player player1 { participant, "BLACK", Role::BLACK, PlayerType::LOCAL_HUMAN_PLAYER },
                player2 { participant, "WHITE", Role::WHITE, PlayerType::LOCAL_HUMAN_PLAYER };
            try {
                contest.enroll(std::move(player1)), contest.enroll(std::move(player2));
            } catch (Contest::StatusError& e) {
                logger->error("Ignore enroll player: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
                break;
            } catch (std::exception& e) {
                logger->error("Ignore enroll Player: {}, player1: {}, player2: {}. playerlist: {}.",
                    e.what(), player1.to_string(), player2.to_string(), contest.players.to_string());
                contest.players = {};
                break;
            }
            contest.local_role = Role::BLACK;
            deliver_ui_state();
            break;
        }
        case OpCode::LOCAL_GAME_TIMEOUT_OP: {
            // Deprecated
            break;
        }
        case OpCode::LOCAL_GAME_MOVE_OP: {
            timer_.cancel();

            Position pos { data1 };
            Role role { data2 };

            Player player, opponent;
            try {
                player = Player { contest.players.at(role, participant) };
                opponent = Player { contest.players.at(-player.role) };
            } catch (std::exception& e) {
                logger->error("Ignore move: {}, playerlist: {}, try to find role {}, participant {}",
                    e.what(), contest.players.to_string(), role.to_string(), participant->to_string());
                break;
            }

            try {
                contest.play(player, pos);
            } catch (Contest::StatusError& e) {
                logger->error("Ignore move: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
                break;
            } catch (std::exception& e) {
                logger->error("Ignore move: {}, player:{}", e.what(), player.to_string());
                break;
            }

            if (contest.status == Contest::Status::ON_GOING) {
                timer_.expires_after(contest.duration);
                timer_.async_wait([this, opponent](const asio::error_code& ec) {
                    if (!ec) {
                        contest.timeout(opponent);
                        opponent.participant->deliver({ OpCode::TIMEOUT_END_OP });
                        deliver_ui_state();
                    }
                });
            }

            deliver_ui_state();
            break;
        }
        case OpCode::READY_OP: {
            logger->info("ready: is_local = {}, data1 = {}, data2 = {}", participant->is_local, data1, data2);

            if (contest.status == Contest::Status::GAME_OVER) {
                contest.clear();
            }

            auto name { data1 };
            Role role { data2 };

            if (!Player::is_valid_name(name)) {
                name = "Player" + std::to_string(contest.players.size() + 1);
                logger->warn("READY_OP: invalid name {}, use default name: {}", data1, name);
            }
            participant->set_name(name);
            auto player_type { participant->is_local
                    ? PlayerType::LOCAL_HUMAN_PLAYER
                    : PlayerType::REMOTE_HUMAN_PLAYER };
            Player player { participant, name, role, player_type };
            try {
                contest.enroll(std::move(player));

            } catch (Contest::StatusError& e) {
                logger->error("Ignore enroll player: Contest status is {}", e.what(), std::to_underlying(contest.status));
                break;
            } catch (std::exception& e) {
                logger->error("Ignore enroll Player: {}, player: {}. playerlist: {}.",
                    e.what(), player.to_string(), contest.players.to_string());
                break;
            }
            if (participant->is_local) {
                contest.local_role = role;
                // TODO: support multiple remote waiting players
                deliver_to_others(msg, participant);
            } else {
                deliver_to_local(msg);
            }
            deliver_ui_state();
            break;
        }
        case OpCode::REJECT_OP: {
            contest.reject();

            if (participant->is_local) {
                // TODO: support multiple remote waiting players
                deliver_to_others(msg, participant);
            } else {
                deliver_to_local(msg);
            }
            break;
        }
        case OpCode::MOVE_OP: {
            timer_cancelled_ = true;
            timer_.cancel();
            std::cout << "timer canceled" << std::endl;

            Position pos { data1 };
            try {
                milliseconds ms { stoull(data2) };
            } catch (std::exception& e) {
                // TODO:
            }

            // TODO: adjust time
            Player player, opponent;
            try {
                player = Player { contest.players.at(Role::NONE, participant) };
                opponent = Player { contest.players.at(-player.role) };
            } catch (std::exception& e) {
                logger->error("Ignore move: {}, playerlist: {}, try to find participant {}",
                    e.what(), contest.players.to_string(), participant->to_string());
                break;
            }

            try {
                contest.play(player, pos);
            } catch (Contest::StatusError& e) {
                logger->error("Ignore move: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
                break;
            } catch (std::exception& e) {
                logger->error("Ignore move: {}, player:{}", e.what(), player.to_string());
                break;
            }

            deliver_to_others(msg, participant); // broadcast
            if (contest.result.winner == opponent.role) {
                opponent.participant->deliver({ OpCode::WIN_PENDING_OP, std::to_string(std::to_underlying(Contest::WinType::SUICIDE)) });
                participant->deliver({ OpCode::SUICIDE_END_OP });
            } else if (contest.result.winner == player.role) {
                participant->deliver({ OpCode::GIVEUP_OP });
                opponent.participant->deliver({ OpCode::GIVEUP_END_OP }); // radical
            }

            if (contest.status == Contest::Status::ON_GOING) {
                contest.duration = TIMEOUT;
                timer_cancelled_ = false;
                timer_.expires_after(contest.duration);
                timer_.async_wait([this, opponent, participant](const asio::error_code& ec) {
                    if (!ec && !timer_cancelled_) {
                        contest.timeout(opponent);
                        participant->deliver({ OpCode::WIN_PENDING_OP, std::to_string(std::to_underlying(Contest::WinType::TIMEOUT)) });
                        opponent.participant->deliver({ OpCode::TIMEOUT_END_OP });
                        deliver_ui_state();
                    }
                });
            }

            deliver_ui_state();
            break;
        }
        case OpCode::GIVEUP_OP: {
            Role role { data2 };

            Player player, opponent;
            try {
                player = Player { contest.players.at(role, participant) };
                opponent = Player { contest.players.at(-player.role) };
            } catch (std::exception& e) {
                logger->error("Ignore give up: {}, playerlist: {}, try to find role {}, participant {}",
                    e.what(), contest.players.to_string(), role.to_string(), participant->to_string());
                break;
            }

            if (participant->is_local) {
                deliver_to_others(msg, participant); // broadcast
            }

            try {
                contest.concede(player);
            } catch (Contest::StatusError& e) {
                logger->error("Ignore concede: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
                break;
            } catch (std::logic_error& e) {
                logger->error("Concede: In {}'s turn, {}", contest.current.role.to_string(), e.what());
                break;
            }
            opponent.participant->deliver({ OpCode::WIN_PENDING_OP, std::to_string(std::to_underlying(Contest::WinType::GIVEUP)) });
            participant->deliver({ OpCode::GIVEUP_END_OP });
            timer_cancelled_ = true;
            timer_.cancel();

            deliver_ui_state();
            break;
        }
        // confirmation from the loser
        case OpCode::TIMEOUT_END_OP:
        case OpCode::SUICIDE_END_OP:
        case OpCode::GIVEUP_END_OP: {
            deliver_to_others(msg, participant); // broadcast
            break;
        }

        case OpCode::LEAVE_OP: {
            logger->debug("receive LEAVE_OP: is_local = {}", participant->is_local);
            if (participant->is_local) {
                logger->debug("receive LEAVE_OP: is local, do close_except");
                close_except(participant);
            } else {
                logger->debug("receive LEAVE_OP: not local, shutdown participant");
                participant->shutdown();
                logger->debug("receive LEAVE_OP: deliver_to_local");
                deliver_to_local(msg);
            }
            logger->debug("receive LEAVE_OP: process end");

            contest.clear();
            break;
        }
        case OpCode::CHAT_OP: {
            recent_msgs_.push_back(msg);
            while (recent_msgs_.size() > max_recent_msgs)
                recent_msgs_.pop_front();

            for (auto participant : participants_)
                participant->deliver(msg);
            break;
        }
        }
    }
    void join(Participant_ptr participant)
    {
        logger->info("{}:{} join", participant->endpoint().address().to_string(), participant->endpoint().port());
        participants_.insert(participant);
        // for (auto msg : recent_msgs_) {
        //     participant->deliver(msg);
        // }
    }

    void leave(Participant_ptr participant)
    {
        logger->info("{}:{} leave", participant->endpoint().address().to_string(), participant->endpoint().port());
        logger->debug("leave: erase participant, participants_.size() = {}", participants_.size());
        if (participants_.find(participant) != participants_.end())
            participants_.erase(participant);
        logger->debug("leave: erase end, participants_.size() = {}", participants_.size());
    }

    void close_except(Participant_ptr participant)
    {
        logger->debug("close_except: participants_.size() = {}", participants_.size());
        auto it = participants_.begin();
        while (it != participants_.end()) {
            auto p = *it;
            if (p != participant) {
                logger->debug("close_except: close {}:{}", p->endpoint().address().to_string(), std::to_string(p->endpoint().port()));
                logger->debug("close_except: send LEAVE_OP");
                p->deliver({ OpCode::LEAVE_OP });
                logger->debug("close_except: erase it");
                it = participants_.erase(it);
                // logger->debug("close_except: stop");
                // p->stop();
                // logger->debug("close_except: shutdown");
                // p->shutdown();
                logger->debug("close_except: end");
            } else {
                logger->debug("close_except: skip self");
                it++;
            }
        }
        logger->debug("close_except: end");
    }

    void deliver_to_others(Message msg, Participant_ptr participant)
    {
        std::cout << "deliver to others: self = " << participant->endpoint() << std::endl;
        recent_msgs_.push_back(msg);
        while (recent_msgs_.size() > max_recent_msgs)
            recent_msgs_.pop_front();

        for (auto p : participants_) {
            if (p != participant) {
                logger->info("broadcast {} from {}:{}", msg.to_string(), participant->endpoint().address().to_string(), participant->endpoint().port());
                p->deliver(msg);
            }
        }
    }

private:
    bool timer_cancelled_ {};
    asio::steady_timer timer_;
    asio::io_context& io_context_;

    std::set<Participant_ptr> participants_;
    enum { max_recent_msgs = 100 };
    std::deque<Message> recent_msgs_;
};

class Session : public Participant, public std::enable_shared_from_this<Session> {
public:
    std::string_view get_name() const override
    {
        return name_;
    }
    void set_name(std::string_view name) override
    {
        name_ = name;
    }
    tcp::endpoint endpoint() const override
    {
        return socket_.local_endpoint();
    }
    bool operator==(const Participant& participant) const override
    {
        return endpoint() == participant.endpoint();
    }
    Session(tcp::socket socket, Room& room, bool is_local = false)
        : Participant { is_local }
        , socket_(std::move(socket))
        , timer_(socket_.get_executor())
        , room_(room)
    {
        timer_.expires_at(std::chrono::steady_clock::time_point::max());
    }

    void start()
    {
        room_.join(shared_from_this());

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->reader(); }, detached);

        co_spawn(
            socket_.get_executor(), [self = shared_from_this()] { return self->writer(); }, detached);
    }

    void deliver(Message msg) override
    {
        logger->info("deliver: {} to {}", msg.to_string(), endpoint().address().to_string() + ":" + std::to_string(endpoint().port()));
        write_msgs_.push_back(msg);
        timer_.cancel_one();
    }

    void stop() override
    {
        logger->debug("stop: {}:{}", endpoint().address().to_string(), std::to_string(endpoint().port()));

        logger->debug("stop: leave room");
        room_.leave(shared_from_this());
        logger->debug("stop: close socket");
        socket_.close();
        logger->debug("stop: cancel timer");
        timer_.cancel();
        logger->debug("stop: end");
    }

    void shutdown() override
    {
        logger->debug("shutdown: {}:{}", endpoint().address().to_string(), std::to_string(endpoint().port()));
        asio::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        logger->debug("shutdown: end");
    }

private:
    awaitable<void> reader()
    {
        try {
            for (std::string read_msg;;) {
                std::size_t n = co_await asio::async_read_until(socket_, asio::dynamic_buffer(read_msg, 1024), "\n", use_awaitable);
                string temp = read_msg.substr(0, n);
                logger->info("Receive Message{}", temp);
                Message msg { read_msg.substr(0, n) };
                room_.process_data(msg, shared_from_this());

                read_msg.erase(0, n);
            }
        } catch (std::exception& e) {
            logger->error("Exception: {}", e.what());
            stop();
        }
    }

    awaitable<void> writer()
    {
        try {
            while (socket_.is_open()) {
                if (write_msgs_.empty()) {
                    asio::error_code ec;
                    co_await timer_.async_wait(redirect_error(use_awaitable, ec));
                } else {
                    auto msg = write_msgs_.front();
                    co_await asio::async_write(socket_, asio::buffer(msg.to_string() + "\n"),
                        use_awaitable);
                    write_msgs_.pop_front();
                    if (msg.op == OpCode::LEAVE_OP && !is_local)
                        shutdown();
                }
            }
        } catch (std::exception& e) {
            logger->error("Exception: {}", e.what());
            stop();
        }
    }

    std::string name_;
    tcp::socket socket_;
    asio::steady_timer timer_;
    Room& room_;
    std::deque<Message> write_msgs_;
};

void start_session(asio::io_context& io_context, Room& room, asio::error_code& ec, std::string_view ip_address, std::string_view port)
{
    tcp::socket socket { io_context };
    socket.connect(tcp::endpoint(asio::ip::make_address(ip_address), stoi(port)), ec);
    if (!ec)
        std::make_shared<Session>(std::move(socket), room, false)->start();
}

awaitable<void> listener(tcp::acceptor acceptor, Room& room, bool is_local = false)
{
    for (;;) {
        std::make_shared<Session>(co_await acceptor.async_accept(use_awaitable), room, is_local)->start();
        logger->info("new connection to {}:{}", acceptor.local_endpoint().address().to_string(), acceptor.local_endpoint().port());
    }
}

_EXPORT void launch_server(std::vector<asio::ip::port_type> ports)
{
    try {
        asio::io_context io_context(1);
        Room room { io_context };

        tcp::endpoint local { tcp::v4(), ports[0] };
        co_spawn(io_context, listener(tcp::acceptor(io_context, local), room, true), detached);
        logger->info("Serving on {}:{}", local.address().to_string(), local.port());
        for (auto port : ports | std::views::drop(1)) {
            tcp::endpoint ep { tcp::v4(), port };
            co_spawn(io_context, listener(tcp::acceptor(io_context, ep), room), detached);
            logger->info("Serving on {}:{}", ep.address().to_string(), ep.port());
        }

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) { io_context.stop(); });

        io_context.run();
    } catch (std::exception& e) {
        logger->error("Exception: {}", e.what());
    }
}