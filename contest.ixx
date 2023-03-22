module;

#include <algorithm>
#include <cctype>
#include <compare>
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
    virtual bool operator==(const Participant&) const = 0;
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

struct PlayerCouple {
    Player player1, player2;
    auto contains(Role role) const { return role.map(player1, player2).participant; }
    auto contians(Participant_ptr participant) const
    {
        return *player1.participant == *participant || *player2.participant == *participant;
    }
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
    auto clear()
    {
        player1 = Player {};
        player2 = Player {};
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
    enum class Win_Type {
        NULL,
        TIMEOUT,
        SUICIDE,
        GIVEUP,
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
    Win_Type win_type;
    Role winner;

    void clear()
    {
        current = State {};
        moves.clear();
        players.clear();
        status = Status {};
        win_type = Win_Type{};
        winner = Role {};
        should_giveup = false;
    }

    void reject()
    {
        if (status != Status::NOT_PREPARED)
            throw logic_error("Contest already started");
        players.clear();
    }

    void enroll(Player player)
    {
        if (status != Status::NOT_PREPARED)
            throw logic_error("Contest already started");

        players.insert(std::move(player));

        if (players.contains(Role::BLACK) && players.contains(Role::WHITE))
            status = Status::ON_GOING;
    }

    void play(Participant_ptr participant, Position pos)
    {
        auto player = players[participant];

        if (status != Status::ON_GOING)
            throw logic_error("Contest not started");
        if (current.role != player.role)
            throw logic_error(player.name + " not allowed to play");

        if (current.board[pos])
            throw StonePositionitionOccupiedException("Stone positionition occupied");

        current = current.next_state(pos);
        moves.push_back(pos);

        if (winner = current.is_over())
            status = Status::GAME_OVER;
            win_type = Win_Type::SUICIDE;
        if(!current.available_actions().size())
            should_giveup = true;
    }

    void concede(Participant_ptr participant)
    {
        auto player = players[participant];

        if (status != Status::ON_GOING)
            throw logic_error("Contest not started");
        if (players[current.role] != player)
            throw logic_error(player.name + " not allowed to concede");

        status = Status::GAME_OVER;
        win_type = Win_Type::GIVEUP;
        winner = -player.role;
    }

    void timeout(Participant_ptr participant)
    {
        auto player = players[participant];
        if (status != Status::ON_GOING)
            throw logic_error("Contest not started");
        if (players[current.role] != player)
            throw logic_error("Not in " + player.name + "'s round");
        status = Status::GAME_OVER;
        win_type = Win_Type::TIMEOUT;
        winner = -player.role;
    }
    auto round() const { return moves.size(); }
};