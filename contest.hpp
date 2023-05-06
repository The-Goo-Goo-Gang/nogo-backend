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
#include "log.hpp"

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
        logger->critical("PlayerCouple: Participant not in couple");
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
        return (player1.participant && *player1.participant == *participant)
            || (player2.participant && *player2.participant == *participant);
    }
    auto insert(Player&& player)
    {
        if (!player1.empty() && !player2.empty()){
            logger->critical("Insert player: Couple already full");
            throw std::logic_error("Couple already full");
        }
        if (contains(player.role)){
            logger->critical("Insert player: {} role already occupied",player.role.map("black","white","none"));
            throw std::logic_error("Role already occupied");
        }
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
    bool should_giveup { false };
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

    Status status;
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
        if (status != Status::NOT_PREPARED){
            logger->critical("Reject: Contest stautus is {}", (int)status);
            throw std::logic_error("Contest already started");
        }
        players.clear();
    }

    void enroll(Player player)
    {
        if (status != Status::NOT_PREPARED){
            logger->critical("Enroll Player: Contest stautus is {}", (int)status);
            throw std::logic_error("Contest already started");
        }
        players.insert(std::move(player));
        if (players.contains(Role::BLACK) && players.contains(Role::WHITE))
            status = Status::ON_GOING;
    }

    void play(Player player, Position pos)
    {
        if (status != Status::ON_GOING){
            logger->critical("Play: Contest stautus is {}", (int)status);
            throw std::logic_error("Contest not started");
        }
        if (current.role != player.role){
            logger->critical("Play: In {}'s turn, " + player.name + " not allowed to play", current.role.map("black", "white"));
            throw std::logic_error(player.name + " not allowed to play");
        }
        if (current.board[pos]){
            logger->critical("Play: positon ({},{}) is occupied", pos.x, pos.y);
            throw StonePositionitionOccupiedException("Stone positionition occupied");
        }
        std::cout << "contest play " << pos.x << ", " << pos.y << std::endl;
        logger->info("contest play " + std::to_string(pos.x) + ", " + std::to_string(pos.y));
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
        if (status != Status::ON_GOING){
            logger->critical("Concede: Contest status is {}",(int)status);
            throw std::logic_error("Contest not started");
        }
        if (players[current.role] != player){
            logger->critical("Concede: In {}'s turn," + player.name + " not allowed to concede",current.role.map("black","white"));
            throw std::logic_error(player.name + " not allowed to concede");
        }
        status = Status::GAME_OVER;
        win_type = WinType::GIVEUP;
        winner = -player.role;
    }

    void overtime(Player player)
    {
        if (status != Status::ON_GOING){
            logger->critical("Overtime: Contest status is {}",(int)status);
            throw std::logic_error("Contest not started");
        }
        if (players[current.role] != player){
            logger->critical("Overtime: In {}'s turn," + player.name + " shouldn't overtime",current.role.map("black","white"));
            throw std::logic_error("not in " + player.name + "'s turn");
        }
        status = Status::GAME_OVER;
        win_type = WinType::TIMEOUT;
        winner = -player.role;
    }
    auto round() const { return moves.size(); }
};