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
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "bot.hpp"
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

static auto TIMEOUT { 30s };

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
    std::deque<ContestRequest> received_requests;
    std::mutex bot_mutex;

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

    constexpr auto is_local_contest() -> bool
    {
        constexpr std::array<Role, 2> roles { Role::BLACK, Role::WHITE };
        return ranges::all_of(roles, [&](auto role) { return contest.players.at(role).participant->is_local; });
    }

    void toggle_bot_hosting(Role role, Participant_ptr participant = nullptr, bool is_local_game = false)
    {
        Player& player { contest.players.at(role, participant) };
        if (player.type == PlayerType::REMOTE_HUMAN_PLAYER)
            return;
        if (player.type == PlayerType::BOT_PLAYER) {
            player.type = PlayerType::LOCAL_HUMAN_PLAYER;
        } else {
            player.type = PlayerType::BOT_PLAYER;
            check_bot(participant, role, is_local_game);
        }
    }

    auto do_move(Participant_ptr participant, Position pos, Role role = Role::NONE, bool is_local_game = false)
    {
        logger->debug("do_move: pos = {}, role = {}, is_local_game = {}", pos.to_string(), role.map("b", "w", "-"), std::to_string(is_local_game));
        timer_cancelled_ = true;
        timer_.cancel();

        Player player, opponent;
        try {
            player = Player { contest.players.at(role, participant) };
            opponent = Player { contest.players.at(-player.role) };
        } catch (std::exception& e) {
            logger->error("Ignore move: {}, playerlist: {}, cannot find player {}",
                e.what(), contest.players.to_string(), role.map("b", "w", "-"));
            return false;
        }

        try {
            contest.play(player, pos);
        } catch (Contest::StatusError& e) {
            logger->error("Ignore move: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
            return false;
        } catch (std::exception& e) {
            logger->error("Ignore move: {}, player:{}", e.what(), player.to_string());
            return false;
        }

        if (!is_local_game)
            check_online_contest_result();

        if (contest.status == Contest::Status::ON_GOING) {
            timer_cancelled_ = false;
            timer_.expires_after(contest.duration);
            timer_.async_wait([=](const asio::error_code& ec) {
                if (!ec && !timer_cancelled_) {
                    logger->debug("timeout: role = {}", role.map("b", "w", "-"));
                    contest.timeout(opponent);
                    if (!is_local_game)
                        check_online_contest_result();
                    deliver_ui_state();
                }
            });
        }

        deliver_ui_state();

        check_bot(opponent.participant, opponent.role, is_local_game);
        return true;
    }

    auto should_bot_move(Participant_ptr participant, Role role = Role::NONE)
    {
        if (!participant->is_local || contest.status != Contest::Status::ON_GOING)
            return false;

        auto player { contest.players.at(role, participant) };

        return player.type == PlayerType::BOT_PLAYER && contest.current.role == player.role;
    }

    void check_bot(Participant_ptr participant, Role role = Role::NONE, bool is_local_game = false)
    {
        logger->debug("check_bot: participant = {}, role = {}, is_local_game = {}", participant != nullptr ? participant->to_string() : "null", role.map("b", "w", "-"), std::to_string(is_local_game));

        if (!should_bot_move(participant, role))
            return;

        auto player { contest.players.at(role, participant) };

        logger->info("check_bot: start bot");
        auto bot = [&](const State& state, Participant_ptr participant, const Role& role = Role::NONE, bool is_local_game = false) {
            std::lock_guard<std::mutex> guard(bot_mutex);
            logger->info("bot start calcing move, role = {}", role.map("b", "w", "-"));
            auto pos = mcts_bot_player(state);
            if (pos) {
                logger->info("bot finish calcing move, role = {}, pos = {}", role.map("b", "w", "-"), pos.to_string());
                if (should_bot_move(participant, role)) {
                    if (do_move(participant, pos, role, is_local_game)) {
                        deliver_to_others({ OpCode::MOVE_OP, pos.to_string() }, participant);
                    }
                }
            } else {
                logger->error("bot failed to calc move, role = {}", role.map("b", "w", "-"));
            }
        };
        std::thread bot_thread { bot, std::ref(contest.current), participant, player.role, is_local_game };
        bot_thread.detach();
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
        logger->debug("receive_new_request: sender = {}, receiver = {}, role = {}", request.sender->to_string(), request.receiver->to_string(), request.role.map("b", "w", "-"));

        if (received_requests.empty()) {
            deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, request.sender->get_name(), request.role.map("b", "w", "") });
        }
        received_requests.push_back(request);
    }

    void enroll_players(ContestRequest& request)
    {
        logger->debug("enroll_players: sender = {}, receiver = {}, role = {}", request.sender->to_string(), request.receiver->to_string(), request.role.map("b", "w", "-"));

        contest.clear();
        contest.set_board_size(9);
        contest.duration = TIMEOUT;
        Player player1 { request.sender, request.sender->get_name(), request.role, request.sender->is_local ? PlayerType::LOCAL_HUMAN_PLAYER : PlayerType::REMOTE_HUMAN_PLAYER },
            player2 { request.receiver, request.receiver->get_name(), -request.role, request.receiver->is_local ? PlayerType::LOCAL_HUMAN_PLAYER : PlayerType::REMOTE_HUMAN_PLAYER };
        contest.enroll(std::move(player1)), contest.enroll(std::move(player2));
        contest.local_role = request.sender->is_local ? request.role : -request.role;
    }

    void reject_all_received_requests(Participant_ptr participant = nullptr)
    {
        std::string_view name { participant == nullptr ? "" : participant->get_name() };
        logger->debug("reject_all_received_requests");

        ranges::for_each(received_requests, [=](auto& r) { if (name != r.sender->get_name()) r.sender->deliver({ OpCode::REJECT_OP, r.receiver->get_name(), "Already accepted other request" }); });
        received_requests.clear();
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
        case OpCode::BOT_HOSTING_OP: {
            Role role { data1 };
            toggle_bot_hosting(role, participant, is_local_contest());
            deliver_ui_state();
            break;
        }
        case OpCode::REPLAY_START_MOVE_OP: {
            // data1: current moves
            // data2: board size
            if (contest.status == Contest::Status::ON_GOING) {
                throw std::logic_error("contest already started");
            }
            auto size { stoi(data2) };
            contest.clear();
            contest.set_board_size(size);
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
            // data1 = timeout|type, data2 = size
            auto tmp = data1 | ranges::views::split("|"sv) | ranges::to<std::vector<std::string>>();
            if (tmp.size() != 2) {
                throw std::logic_error("invalid data1");
            }
            logger->info("start local game: timeout = {}, type = {}, size = {}", tmp[0], tmp[1], data2);
            if (contest.status != Contest::Status::NOT_PREPARED) {
                contest.clear();
            }
            int timeout = stoi(tmp[0]);
            int type = stoi(tmp[1]);
            int size = stoi(data2);

            seconds duration { timeout };
            contest.duration = duration;
            contest.set_board_size(size);

            Player player1 { participant, "Black", Role::BLACK, (type == 2 || type == 3 ? PlayerType::BOT_PLAYER : PlayerType::LOCAL_HUMAN_PLAYER) },
                player2 { participant, "White", Role::WHITE, (type == 1 || type == 3 ? PlayerType::BOT_PLAYER : PlayerType::LOCAL_HUMAN_PLAYER) };
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
            check_bot(participant, Role::BLACK, true);
            break;
        }
        case OpCode::LOCAL_GAME_TIMEOUT_OP: {
            // Deprecated
            break;
        }
        case OpCode::LOCAL_GAME_MOVE_OP: {
            Position pos { data1 };
            Role role { data2 };

            do_move(participant, pos, role, true);
            break;
        }

        case OpCode::SYNC_ONLINE_SETTINGS_OP: {
            if (!participant->is_local) {
                throw std::logic_error { "remote participant should not send SYNC_ONLINE_SETTINGS_OP" };
            }
            receive_participant_name(participant, data1);
            std::chrono::seconds duration { stoi(data2) };
            TIMEOUT = duration;
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
            logger->info("accept request");
            auto request = received_requests.front();
            received_requests.pop_front();
            reject_all_received_requests(request.sender);
            request.sender->deliver({ OpCode::READY_OP, request.receiver->get_name(), "" });
            enroll_players(request);
            deliver_ui_state();
            break;
        }
        case OpCode::REJECT_REQUEST_OP: {
            if (received_requests.empty()) {
                throw std::logic_error { "received_requests.empty()" };
            }
            auto request = received_requests.front();
            received_requests.pop_front();
            request.sender->deliver({ OpCode::REJECT_OP, request.receiver->get_name() });
            if (!received_requests.empty()) {
                auto next_request = received_requests.front();
                next_request.receiver->deliver({ OpCode::RECEIVE_REQUEST_OP, next_request.sender->get_name(), next_request.role.map("b", "w", "") });
            }
            break;
        }

        case OpCode::READY_OP: {
            logger->info("ready: is_local = {}, data1 = {}, data2 = {}", participant->is_local, data1, data2);

            // TODO: warn if invalid name
            auto name { receive_participant_name(participant, data1) }; // role is NONE if receive request reply
            Role role { data2 };

            if (participant->is_local) {
                // READY_OP should not be sent by local
                throw std::logic_error("READY_OP should not be sent by local");
            } else {
                // receive request reply
                if (contest.status == Contest::Status::ON_GOING) {
                    return;
                }
                if (my_request.has_value() && participant == my_request->receiver) {
                    deliver_to_local({ OpCode::RECEIVE_REQUEST_RESULT_OP, "accepted", name });
                    // contest accepted, enroll players
                    enroll_players(my_request.value());
                    reject_all_received_requests(my_request->receiver);
                    // TODO: catch exceptions when enrolling players
                    my_request = std::nullopt;
                } else {
                    // receive request
                    if (contest.status == Contest::Status::ON_GOING) {
                        participant->deliver({ OpCode::REJECT_OP, find_local_participant()->get_name(), "Contest already started" });
                        return;
                    }
                    receive_new_request({ participant, find_local_participant(), role });
                }
                deliver_ui_state();
                break;
            }
        }
        case OpCode::REJECT_OP: {
            auto name { receive_participant_name(participant, data1) };

            if (!my_request.has_value() || my_request->receiver != participant)
                return;

            contest.reject();

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
            Position pos { data1 };

            if (do_move(participant, pos)) {
                deliver_to_others(msg, participant); // broadcast
            }
            break;
        }
        case OpCode::GIVEUP_OP: {
            // data1: role(local) / username(online)
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

            if (contest.players.contains(Role::NONE, participant))
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
        auto is_first { !received_requests.empty() && received_requests.front().sender == participant };
        std::erase_if(received_requests, [&](auto request) { return request.sender == participant; });
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