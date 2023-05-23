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

constexpr auto TIMEOUT { 30s };

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
        , name(!name.empty() ? name : ::to_string(this->endpoint()))
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
    virtual void move(string_view, string_view) = 0;
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

    virtual void update_username(string_view, string_view) = 0;
    virtual void send_request(string_view, string_view) = 0;
    virtual void send_request_by_username(string_view, string_view) = 0;
    virtual void receive_request(string_view, string_view) = 0;
    virtual void accept_request(string_view, string_view) = 0;
    virtual void reject_request(string_view, string_view) = 0;
    void receive_request_result(string_view, string_view)
    {
        throw std::logic_error { "Participant should not send receive_request_result" };
    }
};

_EXPORT using Participant_ptr = std::shared_ptr<Participant>;

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
public:
    Contest contest;
    std::deque<std::string> chats;
    std::optional<ContestRequest> my_request;
    std::queue<ContestRequest> received_requests;

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
            if (!participant->is_local && !participant->name.empty() && new_name != participant->name)
                deliver_to_local({ OpCode::CHAT_USERNAME_UPDATE_OP, participant->name, new_name });
            name = new_name;
        }

        return new_name;
    }

    void receive_new_request(const ContestRequest& request)
    {
        if (received_requests.empty()) {
            deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, request.sender->name, request.role.map("b", "w", "") });
        }
        received_requests.push(request);
        deliver_ui_state();
    }

    void enroll_players(ContestRequest& request)
    {
        Player player1 { request.sender, request.sender->name, request.role, request.sender->is_local ? PlayerType::LOCAL_HUMAN_PLAYER : PlayerType::REMOTE_HUMAN_PLAYER },
            player2 { request.receiver, request.receiver->name, -request.role, request.receiver->is_local ? PlayerType::LOCAL_HUMAN_PLAYER : PlayerType::REMOTE_HUMAN_PLAYER };
        contest.enroll(std::move(player1)), contest.enroll(std::move(player2));
        contest.local_role = request.sender->is_local ? request.role : -request.role;
        contest.duration = TIMEOUT;
    }

    void reject_all_received_requests()
    {
        while (!received_requests.empty()) {
            auto r = received_requests.front();
            received_requests.pop();
            r.sender->deliver({ OpCode::REJECT_OP, r.receiver->name });
        }
    }

public:
    Room(asio::io_context& io_context)
        : timer { io_context }
        , io_context { io_context }
        , my_request { std::nullopt }
    {
    }
    void process_data(Message msg, Participant_ptr participant)
    {
        logger->info("process_data: {} from {}", msg.to_string(), ::to_string(participant));
        const string_view data1 { msg.data1 }, data2 { msg.data2 };
        switch (msg.op) {
        case OpCode::READY_OP:
            participant->ready(data1, data2);
            break;
        case OpCode::REJECT_OP:
            participant->reject(data1, data2);
            break;
        case OpCode::MOVE_OP:
            participant->move(data1, data2);
            break;
        case OpCode::GIVEUP_OP:
            participant->giveup(data1, data2);
            break;
        case OpCode::TIMEOUT_END_OP:
            participant->timeout_end(data1, data2);
            break;
        case OpCode::SUICIDE_END_OP:
            participant->suicide_end(data1, data2);
            break;
        case OpCode::GIVEUP_END_OP:
            participant->giveup_end(data1, data2);
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
            break;
        case OpCode::UPDATE_UI_STATE_OP:
            participant->update_ui_state(data1, data2);
            break;
        // OpCode::LOCAL_GAME_TIMEOUT_OP:
        case OpCode::LOCAL_GAME_MOVE_OP:
            participant->local_game_move(data1, data2);
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
        case OpCode::UPDATE_USERNAME_OP:
            participant->update_username(data1, data2);
            break;
        case OpCode::SEND_REQUEST_OP:
            participant->send_request(data1, data2);
            break;
        case OpCode::SEND_REQUEST_BY_USERNAME_OP:
            participant->send_request_by_username(data1, data2);
            break;
        case OpCode::RECEIVE_REQUEST_OP:
            participant->receive_request(data1, data2);
            break;
        case OpCode::ACCEPT_REQUEST_OP:
            participant->accept_request(data1, data2);
            break;
        case OpCode::REJECT_REQUEST_OP:
            participant->reject_request(data1, data2);
            break;
        case OpCode::RECEIVE_REQUEST_RESULT_OP:
            participant->receive_request_result(data1, data2);
            break;
        }
        deliver_ui_state();
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
        if (participants.find(participant) != participants.end())
            participants.erase(participant);
        logger->debug("leave: erase end, participants.size() = {}", participants.size());
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
            deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, received_requests.front().sender->name, received_requests.front().role.map("b", "w", "") });
        }
        if (participant == my_request->receiver) {
            logger->debug("leave: my_request->receiver == participant, clear my_request");
            my_request = std::nullopt;
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
        erase_if(participants, [](auto p) { return !p->is_local; });
    }

    bool timer_cancelled {};
    asio::steady_timer timer;
    std::set<Participant_ptr> participants;
    asio::io_context& io_context;
};

awaitable<void> Participant::reader()
{
    asio::streambuf buffer;
    std::istream stream(&buffer);
    try {
        for (std::string message;;) {
            co_await asio::async_read_until(socket, buffer, '\n', use_awaitable);
            std::getline(stream, message);
            logger->info("Rece{}", message);
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
        : Participant(std::move(socket), room, name)
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
            contest.clear();
        }

        // TODO: warn if invalid name
        auto name { room.receive_participant_name(shared_from_this(), data1) };
        Role role { data2 };

        if (my_request.has_value() && shared_from_this() == my_request->receiver) {
            room.deliver_to_local({ OpCode::RECEIVE_REQUEST_RESULT_OP, "accepted", name });
            // contest accepted, enroll players
            room.enroll_players(my_request.value());
            // TODO: catch exceptions when enrolling players
            my_request = std::nullopt;
            room.reject_all_received_requests();
        } else {
            ContestRequest request { shared_from_this(), room.find_local_participant(), role };
            if (received_requests.empty()) {
                room.deliver_to_local({ OpCode::RECEIVE_REQUEST_OP, request.sender->name, request.role.map("b", "w", "") });
            }
            received_requests.push(request);
        }
    }
    void reject(string_view data1, string_view) override
    {
        auto& contest { room.contest };
        auto& my_request { room.my_request };
        contest.reject();

        auto name { room.receive_participant_name(shared_from_this(), data1) };
        if (my_request.has_value() && shared_from_this() == my_request->receiver) {
            room.deliver_to_local({ OpCode::RECEIVE_REQUEST_RESULT_OP, "rejected", name });
            my_request = std::nullopt;
        }
    }
    void move(string_view data1, string_view data2) override
    {
        auto& contest { room.contest };
        auto& timer { room.timer };
        auto& timer_cancelled { room.timer_cancelled };
        timer_cancelled = true;
        timer.cancel();
        std::cout << "timer canceled" << std::endl;

        Position pos { data1 };
        try {
            milliseconds ms { stoull(data2) };
        } catch (std::exception& e) {
            // TODO:
        }

        // TODO: adjust time

        Player player { this->player }, opponent;
        try {
            opponent = contest.players.at(-player.role);
        } catch (std::exception& e) {
            logger->error("Ignore move: {}, playerlist: {}, try to find participant {}",
                e.what(), contest.players.to_string(), ::to_string(*this));
            return;
        }

        try {
            contest.play(player, pos);
        } catch (Contest::StatusError& e) {
            logger->error("Ignore move: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
            return;
        } catch (std::exception& e) {
            logger->error("Ignore move: {}, player:{}", e.what(), ::to_string(player));
            return;
        }

        room.deliver_to_others({ OpCode::MOVE_OP, data1, data2 }, shared_from_this()); // broadcast
        if (contest.status == Contest::Status::GAME_OVER)
            process_game_over();

        if (contest.status == Contest::Status::ON_GOING) {
            contest.duration = TIMEOUT;
            timer_cancelled = false;
            timer.expires_after(contest.duration);
            timer.async_wait([&](const asio::error_code& ec) {
                if (!ec && !timer_cancelled) {
                    contest.timeout(opponent);
                    if (contest.status == Contest::Status::GAME_OVER)
                        this->process_game_over();
                    room.deliver_ui_state();
                }
            });
        }
    }
    void giveup(string_view data1, string_view data2) override
    {
        auto& contest { room.contest };
        Role role { data1 };
        auto player { this->player };
        auto opponent { contest.players.at(-player.role) };

        room.deliver_to_others({ OpCode::GIVEUP_OP, data1, data2 }, shared_from_this()); // broadcast

        contest.concede(player);
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

    void update_username(string_view, string_view) override
    {
        throw std::logic_error { "Participant should not update_username" };
    }
    void send_request(string_view, string_view) override
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
};

class LocalSession : public Participant {
public:
    LocalSession(tcp::socket socket, Room& room, string name)
        : Participant(std::move(socket), room, name)
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
    void move(string_view, string_view) override
    {
        throw std::logic_error("MOVE_OP should not be sent by local");
    }
    void giveup(string_view data1, string_view data2)
    {
        auto& contest { room.contest };
        // ignore data1(username)
        // TODO: data2(greeting)
        Player player { this->player }, opponent;
        try {
            opponent = contest.players.at(-player.role);
        } catch (std::exception& e) {
            logger->error("Ignore give up: {}, playerlist: {}, try to find participant {}",
                e.what(), contest.players.to_string(), ::to_string(*this));
            return;
        }

        if (is_local) {
            room.deliver_to_others({ OpCode::GIVEUP_OP, data1, data2 }, shared_from_this()); // broadcast
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
    } // TODO:
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
        auto& contest { room.contest };
        std::cout << "start local game: timeout = " << data1 << ", size = " << data2 << std::endl;
        if (contest.status != Contest::Status::NOT_PREPARED) {
            contest.clear();
        }
        // int timeout = std::stoi(msg.data1);
        // int rank_n = std::stoi(msg.data2);

        seconds duration { stoi(data1) };
        contest.duration = duration;

        Player player1 { shared_from_this(), "BLACK", Role::BLACK, PlayerType::LOCAL_HUMAN_PLAYER },
            player2 { shared_from_this(), "WHITE", Role::WHITE, PlayerType::LOCAL_HUMAN_PLAYER };
        try {
            contest.enroll(std::move(player1)), contest.enroll(std::move(player2));
        } catch (Contest::StatusError& e) {
            logger->error("Ignore enroll player: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
            return;
        } catch (std::exception& e) {
            logger->error("Ignore enroll Player: {}, player1: {}, player2: {}. playerlist: {}.",
                e.what(), ::to_string(player1), ::to_string(player2), contest.players.to_string());
            contest.players = {};
            return;
        }
        contest.local_role = Role::BLACK;
    }
    // update_ui_state
    // local_game_timeout
    void local_game_move(string_view data1, string_view data2) override
    {
        Position pos { data1 };
        Role role { data2 };
        auto& contest { room.contest };
        timer.cancel();

        Player player { this->player }, opponent;
        try {
            opponent = contest.players.at(-player.role);
        } catch (std::exception& e) {
            logger->error("Ignore move: {}, playerlist: {}, try to find role {}, participant {}",
                e.what(), contest.players.to_string(), role.to_string(), ::to_string(*this));
            return;
        }

        try {
            contest.play(player, pos);
        } catch (Contest::StatusError& e) {
            logger->error("Ignore move: {}, Contest status is {}", e.what(), std::to_underlying(contest.status));
            return;
        } catch (std::exception& e) {
            logger->error("Ignore move: {}, player:{}", e.what(), ::to_string(player));
            return;
        }

        if (contest.status == Contest::Status::ON_GOING) {
            timer.expires_after(contest.duration);
            timer.async_wait([&](const asio::error_code& ec) {
                if (!ec) {
                    contest.timeout(opponent);
                    opponent.participant->deliver({ OpCode::TIMEOUT_END_OP });
                    room.deliver_ui_state();
                }
            });
        }
    }
    void connect_to_remote(string_view data1, string_view data2) override
    {
        asio::error_code ec;
        tcp::endpoint endpoint { asio::ip::make_address(data1), integer_cast<asio::ip::port_type>(data2) };
        start_session(room.io_context, room, ec, endpoint);
        if (ec) {
            logger->error("start_session failed: {}", ec.message());
            deliver({ OpCode::CONNECT_RESULT_OP, "failed", ec.message() });
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

    void update_username(string_view data1, string_view)
    {
        room.receive_participant_name(shared_from_this(), data1);
    }
    void send_request(string_view data1, string_view data2) override
    {
        auto& participants { room.participants };
        auto& my_request { room.my_request };
        // data1 is host:port, data2 is role
        auto host { data1.substr(0, data1.find(':')) };
        auto port { data1.substr(data1.find(':') + 1) };
        tcp::endpoint ep { asio::ip::make_address(host), integer_cast<asio::ip::port_type>(port) };
        Role role { data2 };

        auto ps = participants | std::views::filter([ep](auto p) { return p->endpoint() == ep; })
            | ranges::to<std::vector>();

        if (ps.size() != 1) {
            throw std::logic_error { "participants.size() != 1" };
        }

        auto receiver { ps[0] };
        ContestRequest request { shared_from_this(), receiver, role };
        my_request = request;
        receiver->deliver({ OpCode::READY_OP, name, data2 });
    }
    void send_request_by_username(string_view data1, string_view data2) override
    {
        auto& participants { this->room.participants };
        auto& my_request { this->room.my_request };

        // data1 is username, data2 is role
        auto ps = participants | std::views::filter([data1](auto p) { return p->name == data1; })
            | ranges::to<std::vector>();

        if (ps.size() != 1) {
            throw std::logic_error { "participants.size() != 1" };
        }

        auto receiver { ps[0] };
        ContestRequest request { shared_from_this(), receiver, Role { data2 } };
        my_request = request;
        receiver->deliver({ OpCode::READY_OP, name, data2 });
    }
    void receive_request(string_view, string_view) override
    {
        auto& received_requests { this->room.received_requests };
        if (received_requests.empty()) {
            throw std::logic_error { "received_requests.empty()" };
        }
        auto request = received_requests.front();
        received_requests.pop();
        room.reject_all_received_requests();
        request.sender->deliver({ OpCode::READY_OP, request.receiver->name, (-request.role).map("b", "w", "") });
        room.enroll_players(request);
    }
    void accept_request(string_view, string_view) override
    {
        auto& received_requests { this->room.received_requests };
        if (received_requests.empty()) {
            throw std::logic_error { "received_requests.empty()" };
        }
        auto request { received_requests.front() };
        received_requests.pop();
        room.reject_all_received_requests();
        request.sender->deliver({ OpCode::READY_OP, request.receiver->name, (-request.role).map("b", "w", "") });
        room.enroll_players(request);
    }
    void reject_request(string_view, string_view) override
    {
        auto& received_requests { this->room.received_requests };
        if (received_requests.empty()) {
            throw std::runtime_error { "received_requests.empty()" };
        }
        auto request = received_requests.front();
        received_requests.pop();
        request.sender->deliver({ OpCode::REJECT_OP, request.receiver->name });
        if (!received_requests.empty()) {
            auto next_request = received_requests.front();
            next_request.receiver->deliver({ OpCode::RECEIVE_REQUEST_OP, next_request.sender->name, next_request.role.map("b", "w", "") });
        }
    }
    // receive_request_result
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