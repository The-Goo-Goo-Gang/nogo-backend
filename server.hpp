#pragma once

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/streambuf.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <magic_enum.hpp>

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

class Participant : public std::enable_shared_from_this<Participant> {
public:
    Player player;
    Room& room;
    bool is_local;

    tcp::socket socket;
    asio::steady_timer timer;
    std::deque<Message> write_msgs;

    awaitable<void> reader();
    awaitable<void> writer();

public:
    std::string name;

    Participant(tcp::socket socket, Room& room, string name)
        : room(room)
        , socket(std::move(socket))
        , timer(socket.get_executor())
        , name(name)
    {
        timer.expires_at(std::chrono::steady_clock::time_point::max());
    }
    virtual ~Participant()
    {
    }
    bool operator==(const Participant& participant) const
    {
        // TODO: Use a better comparsion
        if (!name.empty() || !participant.name.empty())
            return name == participant.name;
        return endpoint() == participant.endpoint();
    }
    void process_game_over();

    friend std::ostream& operator<<(std::ostream& os, const Participant& participant)
    {
        return os << participant.endpoint();
    }
    virtual tcp::endpoint endpoint() const = 0;

    void start();
    void stop();
    void deliver(const Message& msg)
    {
        logger->info("deliver: {} to {}", msg.to_string(), ::to_string(endpoint()));
        write_msgs.push_back(msg);
        timer.cancel_one();
    }
    void shutdown()
    {
        logger->debug("shutdown: {}", ::to_string(endpoint()));
        asio::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
    }

    virtual void ready(string_view, string_view) = 0;
    virtual void reject(string_view, string_view) = 0;
    void move(string_view, string_view);
    virtual void giveup(string_view, string_view) = 0;
    virtual void giveup_end(string_view, string_view) = 0;
    virtual void timeout_end(string_view, string_view) = 0;
    virtual void suicide_end(string_view, string_view) = 0;
    virtual void leave(string_view, string_view) = 0;
    virtual void chat(string_view, string_view) = 0;

    virtual void start_local_game(string_view, string_view) = 0;
    void update_ui_state(string_view, string_view)
    {
        throw std::logic_error { "UPDATE_UI_STATE_OP is deprecated" };
    }
    void local_game_timeout(string_view, string_view)
    {
        throw std::logic_error { "LOCAL_GAME_TIMEOUT_OP is deprecated" };
    }

    virtual void local_game_move(string_view, string_view) = 0;
    virtual void connect_to_remote(string_view, string_view) = 0;
    void connect_result(string_view, string_view)
    {
        throw std::logic_error { "Participant should not send connect_result" };
    }
    void win_pending(string_view, string_view)
    {
        throw std::logic_error { "Participant should not send win_pending" };
    }

    virtual void chat_send_message(string_view, string_view) = 0;
    virtual void chat_send_broadcast_message(string_view, string_view) = 0;
    void chat_receive_message(string_view, string_view)
    {
        throw std::logic_error { "Participant should not send chat_receive_message" };
    }
    virtual void chat_username_update(string_view, string_view) = 0;

    virtual void sync_online_settings(string_view, string_view) = 0;
    virtual void send_request_by_ip(string_view, string_view) = 0;
    virtual void send_request_by_username(string_view, string_view) = 0;
    virtual void receive_request(string_view, string_view) = 0;
    virtual void accept_request(string_view, string_view) = 0;
    virtual void reject_request(string_view, string_view) = 0;
    void receive_request_result(string_view, string_view)
    {
        throw std::logic_error { "Participant should not send receive_request_result" };
    }

    virtual void replay_start_move(string_view, string_view) = 0;
    virtual void replay_move(string_view, string_view) = 0;
    virtual void replay_stop_move(string_view, string_view) = 0;

    virtual void bot_hosting(string_view, string_view) = 0;
};

template <>
struct fmt::formatter<Participant> : fmt::formatter<std::string> {
    auto format(const Participant& participant, format_context& ctx)
    {
        return fmt::format_to(ctx.out(), "[Participant {} with {}]", ::to_string(participant.endpoint()), participant.player);
    }
};

_EXPORT using Participant_ptr = std::shared_ptr<Participant>;

class Room {
public:
    Contest contest;
    std::deque<std::string> chats;
    Participant_ptr my_request;
    std::deque<Participant_ptr> received_requests;
    std::mutex bot_mutex;

    Participant_ptr find_local_participant()
    {
        auto p = ranges::find_if(participants, [](auto participant) { return participant->is_local; });
        if (p == participants.end())
            throw std::logic_error("no local participant");
        return *p;
    }

    void deliver_to_local(const Message& msg)
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

    void toggle_bot_hosting(Player& player, bool is_local_game = false)
    {
        if (player.type == PlayerType::REMOTE_HUMAN_PLAYER)
            return;
        if (player.type == PlayerType::BOT_PLAYER) {
            player.type = PlayerType::LOCAL_HUMAN_PLAYER;
        } else {
            player.type = PlayerType::BOT_PLAYER;
            check_bot(player, is_local_game);
        }
    }

    auto do_move(const Player& player, Position pos, bool is_local_game = false)
    {
        logger->debug("do_move: player = {}, pos = {}, is_local_game = {}", player.to_string(), pos.to_string(), std::to_string(is_local_game));
        timer_cancelled = true;
        timer.cancel();

        Player opponent;
        try {
            opponent = Player { contest.players.at(-player.role) };
        } catch (std::exception& e) {
            logger->error("Ignore move: {}, playerlist: {}, cannot find player {}",
                e.what(), contest.players.to_string(), player.to_string());
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

        if (!is_local_game) {
            if (contest.status == Contest::Status::GAME_OVER) {
                player.participant->process_game_over();
            }
        }

        if (contest.status == Contest::Status::ON_GOING) {
            timer_cancelled = false;
            timer.expires_after(contest.duration);
            timer.async_wait([=](const asio::error_code& ec) {
                if (!ec && !timer_cancelled) {
                    logger->debug("timeout: player = {}", player.to_string());
                    contest.timeout(opponent);
                    if (!is_local_game) {
                        if (contest.status == Contest::Status::GAME_OVER) {
                            player.participant->process_game_over();
                        }
                    }
                    deliver_ui_state();
                }
            });
        }

        deliver_ui_state();

        check_bot(opponent, is_local_game);
        return true;
    }

    auto should_bot_move(const Player& player) const
    {
        auto participant { player.participant };

        if (!participant->is_local || contest.status != Contest::Status::ON_GOING)
            return false;

        return player.type == PlayerType::BOT_PLAYER && contest.current.role == player.role;
    }

    void check_bot(const Player& player, bool is_local_game = false)
    {
        logger->debug("check_bot: player = {}, is_local_game = {}", player.to_string(), std::to_string(is_local_game));

        if (!should_bot_move(player))
            return;

        logger->info("check_bot: start bot");
        auto bot = [&](const State& state, Player player, bool is_local_game = false) {
            std::lock_guard<std::mutex> guard(bot_mutex);
            logger->info("bot start calcing move, player = {}", player.to_string());
            Position pos = mcts_bot_player(state);
            if (pos) {
                logger->info("bot finish calcing move, player = {}, pos = {}", player.to_string(), pos.to_string());
                if (should_bot_move(player)) {
                    if (do_move(player, pos, is_local_game)) {
                        deliver_to_others({ OpCode::MOVE_OP, pos.to_string() }, player.participant);
                    }
                }
            } else {
                logger->error("bot failed to calc move, player = {}", player.to_string());
            }
        };
        std::thread bot_thread { bot, std::ref(contest.current), player, is_local_game };
        bot_thread.detach();
    }

    auto receive_participant_name(Participant_ptr participant, std::string_view name)
    {
        auto new_name { name };

        if (!Player::is_valid_name(name)) {
            if (participant->name.empty())
                new_name = "Player" + std::to_string(contest.players.size() + 1);
            else
                new_name = participant->name;
        }

        if (new_name != participant->name) {
            if (!participant->is_local && !participant->name.empty() && participant->name != ::to_string(participant->endpoint()))
                deliver_to_local({ OpCode::CHAT_USERNAME_UPDATE_OP, participant->name, new_name });
            participant->name = new_name;
        }

        return new_name;
    }

    void reject_all_received_requests(string_view name)
    {
        logger->debug("reject_all_received_requests");
        ranges::for_each(received_requests, [=](auto& r) { if (name != r->name) r->deliver({ OpCode::REJECT_OP, find_local_participant()->name, "Already accepted other request" }); });
        received_requests.clear();
    }

public:
    Room(asio::io_context& io_context)
        : timer { io_context }
        , io_context { io_context }
    {
    }
    void process_data(Message msg, Participant_ptr participant)
    {
        logger->info("process_data: {} from {}", msg.to_string(), ::to_string(*participant));
        const string_view data1 { msg.data1 }, data2 { msg.data2 };
        switch (msg.op) {
        case OpCode::READY_OP:
            participant->ready(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::REJECT_OP:
            participant->reject(data1, data2);
            break;
        case OpCode::MOVE_OP:
            participant->move(data1, data2);
            // deliver_ui_state();
            break;
        case OpCode::GIVEUP_OP:
            participant->giveup(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::TIMEOUT_END_OP:
            participant->timeout_end(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::SUICIDE_END_OP:
            participant->suicide_end(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::GIVEUP_END_OP:
            participant->giveup_end(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::LEAVE_OP:
            participant->leave(data1, data2);
            break;
        case OpCode::CHAT_OP:
            participant->chat(data1, data2);
            break;
        // -------- Extend OpCode --------
        case OpCode::START_LOCAL_GAME_OP:
            participant->start_local_game(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::UPDATE_UI_STATE_OP:
            participant->update_ui_state(data1, data2);
            break;
        case OpCode::LOCAL_GAME_MOVE_OP:
            participant->local_game_move(data1, data2);
            // deliver_ui_state();
            break;
        case OpCode::CONNECT_TO_REMOTE_OP:
            participant->connect_to_remote(data1, data2);
            break;
        case OpCode::CONNECT_RESULT_OP:
            participant->connect_result(data1, data2);
            break;
        case OpCode::WIN_PENDING_OP:
            participant->win_pending(data1, data2);
            break;
        // -------- Chat --------
        case OpCode::CHAT_SEND_MESSAGE_OP:
            participant->chat_send_message(data1, data2);
            break;
        case OpCode::CHAT_SEND_BROADCAST_MESSAGE_OP:
            participant->chat_send_broadcast_message(data1, data2);
            break;
        case OpCode::CHAT_RECEIVE_MESSAGE_OP:
            participant->chat_receive_message(data1, data2);
            break;
        case OpCode::CHAT_USERNAME_UPDATE_OP:
            participant->chat_username_update(data1, data2);
            break;
        // -------- Contest Request --------
        case OpCode::SYNC_ONLINE_SETTINGS_OP:
            participant->sync_online_settings(data1, data2);
            break;
        case OpCode::SEND_REQUEST_BY_IP_OP:
            participant->send_request_by_ip(data1, data2);
            break;
        case OpCode::SEND_REQUEST_BY_USERNAME_OP:
            participant->send_request_by_username(data1, data2);
            break;
        case OpCode::RECEIVE_REQUEST_OP:
            participant->receive_request(data1, data2);
            break;
        case OpCode::ACCEPT_REQUEST_OP:
            participant->accept_request(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::REJECT_REQUEST_OP:
            participant->reject_request(data1, data2);
            break;
        case OpCode::RECEIVE_REQUEST_RESULT_OP:
            participant->receive_request_result(data1, data2);
            break;
        case OpCode::REPLAY_START_MOVE_OP:
            participant->replay_start_move(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::REPLAY_MOVE_OP:
            participant->replay_move(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::REPLAY_STOP_MOVE_OP:
            participant->replay_stop_move(data1, data2);
            deliver_ui_state();
            break;
        case OpCode::BOT_HOSTING_OP:
            participant->bot_hosting(data1, data2);
            deliver_ui_state();
            break;
        }
    }
    void join(Participant_ptr participant)
    {
        logger->info("{}:{} join", participant->endpoint().address().to_string(), participant->endpoint().port());
        participants.insert(participant);
    }

    void leave(Participant_ptr participant)
    {
        logger->info("leave: {}:{} leave", participant->endpoint().address().to_string(), participant->endpoint().port());
        if (participants.find(participant) == participants.end()) {
            logger->info("leave: {}:{} not found", participant->endpoint().address().to_string(), participant->endpoint().port());
            return;
        }
        logger->debug("leave: erase participant, participants.size() = {}", participants.size());
        participants.erase(participant);
        logger->debug("leave: erase end, participants.size() = {}", participants.size());
        logger->debug("leave: remove all requests from {}:{} in received_requests", participant->endpoint().address().to_string(), participant->endpoint().port());

        auto is_first { !received_requests.empty() && received_requests.front() == participant };
        std::erase(received_requests, participant);

        if (is_first && !received_requests.empty()) {
            logger->debug("leave: is_first && !received_requests.empty(), send received_requests.front() to local");
            deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, received_requests.front()->name, received_requests.front()->player.role.map("b", "w", "") });
        }
        if (participant == my_request) {
            logger->debug("leave: my_request->receiver == participant, clear my_request");
            my_request = nullptr;
        }
        if (!participant->name.empty()) {
            logger->debug("leave: participant->name is not empty, send LEAVE_OP to local");
            deliver_to_local({ OpCode::LEAVE_OP, participant->name });
        }
    }

    void close_except(Participant_ptr participant)
    {
        logger->debug("close_except: participants.size() = {}", participants.size());

        std::erase_if(participants, [&](auto p) { return p != participant; });

        logger->debug("close_except: close {}", ::to_string(participant->endpoint()));
        logger->debug("close_except: send LEAVE_OP");

        logger->debug("close_except: erase it");
        logger->debug("close_except: end");
        logger->debug("close_except: skip self");
    }

    void deliver_to_others(Message msg, Participant_ptr participant)
    {
        std::cout << "deliver to others: self = " << participant->endpoint() << std::endl;

        for (auto p : participants) {
            if (p != participant) {
                logger->info("broadcast {} from {}:{}", msg.to_string(), participant->endpoint().address().to_string(), participant->endpoint().port());
                p->deliver(msg);
            }
        }
    }

    void clear()
    {
        // TODO: only keep local session
        std::erase_if(participants, [](auto p) { return !p->is_local; });
    }

    bool timer_cancelled {};
    asio::steady_timer timer;
    std::set<Participant_ptr> participants;
    asio::io_context& io_context;
};

void Participant::move(string_view data1, string_view data2)
{
    Position pos { data1 };

    if (room.do_move(player, pos)) {
        room.deliver_to_others({ OpCode::MOVE_OP, data1, data2 }, shared_from_this()); // broadcast
    }
}

awaitable<void> Participant::reader()
{
    asio::streambuf buffer;
    std::istream stream(&buffer);
    try {
        for (std::string message;;) {
            co_await asio::async_read_until(socket, buffer, '\n', use_awaitable);
            std::getline(stream, message);
            logger->info("Receive: {}", message);
            room.process_data({ message }, shared_from_this());
        }
    } catch (std::exception& e) {
        logger->error("Exception: {}", e.what());
        if (!is_local)
            stop();
    }
}
awaitable<void> Participant::writer()
{
    try {
        while (socket.is_open()) {
            if (write_msgs.empty()) {
                asio::error_code ec;
                co_await timer.async_wait(redirect_error(use_awaitable, ec));
            } else {
                auto msg = write_msgs.front();
                co_await asio::async_write(socket, asio::buffer(msg.to_string() + "\n"),
                    use_awaitable);
                write_msgs.pop_front();
                if (msg.op == OpCode::LEAVE_OP && !is_local) {
                    room.leave(shared_from_this());
                    shutdown();
                }
            }
        }
    } catch (std::exception& e) {
        logger->error("Exception: {}", e.what());
        if (!this->is_local)
            stop();
    }
}
void Participant::start()
{
    room.join(shared_from_this());

    co_spawn(
        socket.get_executor(), [self = shared_from_this()] { return self->reader(); }, detached);
    co_spawn(
        socket.get_executor(), [self = shared_from_this()] { return self->writer(); }, detached);
}
void Participant::stop()
{
    logger->debug("stop: {} leave room", ::to_string(endpoint()));
    room.leave(shared_from_this());
    logger->debug("stop: close socket");
    socket.close();
    logger->debug("stop: cancel timer");
    timer.cancel();
}

void Participant::process_game_over()
{
    auto& contest { room.contest };
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

void start_session(asio::io_context& io_context, Room& room, asio::error_code& ec, tcp::endpoint endpoint);

class RemoteSession : public Participant {
public:
    RemoteSession(tcp::socket socket, Room& room, string name)
        : Participant(std::move(socket), room, ::to_string(socket.remote_endpoint()))
    {
        logger->debug("RemoteSession: {}", ::to_string(*this));
        this->is_local = false;
    }

    ~RemoteSession()
    {
        logger->debug("~RemoteSession: {}", ::to_string(*this));
        deliver({ OpCode::LEAVE_OP });
        stop();
    }

    tcp::endpoint endpoint() const override
    {
        return socket.remote_endpoint();
    }

    void ready(string_view data1, string_view data2) override
    {
        fmt::print("ready: is_local = {}, data1 = {}, data2 = {}\n", this->is_local, data1, data2);

        auto& my_request { this->room.my_request };
        auto& received_requests { this->room.received_requests };
        auto& contest { this->room.contest };

        if (contest.status == Contest::Status::GAME_OVER) {
            contest = Contest {};
            contest.duration = TIMEOUT;
        }

        // TODO: warn if invalid name
        string_view name { room.receive_participant_name(shared_from_this(), data1) };
        Role role { data2 };

        if (my_request == shared_from_this()) {
            auto local_participant { room.find_local_participant() };
            room.deliver_to_local({ OpCode::RECEIVE_REQUEST_RESULT_OP, "accepted", name });
            // contest accepted, enroll players
            logger->debug("contest accepted, enroll players");
            if (role == Role::NONE) {
                role = -local_participant->player.role;
            }
            if (role != -local_participant->player.role) {
                throw std::logic_error("role not match");
            }
            this->player = Player { shared_from_this(), name, role, PlayerType::REMOTE_HUMAN_PLAYER };
            logger->debug("role = {}, local_participant->player.role = {}", int(role), int(local_participant->player.role));
            contest = Contest { PlayerList { this->player, local_participant->player } };
            contest.duration = TIMEOUT;
            contest.local_role = local_participant->player.role;
            // TODO: catch exceptions when enrolling players
            my_request = nullptr;
            room.reject_all_received_requests(this->name);
            room.check_bot(this->player);
        } else {
            if (contest.status == Contest::Status::ON_GOING) {
                deliver({ OpCode::REJECT_OP, room.find_local_participant()->name, "Contest already started" });
                return;
            }
            this->player = Player { shared_from_this(), name, role, PlayerType::REMOTE_HUMAN_PLAYER };
            if (received_requests.empty()) {
                room.deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, this->name, this->player.role.map("b", "w", "") });
            }
            received_requests.push_back(shared_from_this());
        }
    }
    void reject(string_view data1, string_view) override
    {
        auto& contest { room.contest };
        auto& my_request { room.my_request };

        auto name { room.receive_participant_name(shared_from_this(), data1) };
        if (my_request == shared_from_this()) {
            room.deliver_to_local({ OpCode::RECEIVE_REQUEST_RESULT_OP, "rejected", name });
            my_request = nullptr;
            contest.reject();
        }
    }
    // move
    void giveup(string_view data1, string_view data2) override
    {
        // data1: role(local) / username(online)
        // TODO: data2(greeting)
        auto& contest { room.contest };
        Player player { this->player }, opponent;
        try {
            opponent = contest.players.at(-player.role);
        } catch (std::exception& e) {
            logger->error("Ignore give up: {}, playerlist: {}, try to find participant {}",
                e.what(), contest.players.to_string(), ::to_string(*this));
            return;
        }

        try {
            contest.concede(player);
        } catch (Contest::StatusError& e) {
            logger->error("Ignore concede: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
            return;
        } catch (std::logic_error& e) {
            logger->error("Concede: In {}'s turn, {}", contest.current.role.to_string(), e.what());
            return;
        }
        room.timer_cancelled = true;
        room.timer.cancel();
        if (contest.status == Contest::Status::GAME_OVER)
            process_game_over();
    }
    void gg_end(OpCode op)
    // confirmation from the loser
    {
        auto& contest { room.contest };
        if (contest.result.confirmed)
            return;
        auto player { this->player };
        auto opponent { contest.players.at(-player.role) };
        if (contest.result.winner == player.role) {
            auto gg_op { op };
            auto claimed_win_type {
                gg_op == OpCode::GIVEUP_END_OP        ? Contest::WinType::GIVEUP
                    : gg_op == OpCode::TIMEOUT_END_OP ? Contest::WinType::TIMEOUT
                                                      : Contest::WinType::SUICIDE
            };
            auto result_valid { claimed_win_type == contest.result.win_type };
            // Use lenient validation for timeout
            if (claimed_win_type == Contest::WinType::TIMEOUT && !result_valid) {
                auto remain_time { std::chrono::duration_cast<milliseconds>(timer.expiry() - std::chrono::steady_clock::now()) };
                // 270ms is the median human reaction time (reference: https://humanbenchmark.com/tests/reactiontime/statistics)
                if (remain_time < 270ms) {
                    result_valid = true;
                }
            }
            if (result_valid) {
                contest.confirm();
                // reply same GG_OP to confirm
                deliver({ gg_op, std::to_string(std::to_underlying(contest.result.win_type)) });
                // TODO: message format?
            } else {
                // result is not valid, do nothing
            }
        } else if (contest.result.winner == opponent.role) {
            contest.confirm();
        }
    }

    void giveup_end(string_view, string_view) override
    {
        return gg_end(OpCode::GIVEUP_END_OP);
    }
    void timeout_end(string_view, string_view) override
    {
        return gg_end(OpCode::TIMEOUT_END_OP);
    }
    void suicide_end(string_view, string_view) override
    {
        return gg_end(OpCode::SUICIDE_END_OP);
    }
    void leave(string_view, string_view) override
    {
        room.leave(shared_from_this());
        deliver({ OpCode::LEAVE_OP });
        // TODO: contest
    }
    void chat(string_view data1, string_view) override
    {
        auto name = this->name;
        if (name.empty()) {
            name = endpoint().address().to_string();
        }
        room.deliver_to_local({ OpCode::CHAT_RECEIVE_MESSAGE_OP, data1, name });
    }

    void start_local_game(string_view, string_view) override
    {
        throw std::logic_error { "Remote participant should not start local game" };
    }
    // local_game_timeout
    void local_game_move(string_view, string_view) override
    {
        throw std::logic_error { "Remote participant should not start local game" };
    }
    void connect_to_remote(string_view, string_view) override
    {
        throw std::logic_error { "Remote participant should not connect to remote" };
    }
    // connect_result
    // win_pending

    void chat_send_message(string_view, string_view) override
    {
        throw std::logic_error { "Remote participant should not send chat_send_message" };
    }
    void chat_send_broadcast_message(string_view, string_view) override
    {
        throw std::logic_error { "Remote participant should not send chat_send_broadcast_message" };
    }
    // chat_receive_message
    void chat_username_update(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not send chat_username_update" };
    }

    void sync_online_settings(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not sync_online_settings" };
    }
    void send_request_by_ip(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not send request" };
    }
    void send_request_by_username(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not send request" };
    }
    void receive_request(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not receive request" };
    }
    void accept_request(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not accept request" };
    }
    void reject_request(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not reject request" };
    }
    // receive_request_result
    void replay_start_move(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not start replay" };
    }
    void replay_move(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not replay" };
    }
    void replay_stop_move(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not stop replay" };
    }

    void bot_hosting(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not toggle bot hosting" };
    }
};

class LocalSession : public Participant {
    PlayerList local_players;

public:
    LocalSession(tcp::socket socket, Room& room, string name)
        : Participant(std::move(socket), room, ::to_string(socket.local_endpoint()))
    {
        this->is_local = true;
    }

    ~LocalSession()
    {
    }

    tcp::endpoint endpoint() const override
    {
        return socket.local_endpoint();
    }

    void ready(string_view, string_view) override
    {
        throw std::logic_error("READY_OP should not be sent by local");
    }
    void reject(string_view, string_view) override
    {
        throw std::logic_error("REJECT_OP should not be sent by local");
    }
    // move
    void giveup(string_view data1, string_view data2)
    {
        // data1: role(local) / username(online)
        // TODO: data2(greeting)
        auto& contest { room.contest };
        Player player, opponent;
        try {
            if (room.is_local_contest()) {
                Role role { data1 };
                player = contest.players.at(role);
            } else {
                player = contest.players.at(this->player.role);
            }
            opponent = contest.players.at(-player.role);
        } catch (std::exception& e) {
            logger->error("Ignore give up: {}, playerlist: {}, try to find participant {}",
                e.what(), contest.players.to_string(), ::to_string(*this));
            return;
        }

        room.deliver_to_others({ OpCode::GIVEUP_OP, data1, data2 }, shared_from_this()); // broadcast

        try {
            contest.concede(player);
        } catch (Contest::StatusError& e) {
            logger->error("Ignore concede: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
            return;
        } catch (std::logic_error& e) {
            logger->error("Concede: In {}'s turn, {}", contest.current.role.to_string(), e.what());
            return;
        }
        room.timer_cancelled = true;
        room.timer.cancel();
        if (contest.status == Contest::Status::GAME_OVER)
            process_game_over();
    }
    void giveup_end(string_view, string_view) override
    {
        throw std::logic_error("GIVEUP_END_OP should not be sent by local");
    }
    void timeout_end(string_view, string_view) override
    {
        throw std::logic_error("TIMEOUT_END_OP should not be sent by local");
    }
    void suicide_end(string_view, string_view) override
    {
        throw std::logic_error("SUICIDE_END_OP should not be sent by local");
    }
    void leave(string_view, string_view) override
    {
        room.clear();
    }
    void chat(string_view, string_view) override
    {
        throw std::logic_error { "CHAT_OP should not be sent by local" };
    }

    void start_local_game(string_view data1, string_view data2) override
    {
        // data1 = timeout|type, data2 = size
        auto& contest { room.contest };
        auto tmp = data1 | ranges::views::split("|"sv) | ranges::to<std::vector<std::string>>();
        if (tmp.size() != 2) {
            throw std::logic_error("invalid data1");
        }
        logger->info("start local game: timeout = {}, type = {}, size = {}", tmp[0], tmp[1], data2);
        if (contest.status != Contest::Status::NOT_PREPARED) {
            contest = Contest {};
        }
        int timeout = stoi(tmp[0]);
        int type = stoi(tmp[1]);
        int size = stoi(data2);

        seconds duration { timeout };

        Player player1 { shared_from_this(), "Black", Role::BLACK, (type == 2 || type == 3 ? PlayerType::BOT_PLAYER : PlayerType::LOCAL_HUMAN_PLAYER) },
            player2 { shared_from_this(), "White", Role::WHITE, (type == 1 || type == 3 ? PlayerType::BOT_PLAYER : PlayerType::LOCAL_HUMAN_PLAYER) };
        local_players = { player1, player2 };
        try {
            contest = Contest { local_players, size };
            contest.duration = duration;
            contest.local_role = Role::BLACK;
        } catch (Contest::StatusError& e) {
            logger->error("Ignore enroll player: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
            return;
        } catch (std::exception& e) {
            logger->error("Ignore enroll Player: {}, player1: {}, player2: {}. playerlist: {}.",
                e.what(), player1, player2, contest.players.to_string());
            contest.players = {};
            return;
        }
        room.check_bot(contest.players.at(Role::BLACK));
    }
    // update_ui_state
    // local_game_timeout
    void local_game_move(string_view data1, string_view data2) override
    {
        Position pos { data1 };
        Role role { data2 };
        Player player { local_players.at(role) };

        room.do_move(player, pos, true);
    }
    void connect_to_remote(string_view data1, string_view data2) override
    {
        asio::error_code ec;
        tcp::endpoint endpoint { asio::ip::make_address(data1), integer_cast<asio::ip::port_type>(data2) };
        start_session(room.io_context, room, ec, endpoint);
        if (ec) {
            logger->error("start_session failed: {}", ec.message());
            deliver({ OpCode::CONNECT_RESULT_OP, "failed", "connect failed" });
        } else {
            logger->info("start_session success: {}:{}", data1, data2);
            deliver({ OpCode::CONNECT_RESULT_OP, "success", ::to_string(endpoint) });
        }
    }
    // connect_result

    void chat_send_message(string_view data1, string_view data2) override
    {
        auto success { false };
        for (auto participant : room.participants) {
            if (!this->name.empty() && this->name == data2) {
                participant->deliver({ OpCode::CHAT_OP, data1 });
                success = true;
            } else if (participant->name.empty() && participant->endpoint().address().to_string() == data2) {
                participant->deliver({ OpCode::CHAT_OP, data1 });
                success = true;
            }
        }
    }
    void chat_send_broadcast_message(string_view data1, string_view) override
    {
        room.deliver_to_others({ OpCode::CHAT_OP, data1 }, shared_from_this());
    }
    // chat_receive_message
    void chat_username_update(string_view, string_view) override
    {
    }

    void sync_online_settings(string_view data1, string_view data2)
    {
        room.receive_participant_name(shared_from_this(), data1);
        std::chrono::seconds duration { stoi(data2) };
        TIMEOUT = duration;
        // room.contest.duration = TIMEOUT;
    }
    void send_request(string_view role_str, auto&& predicate)
    {
        auto& participants { this->room.participants };
        auto& my_request { this->room.my_request };
        Role role { role_str };
        auto participant { std::ranges::find_if(participants, predicate) };
        if (participant == participants.end()) {
            logger->error("send_request failed: {}, participant not found", ::to_string(shared_from_this()));
            return;
        }
        my_request = *participant;
        my_request->deliver({ OpCode::READY_OP, name, role_str });
        this->player = Player { shared_from_this(), name, role, PlayerType::LOCAL_HUMAN_PLAYER };
    }
    void send_request_by_ip(string_view data1, string_view data2) override
    {
        // data1 is host:port, data2 is role
        auto host { data1.substr(0, data1.find(':')) };
        auto port { data1.substr(data1.find(':') + 1) };
        tcp::endpoint ep { asio::ip::make_address(host), integer_cast<asio::ip::port_type>(port) };

        send_request(data2, [&](auto p) { return p->endpoint() == ep; });
    }
    void send_request_by_username(string_view data1, string_view data2) override
    {
        // data1 is username, data2 is role
        send_request(data2, [&](auto p) { return p->name == data1; });
    }
    void receive_request(string_view, string_view) override
    {
        throw std::logic_error { "receive_request" };
    }

    void accept_request(string_view, string_view) override
    {
        auto& received_requests { this->room.received_requests };
        if (received_requests.empty())
            throw std::logic_error { "received_requests.empty()" };
        auto participant { received_requests.front() };
        received_requests.pop_front();
        room.reject_all_received_requests(this->name);

        this->player = Player { shared_from_this(), this->name, -participant->player.role, PlayerType::LOCAL_HUMAN_PLAYER };
        participant->deliver({ OpCode::READY_OP, this->name });
        // logger->info("accept_request: {} {}", ::to_string(request.sender->player), ::to_string(request.receiver->player));
        room.contest = Contest { { this->player, participant->player } };
        room.contest.duration = TIMEOUT;

        logger->debug("contest accepted, enroll players");
        logger->debug("role = {}, my_request->player.role = {}", int(this->player.role), int(participant->player.role));
        room.contest.local_role = participant->is_local ? participant->player.role : -participant->player.role;
    }
    void reject_request(string_view, string_view) override
    {
        auto& received_requests { this->room.received_requests };
        if (received_requests.empty())
            throw std::runtime_error { "received_requests.empty()" };
        auto participant { received_requests.front() };
        received_requests.pop_front();
        participant->deliver({ OpCode::REJECT_OP, this->name });
        if (!received_requests.empty()) {
            auto next_participant { received_requests.front() };
            next_participant->deliver({ OpCode::RECEIVE_REQUEST_OP, next_participant->name, next_participant->player.role.map("b", "w", "") });
        }
    }
    // receive_request_result

    void replay_start_move(string_view data1, string_view data2)
    {
        auto& contest { this->room.contest };
        // data1: current moves
        // data2: board size
        if (contest.status == Contest::Status::ON_GOING) {
            throw std::logic_error("contest already started");
        }
        auto size { stoi(data2) };
        contest.clear();
        contest.set_board_size(size);
        Player player1 { shared_from_this(), "BLACK", Role::BLACK, PlayerType::LOCAL_HUMAN_PLAYER },
            player2 { shared_from_this(), "WHITE", Role::WHITE, PlayerType::LOCAL_HUMAN_PLAYER };
        contest = Contest { { player1, player2 } };
        contest.duration = TIMEOUT;
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
    }
    void replay_move(string_view data1, string_view) override
    {
        auto& contest { room.contest };
        Position pos { data1 };
        Role role { contest.moves.size() % 2 == 0 ? Role::BLACK : Role::WHITE };
        auto player { contest.players.at(role) };
        contest.play(player, pos);
    }
    void replay_stop_move(string_view, string_view) override
    {
        room.contest.clear();
    }

    void bot_hosting(string_view data1, string_view) override
    {
        Role role { data1 };
        if (!room.is_local_contest()) {
            role = this->player.role;
        }
        room.toggle_bot_hosting(room.contest.players.at(role), true);
    }
};

void start_session(asio::io_context& io_context, Room& room, asio::error_code& ec, tcp::endpoint endpoint)
{
    tcp::socket socket { io_context };
    socket.connect(endpoint, ec);
    if (!ec)
        std::make_shared<RemoteSession>(std::move(socket), room, "")->start();
}

template <bool is_local>
awaitable<void> listener(tcp::acceptor acceptor, Room& room)
{
    for (;;) {
        if constexpr (is_local)
            std::make_shared<LocalSession>(co_await acceptor.async_accept(use_awaitable), room, "")->start();
        else
            std::make_shared<RemoteSession>(co_await acceptor.async_accept(use_awaitable), room, "")->start();
        logger->info("new connection to {}", ::to_string(acceptor.local_endpoint()));
    }
}

_EXPORT void launch_server(std::vector<asio::ip::port_type> ports)
{
    try {
        asio::io_context io_context(1);
        Room room { io_context };

        tcp::endpoint local { tcp::v4(), ports[0] };
        co_spawn(io_context, listener<true>(tcp::acceptor(io_context, local), room), detached);
        logger->info("Serving on {}:{}", local.address().to_string(), local.port());
        for (auto port : ports | std::views::drop(1)) {
            tcp::endpoint ep { tcp::v4(), port };
            co_spawn(io_context, listener<false>(tcp::acceptor(io_context, ep), room), detached);
            logger->info("Serving on {}:{}", ep.address().to_string(), ep.port());
        }

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) { io_context.stop(); });

        io_context.run();
    } catch (std::exception& e) {
        logger->error("Exception: {}", e.what());
    }
}