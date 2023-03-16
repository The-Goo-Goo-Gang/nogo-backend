module;

#include <algorithm>
#include <cctype>
#include <map>
#include <ranges>
#include <stdexcept>
#include <vector>

export module nogo.contest;

import nogo.rule;
import nogo.network.data;

using namespace std;
namespace ranges = std::ranges;

class Participant {
public:
    virtual ~Participant()
    {
    }
    virtual void deliver(Message msg) = 0;
    virtual auto operator<=>(const Session&) const = 0;
};
// bool is_evil { false };
// bool is_local { false };

export using Participant_ptr = std::shared_ptr<Participant>;

export struct Player {
    Participant_ptr participant;
    Role role;
    std::string name;
    auto operator<=>(const Player&) const = default;

    auto name_valid()
    {
        return !name.empty() && ranges::all_of(name, [](auto c) { return std::isalnum(c) || c == '_'; });
    }
    auto map(auto v_black, auto v_white) const { return role.map(v_black, v_white); }
    auto empty() const { return !participant; }
};

class PlayerCouple {
    Player player1, player2;
    auto operator[](Role role) -> Player& { return role.map(player1, player2); }
    auto operator[](Participant_ptr participant) -> Player&
    {
        if (*player1.participant == *participant)
            return player1;
        if (*player2.participant == *participant)
            return player2;
        throw logic_error("Participant not in couple");
    }
    auto insert(Player&& player)
    {
        if (!player1.empty() && !player2.empty())
            throw logic_error("Couple already full");
        if (contains(player.role))
            throw logic_error("Role already occupied");
        player.role.map(player1, player2) = std::move(player);
    }
};

export class Contest {
public:
    enum class Status {
        NOT_PREPARED,
        ON_GOING,
        GAME_OVER,
    };
    class StonePositionitionOccupiedException : public std::logic_error {
        using logic_error::logic_error;
    };
    class TimeLimitExceededException : public std::runtime_error {
        using runtime_error::runtime_error;
    };

    State current {};
    std::vector<Position> moves;
    PlayerCouple players;

    Status status;
    Role winner;

    void clear()
    {
        current = State {};
        moves.clear();
        players.clear();
        status = Status {};
        winner = Role {};
    }

    void reject(Player player)
    {
        if (status)
            throw logic_error("Contest already started");
        players.clear();
    }

    void register(Player player)
    {
        if (status)
            throw logic_error("Contest already started");

        players.insert(player);

        if (players.contains(Role::BLACK) && players.contains(Role::WHITE))
            status = Status::ON_GOING;
    }

    void play(Participant_ptr participant, Position pos)
    {
        auto player = players[participant];

        if (status != Status::ON_GOING)
            throw logic_error("Contest not started");
        if (current.role != player.role)
            throw logic_error(to_string(player) + " not allowed to play");

        if (current.board[pos])
            throw StonePositionitionOccupiedException("Stone positionition occupied");

        current = current.next_state(pos);
        moves.push_back(pos);

        if (winner = current.is_over())
            status = Status::GAME_OVER;
    }

    void concede(Player player)
    {
        auto player = players[participant];

        if (status != Status::ON_GOING)
            throw logic_error("Contest not started");
        if (players[current.role] != player)
            throw logic_error(to_string(player) + " not allowed to concede");

        status = Status::GAME_OVER;
        winner = -player.role;
    }

    auto round() const { return moves.size(); }
};