#pragma once
#define export

#include <algorithm>
#include <cctype>
#include <map>
#include <ranges>
#include <stdexcept>
#include <vector>

#include <asio/ip/tcp.hpp>
using asio::ip::tcp;

#include "message.hpp"
#include "rule.hpp"

class Participant {
public:
    bool is_local { false };
    Participant() = default;
    Participant(bool is_local)
        : is_local(is_local)
    {
    }
    virtual ~Participant()
    {
    }
    virtual tcp::endpoint endpoint() const = 0;
    virtual void deliver(Message msg) = 0;
    virtual void stop() = 0;
    virtual bool operator==(const Participant&) const = 0;
};

export using Participant_ptr = std::shared_ptr<Participant>;

export struct Player {
    Participant_ptr participant;
    std::string name;
    Role role;
    PlayerType type;
    Player() = default;
    Player(Participant_ptr participant, Role role)
        : participant(participant)
        , role(role)
    {
    }
    Player(Participant_ptr participant, std::string_view name, Role role, PlayerType type)
        : participant(participant)
        , name(name)
        , role(role)
        , type(type)
    {
    }
    auto operator<=>(const Player&) const = default;

    auto name_valid()
    {
        return !name.empty() && std::ranges::all_of(name, [](auto c) { return std::isalnum(c) || c == '_'; });
    }
    auto map(auto v_black, auto v_white) const { return role.map(v_black, v_white); }
    auto empty() const { return !participant; }
};

struct PlayerCouple {
    Player player1, player2;
    auto operator[](Role role) -> Player& { return role.map(player1, player2); }
    auto operator[](Participant_ptr participant) -> Player&
    {
        if (*player1.participant == *participant)
            return player1;
        if (*player2.participant == *participant)
            return player2;
        throw std::logic_error("Participant not in couple");
    }
    auto operator[](Player player) -> Player&
    {
        if (player.role == Role::NONE) {
            return player.participant == player1.participant ? player1 : player2;
        } else {
            return player.role.map(player1, player2);
        }
    }
    auto contains(Role role) const
    {
        return !role.map(player1, player2).empty();
    }
    auto contains(Participant_ptr participant) const
    {
        return player1.participant && *player1.participant == *participant
            || player2.participant && *player2.participant == *participant;
    }
    auto insert(Player&& player)
    {
        if (!player1.empty() && !player2.empty())
            throw std::logic_error("Couple already full");
        if (contains(player.role))
            throw std::logic_error("Role already occupied");
        player.role.map(player1, player2) = std::move(player);
    }
    void clear() { player1 = player2 = Player {}; }
    auto opposite(Player player) -> Player&
    {
        return player.role.map(player2, player1);
    }
};

export class Contest {
public:
    enum class Status {
        NOT_PREPARED,
        ON_GOING,
        GAME_OVER,
    };
    bool should_giveup = false;
    enum class WinType {
        NONE,
        TIMEOUT,
        SUICIDE,
        GIVEUP,
    };
    class StonePositionitionOccupiedException : public std::logic_error {
        using std::logic_error::logic_error;
    };
    class TimeLimitExceededException : public std::runtime_error {
        using runtime_error::runtime_error;
    };

    State current {};
    std::vector<Position> moves;
    PlayerCouple players;

    Status status = Status::NOT_PREPARED;
    WinType win_type;
    Role winner;

    void clear()
    {
        current = State {};
        moves.clear();
        players.clear();
        status = Status {};
        win_type = WinType {};
        winner = Role {};
        should_giveup = false;
    }

    void reject()
    {
        if (status != Status::NOT_PREPARED)
            throw std::logic_error("Contest already started");
        players.clear();
    }

    void enroll(Player player)
    {
        if (status != Status::NOT_PREPARED)
            throw std::logic_error("Contest already started");

        players.insert(std::move(player));
        if (players.contains(Role::BLACK) && players.contains(Role::WHITE))
            status = Status::ON_GOING;
    }

    void play(Player player, Position pos)
    {
        if (status != Status::ON_GOING)
            throw std::logic_error("Contest not started");
        if (current.role != player.role)
            throw std::logic_error(player.name + " not allowed to play");

        if (current.board[pos])
            throw StonePositionitionOccupiedException("Stone positionition occupied");

        current = current.next_state(pos);
        moves.push_back(pos);

        if ((winner = current.is_over())) {
            status = Status::GAME_OVER;
            win_type = WinType::SUICIDE;
        }
        if (!current.available_actions().size())
            should_giveup = true;
    }

    void concede(Player player)
    {
        if (status != Status::ON_GOING)
            throw std::logic_error("Contest not started");
        if (players[current.role] != player)
            throw std::logic_error(player.name + " not allowed to concede");

        status = Status::GAME_OVER;
        win_type = WinType::GIVEUP;
        winner = -player.role;
    }

    void overtime(Participant_ptr participant)
    {
        auto player = players[participant];

        if (status != Status::ON_GOING)
            throw std::logic_error("Contest not started");
        if (players[current.role] != player)
            throw std::logic_error("not in " + player.name + "'s turn");

        status = Status::GAME_OVER;
        win_type = WinType::TIMEOUT;
        winner = -player.role;
    }
    auto round() const { return moves.size(); }
};