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
#include <asio/streambuf.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "contest.hpp"
#include "log.hpp"
#include "message.hpp"
#include "uimessage.hpp"
#include "utility.hpp"

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

using std::operator""sv;

constexpr auto TIMEOUT { 30s };

class Room;

void start_session(asio::io_context&, Room&, asio::error_code&, std::string_view, std::string_view);

struct ContestRequest {
    Participant_ptr sender;
    Participant_ptr receiver;
    Role role;
    system_clock::time_point time;

    ContestRequest(Participant_ptr sender, Participant_ptr receiver, Role role, system_clock::time_point time = system_clock::now())
        : sender { sender }
        , receiver { receiver }
        , role { role }
        , time { time }
    {
    }
};

class Room {
    Contest contest;
    std::deque<std::string> chats;
    std::optional<ContestRequest> my_request;
    std::queue<ContestRequest> received_requests;

    Participant_ptr find_local_participant()
    {
        if (auto p = ranges::find_if(participants_, [](auto participant) { return participant->is_local; }); p != participants_.end())
            return *p;
        throw std::logic_error("no local participant");
    }

    void deliver_to_local(Message msg)
    {
        find_local_participant()->deliver(msg);
    }

    void deliver_ui_state()
    {
        deliver_to_local(UiMessage { contest });
    }

    auto receive_participant_name(Participant_ptr participant, std::string_view name)
    {
        auto new_name { name };

        if (!Player::is_valid_name(new_name)) {
            if (participant->get_name().empty())
                new_name = "Player" + std::to_string(contest.players.size() + 1);
            else
                new_name = participant->get_name();
        }

        if (new_name != participant->get_name()) {
            if (!participant->is_local && !participant->get_name().empty() && new_name != participant->get_name())
                deliver_to_local({ OpCode::CHAT_USERNAME_UPDATE_OP, participant->get_name(), new_name });
            participant->set_name(new_name);
        }

        return new_name;
    }

    void receive_new_request(const ContestRequest& request)
    {
        if (received_requests.empty()) {
            deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, request.sender->get_name(), request.role.map("b", "w", "") });
        }
        received_requests.push(request);
        deliver_ui_state();
    }

    void enroll_players(ContestRequest& request)
    {
        contest.set_board_size(9);
        Player player1 { request.sender, request.sender->get_name(), request.role, request.sender->is_local ? PlayerType::LOCAL_HUMAN_PLAYER : PlayerType::REMOTE_HUMAN_PLAYER },
            player2 { request.receiver, request.receiver->get_name(), -request.role, request.receiver->is_local ? PlayerType::LOCAL_HUMAN_PLAYER : PlayerType::REMOTE_HUMAN_PLAYER };
        contest.enroll(std::move(player1)), contest.enroll(std::move(player2));
        contest.local_role = request.sender->is_local ? request.role : -request.role;
        contest.duration = TIMEOUT;
    }

    void reject_all_received_requests()
    {
        while (!received_requests.empty()) {
            auto r = received_requests.front();
            received_requests.pop();
            r.sender->deliver({ OpCode::REJECT_OP, r.receiver->get_name() });
        }
    }

    void check_online_contest_result()
    {
        if (contest.status == Contest::Status::GAME_OVER) {
            auto winner { contest.players.at(contest.result.winner) };
            auto loser { contest.players.at(-winner.role) };

            auto gg_op {
                contest.result.win_type == Contest::WinType::GIVEUP        ? OpCode::GIVEUP_END_OP
                    : contest.result.win_type == Contest::WinType::TIMEOUT ? OpCode::TIMEOUT_END_OP
                                                                           : OpCode::SUICIDE_END_OP
            };
            if (winner.participant->is_local) {
                winner.participant->deliver({ OpCode::WIN_PENDING_OP, std::to_string(std::to_underlying(contest.result.win_type)) });
                loser.participant->deliver({ gg_op });
            } else {
                // do nothing, waiting for GG_OP to confirm
            }
        }
    }

public:
    Room(asio::io_context& io_context)
        : timer_ { io_context }
        , io_context_ { io_context }
        , my_request { std::nullopt }
    {
    }
    void process_data(Message msg, Participant_ptr participant)
    {
        logger->info("process_data: {} from {}", msg.to_string(), participant->to_string());
        const string_view data1 { msg.data1 }, data2 { msg.data2 };

        switch (msg.op) {
        case OpCode::REPLAY_START_MOVE_OP: {
            // data1: current moves
            if (contest.status == Contest::Status::ON_GOING) {
                throw std::logic_error("contest already started");
            }
            contest.clear();
            Player player1 { participant, "BLACK", Role::BLACK, PlayerType::LOCAL_HUMAN_PLAYER },
                player2 { participant, "WHITE", Role::WHITE, PlayerType::LOCAL_HUMAN_PLAYER };
            contest.enroll(std::move(player1)), contest.enroll(std::move(player2));
            contest.local_role = Role::BLACK;
            contest.is_replaying = true;

            auto tmp {
                data1 | ranges::views::split(" "sv)
                | ranges::to<std::vector<std::string>>()
            };

            auto moves {
                tmp | ranges::views::transform([](auto&& s) { return Position { s }; })
                | ranges::to<std::vector<Position>>()
            };

            ranges::for_each(moves, [&](auto&& pos) {
                Role role { contest.moves.size() % 2 == 0 ? Role::BLACK : Role::WHITE };
                auto player { contest.players.at(role) };
                contest.play(player, pos);
            });

            deliver_ui_state();
            break;
        }
        case OpCode::REPLAY_MOVE_OP: {
            Position pos { data1 };
            Role role { contest.moves.size() % 2 == 0 ? Role::BLACK : Role::WHITE };
            auto player { contest.players.at(role) };
            contest.play(player, pos);
            deliver_ui_state();
            break;
        }
        case OpCode::REPLAY_STOP_MOVE_OP: {
            contest.clear();
            deliver_ui_state();
            break;
        }
        case OpCode::WIN_PENDING_OP: {
            break;
        }
        case OpCode::CHAT_USERNAME_UPDATE_OP: {
            break;
        }
        case OpCode::RECEIVE_REQUEST_RESULT_OP: {
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
            int timeout = stoi(data1);
            int size = stoi(data2);

            seconds duration { timeout };
            contest.duration = duration;
            contest.set_board_size(size);

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
                player = contest.players.at(role, participant);
                opponent = contest.players.at(-player.role);
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

        case OpCode::UPDATE_USERNAME_OP: {
            if (!participant->is_local) {
                throw std::logic_error { "remote participant should not send UPDATE_USERNAME_OP" };
            }
            receive_participant_name(participant, data1);
            break;
        }

        case OpCode::SEND_REQUEST_OP: {
            // data1 is host:port, data2 is role
            auto host { data1.substr(0, data1.find(':')) };
            auto port { data1.substr(data1.find(':') + 1) };
            auto ep = tcp::endpoint { asio::ip::make_address(host), integer_cast<asio::ip::port_type>(port) };
            Role role { data2 };

            auto participants = participants_ | std::views::filter([ep](auto p) { return p->endpoint() == ep; })
                | ranges::to<std::vector>();

            if (participants.size() != 1) {
                throw std::logic_error { "participants.size() != 1" };
            }

            auto receiver { participants[0] };
            ContestRequest request { participant, receiver, role };
            my_request = request;
            receiver->deliver({ OpCode::READY_OP, participant->get_name(), data2 });
            break;
        }
        case OpCode::SEND_REQUEST_BY_USERNAME_OP: {
            // data1 is username, data2 is role
            auto participants = participants_ | std::views::filter([data1](auto p) { return p->get_name() == data1; })
                | ranges::to<std::vector>();

            if (participants.size() != 1) {
                throw std::logic_error { "participants.size() != 1" };
            }

            auto receiver { participants[0] };
            ContestRequest request { participant, receiver, Role { data2 } };
            my_request = request;
            receiver->deliver({ OpCode::READY_OP, participant->get_name(), data2 });
            break;
        }
        case OpCode::RECEIVE_REQUEST_OP: {
            // should not be sent by client
            break;
        }
        case OpCode::ACCEPT_REQUEST_OP: {
            if (received_requests.empty()) {
                throw std::logic_error { "received_requests.empty()" };
            }
            auto request = received_requests.front();
            received_requests.pop();
            reject_all_received_requests();
            request.sender->deliver({ OpCode::READY_OP, request.receiver->get_name(), (-request.role).map("b", "w", "") });
            enroll_players(request);
            deliver_ui_state();
            break;
        }
        case OpCode::REJECT_REQUEST_OP: {
            if (received_requests.empty()) {
                throw std::logic_error { "received_requests.empty()" };
            }
            auto request = received_requests.front();
            received_requests.pop();
            request.sender->deliver({ OpCode::REJECT_OP, request.receiver->get_name() });
            if (!received_requests.empty()) {
                auto next_request = received_requests.front();
                next_request.receiver->deliver({ OpCode::RECEIVE_REQUEST_OP, next_request.sender->get_name(), next_request.role.map("b", "w", "") });
            }
            break;
        }

        case OpCode::READY_OP: {
            logger->info("ready: is_local = {}, data1 = {}, data2 = {}", participant->is_local, data1, data2);

            if (contest.status == Contest::Status::GAME_OVER) {
                contest.clear();
            }

            // TODO: warn if invalid name
            auto name { receive_participant_name(participant, data1) };
            Role role { data2 };

            if (participant->is_local) {
                // READY_OP should not be sent by local
                throw std::logic_error("READY_OP should not be sent by local");
            } else {
                if (my_request.has_value() && participant == my_request->receiver) {
                    deliver_to_local({ OpCode::RECEIVE_REQUEST_RESULT_OP, "accepted", name });
                    // contest accepted, enroll players
                    enroll_players(my_request.value());
                    // TODO: catch exceptions when enrolling players
                    my_request = std::nullopt;
                    reject_all_received_requests();
                } else {
                    // receive request
                    receive_new_request({ participant, find_local_participant(), role });
                }
            }
            deliver_ui_state();
            break;
        }
        case OpCode::REJECT_OP: {
            contest.reject();

            auto name { receive_participant_name(participant, data1) };

            if (participant->is_local) {
                // REJECT_OP should not be sent by local
                throw std::logic_error("REJECT_OP should not be sent by local");
            } else {
                if (my_request.has_value() && participant == my_request->receiver) {
                    deliver_to_local({ OpCode::RECEIVE_REQUEST_RESULT_OP, "rejected", name });
                    my_request = std::nullopt;
                }
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
            check_online_contest_result();

            if (contest.status == Contest::Status::ON_GOING) {
                contest.duration = TIMEOUT;
                timer_cancelled_ = false;
                timer_.expires_after(contest.duration);
                timer_.async_wait([this, opponent, participant](const asio::error_code& ec) {
                    if (!ec && !timer_cancelled_) {
                        contest.timeout(opponent);
                        check_online_contest_result();
                        deliver_ui_state();
                    }
                });
            }

            deliver_ui_state();
            break;
        }
        case OpCode::GIVEUP_OP: {
            // data1(role)
            // TODO: data2(greeting)
            Role role { data1 };
            Player player, opponent;
            try {
                player = contest.players.at(role, participant);
                opponent = contest.players.at(-player.role);
            } catch (std::exception& e) {
                logger->error("Ignore give up: {}, playerlist: {}, try to find participant {}",
                    e.what(), contest.players.to_string(), participant->to_string());
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
            timer_cancelled_ = true;
            timer_.cancel();

            check_online_contest_result();
            deliver_ui_state();
            break;
        }
        // confirmation from the loser
        case OpCode::TIMEOUT_END_OP:
        case OpCode::SUICIDE_END_OP:
        case OpCode::GIVEUP_END_OP: {
            if (contest.result.confirmed)
                break;
            if (!participant->is_local) {
                auto player { contest.players.at(Role::NONE, participant) };
                auto opponent { contest.players.at(-player.role) };
                if (contest.result.winner == player.role) {
                    auto gg_op { msg.op };
                    auto claimed_win_type {
                        gg_op == OpCode::GIVEUP_END_OP        ? Contest::WinType::GIVEUP
                            : gg_op == OpCode::TIMEOUT_END_OP ? Contest::WinType::TIMEOUT
                                                              : Contest::WinType::SUICIDE
                    };
                    auto result_valid { claimed_win_type == contest.result.win_type };
                    // Use lenient validation for timeout
                    if (claimed_win_type == Contest::WinType::TIMEOUT && !result_valid) {
                        auto remain_time { std::chrono::duration_cast<milliseconds>(timer_.expiry() - std::chrono::steady_clock::now()) };
                        // 270ms is the median human reaction time (reference: https://humanbenchmark.com/tests/reactiontime/statistics)
                        if (remain_time < 270ms) {
                            result_valid = true;
                        }
                    }
                    if (result_valid) {
                        contest.confirm();
                        // reply same GG_OP to confirm
                        participant->deliver(msg);
                    } else {
                        // result is not valid, do nothing
                    }
                } else if (contest.result.winner == opponent.role) {
                    contest.confirm();
                }

                deliver_ui_state();
            }
            break;
        }

        case OpCode::LEAVE_OP: {
            logger->debug("receive LEAVE_OP: is_local = {}", participant->is_local);
            if (participant->is_local) {
                logger->debug("receive LEAVE_OP: is local, do close_except");
                close_except(participant);
            } else {
                logger->debug("receive LEAVE_OP: not local, deliver LEVEL_OP");
                participant->deliver({ OpCode::LEAVE_OP });
            }
            logger->debug("receive LEAVE_OP: process end");

            contest.clear();
            break;
        }
        case OpCode::CHAT_OP: {
            recent_msgs_.push_back(msg);
            while (recent_msgs_.size() > max_recent_msgs)
                recent_msgs_.pop_front();

            if (participant->is_local) {
                throw std::logic_error("CHAT_OP should not be sent by local");
            } else {
                auto name = participant->get_name();
                if (name.empty()) {
                    name = participant->endpoint().address().to_string();
                }
                deliver_to_local({ OpCode::CHAT_RECEIVE_MESSAGE_OP, data1, name });
            }
            break;
        }
        case OpCode::CHAT_SEND_MESSAGE_OP: {
            if (participant->is_local) {
                auto success = false;
                for (auto participant : participants_) {
                    if (!participant->get_name().empty() && participant->get_name() == data2) {
                        participant->deliver({ OpCode::CHAT_OP, data1 });
                        success = true;
                    } else if (participant->get_name().empty() && participant->endpoint().address().to_string() == data2) {
                        participant->deliver({ OpCode::CHAT_OP, data1 });
                        success = true;
                    }
                }
            } else {
                throw std::logic_error("CHAT_SEND_MESSAGE_OP should not be sent by remote");
            }
            break;
        }
        case OpCode::CHAT_SEND_BROADCAST_MESSAGE_OP: {
            if (participant->is_local) {
                deliver_to_others({ OpCode::CHAT_OP, data1 }, participant);
            } else {
                throw std::logic_error("CHAT_SEND_BROADCAST_MESSAGE_OP should not be sent by remote");
            }
            break;
        }
        case OpCode::CHAT_RECEIVE_MESSAGE_OP: {
            // should not be sent by client
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
        logger->info("leave: {}:{} leave", participant->endpoint().address().to_string(), participant->endpoint().port());
        if (participants_.find(participant) == participants_.end()) {
            logger->info("leave: {}:{} not found", participant->endpoint().address().to_string(), participant->endpoint().port());
            return;
        }
        logger->debug("leave: erase participant, participants_.size() = {}", participants_.size());
        if (participants_.find(participant) != participants_.end())
            participants_.erase(participant);
        logger->debug("leave: erase end, participants_.size() = {}", participants_.size());
        logger->debug("leave: remove all requests from {}:{} in received_requests", participant->endpoint().address().to_string(), participant->endpoint().port());
        std::queue<ContestRequest> requests {};
        requests.swap(received_requests);
        auto is_first { !requests.empty() && requests.front().sender == participant };
        while (!requests.empty()) {
            auto request = requests.front();
            requests.pop();
            if (request.sender != participant) {
                received_requests.push(request);
            }
        }
        if (is_first && !received_requests.empty()) {
            logger->debug("leave: is_first && !received_requests.empty(), send received_requests.front() to local");
            deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, received_requests.front().sender->get_name(), received_requests.front().role.map("b", "w", "") });
        }
        if (participant == my_request->receiver) {
            logger->debug("leave: my_request->receiver == participant, clear my_request");
            my_request = std::nullopt;
        }
        if (!participant->get_name().empty()) {
            logger->debug("leave: participant->get_name() is not empty, send LEAVE_OP to local");
            deliver_to_local({ OpCode::LEAVE_OP, participant->get_name() });
        }
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
        return is_local ? socket_.local_endpoint() : socket_.remote_endpoint();
    }
    bool operator==(const Participant& participant) const override
    {
        // TODO: Use a better comparsion
        if (!get_name().empty() || !participant.get_name().empty())
            return get_name() == participant.get_name();
        return endpoint() == participant.endpoint();
    }
    Session(tcp::socket socket, Room& room, bool is_local = false)
        : Participant { is_local }
        , name_("")
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

private:
    void shutdown()
    {
        logger->debug("shutdown: {}:{}", endpoint().address().to_string(), std::to_string(endpoint().port()));
        asio::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        logger->debug("shutdown: end");
    }

    awaitable<void> reader()
    {
        asio::streambuf buffer;
        std::istream stream(&buffer);
        try {
            for (std::string message;;) {
                co_await asio::async_read_until(socket_, buffer, '\n', use_awaitable);
                std::getline(stream, message);
                logger->info("Receive Message{}", message);
                room_.process_data(Message { message }, shared_from_this());
            }
        } catch (std::exception& e) {
            logger->error("Exception: {}", e.what());
            if (!is_local)
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
                    if (msg.op == OpCode::LEAVE_OP && !is_local) {
                        room_.leave(shared_from_this());
                        shutdown();
                    }
                }
            }
        } catch (std::exception& e) {
            logger->error("Exception: {}", e.what());
            if (!is_local)
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