#pragma once
#ifndef _EXPORT
#define _EXPORT
#endif 

#include <algorithm>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <vector>

#ifdef __GNUC__
#include <range/v3/all.hpp>
namespace ranges::views {
auto join_with = join;
};
#else
namespace ranges = std::ranges;
#endif

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

_EXPORT using Participant_ptr = std::shared_ptr<Participant>;

_EXPORT struct Player {
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

    static auto is_valid_name(std::string_view name)
    {
        return !name.empty() && std::ranges::all_of(name, [](auto c) { return std::isalnum(c) || c == '_'; });
    }
};

class PlayerList {
    std::vector<Player> players;

public:
    auto find(Role role, Participant_ptr participant = nullptr)
    {
        // If the criteria is valid, the player must match it
        auto it = std::ranges::find_if(players, [&](auto& p) {
            return (!role || p.role == role) && (!participant || p.participant == participant);
        });
        return it == players.end() ? nullptr : std::addressof(*it);
    }
    auto find(Role role, Participant_ptr participant = nullptr) const
    {
        return static_cast<const Player*>(const_cast<PlayerList*>(this)->find(role, participant));
    }

    auto at(Role role, Participant_ptr participant = nullptr) -> Player&
    {
        auto it = find(role, participant);
        if (!it)
            throw std::logic_error("Player not found");
        return static_cast<Player&>(*it);
    }
    auto at(Role role, Participant_ptr participant = nullptr) const
    {
        return static_cast<const Player&>(const_cast<PlayerList*>(this)->at(role, participant));
    }

    auto contains(Role role, Participant_ptr participant = nullptr) const
    {
        return find(role, participant) != nullptr;
    }
    auto insert(Player&& player)
    {
        if (std::ranges::find(players, player) != players.end())
            throw std::logic_error("Player already in list");
        if (contains(player.role))
            throw std::logic_error("Role already occupied");
        if (player.role == Role::NONE) {
            if (contains(Role::BLACK))
                player.role = Role::WHITE;
            else if (contains(Role::WHITE))
                player.role = Role::BLACK;
            else
                throw std::logic_error("No role for player");
        }
        players.push_back(std::move(player));
    }
    auto size() const
    {
        return players.size();
    }
};

_EXPORT class Contest {
public:
    enum class Status {
        NOT_PREPARED,
        ON_GOING,
        GAME_OVER,
    };
    enum class WinType {
        NONE,
        TIMEOUT,
        SUICIDE,
        GIVEUP,
    };
    struct GameResult {
        Role winner;
        Contest::WinType win_type;
    };
    class StonePositionitionOccupiedException : public std::logic_error {
        using std::logic_error::logic_error;
    };
    class TimeLimitExceededException : public std::runtime_error {
        using runtime_error::runtime_error;
    };

    bool should_giveup { false };

    State current {};
    std::vector<Position> moves;
    PlayerList players;

    Status status;
    GameResult result;

    void clear()
    {
        current = {};
        moves.clear();
        players = {};
        status = {};
        result = {};
        should_giveup = false;
    }

    void reject()
    {
        if (status != Status::NOT_PREPARED)
            throw std::logic_error("Contest already started");
        players = {};
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

        std::cout << "contest play " << pos.x << ", " << pos.y << std::endl;
        current = current.next_state(pos);
        moves.push_back(pos);

        if (auto winner = current.is_over()) {
            status = Status::GAME_OVER;
            result = { winner, WinType::SUICIDE };
        }
        if (!current.available_actions().size())
            should_giveup = true;
    }

    void concede(Player player)
    {
        if (status != Status::ON_GOING)
            throw std::logic_error("Contest not started");
        if (players.at(current.role) != player)
            throw std::logic_error(player.name + " not allowed to concede");

        status = Status::GAME_OVER;
        result = { -player.role, WinType::GIVEUP };
    }

    void timeout(Player player)
    {
        if (status != Status::ON_GOING)
            throw std::logic_error("Contest not started");
        if (players.at(current.role) != player)
            throw std::logic_error("not in " + player.name + "'s turn");

        status = Status::GAME_OVER;
        result = { -player.role, WinType::TIMEOUT };
    }
    auto round() const { return moves.size(); }

    auto encode() const -> string
    {
        std::string delimiter = " ";
        std::string terminator = result.win_type == WinType::GIVEUP ? "G"
            : result.win_type == WinType::TIMEOUT                   ? "T"
                                                                    : "";
        auto moves_str = moves | std::views::transform([](auto pos) { return pos.to_string(); });
        return (moves_str | ranges::views::join_with(delimiter) | ranges::to<std::string>())
            + delimiter + terminator;
    }
};