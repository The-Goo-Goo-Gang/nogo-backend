#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <asio/ip/tcp.hpp>
using asio::ip::tcp;

#include "log.hpp"
#include "message.hpp"
#include "rule.hpp"
#include "utility.hpp"

using namespace std::chrono_literals;
constexpr auto TIMEOUT { 30s };

class Participant;
using Participant_ptr = std::shared_ptr<Participant>;

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
    friend std::ostream& operator<<(std::ostream& os, const Player& player)
    {
        return os << fmt::format("ip:{}, name:{}, role:{}, type:{}",
                   to_string(*player.participant), player.name, player.role.to_string(), std::to_underlying(player.type));
    }
};

class PlayerList {
    std::vector<Player> players;

public:
    class PlayerExistException : public std::logic_error {
        using std::logic_error::logic_error;
    };
    class PlayerListFullException : public std::logic_error {
        using std::logic_error::logic_error;
    };
    class RoleOccupiedException : public std::logic_error {
        using std::logic_error::logic_error;
    };
    auto to_string() const
    {
        return players | ranges::views::transform([](auto& p) { return ::to_string(p); })
            | ranges::views::join_with(';') | ranges::to<std::string>();
    }
    auto find(Role role)
    {
        auto it = std::ranges::find_if(players, [&](auto& p) {
            return p.role == role;
        });
        return it == players.end() ? nullptr : std::addressof(*it);
    }
    auto find(Role role) const
    {
        return static_cast<const Player*>(const_cast<PlayerList*>(this)->find(role));
    }

    auto at(Role role) -> Player&
    {
        auto it = find(role);
        if (!it)
            throw std::logic_error { "Player not found" };
        return static_cast<Player&>(*it);
    }
    auto at(Role role) const
    {
        return static_cast<const Player&>(const_cast<PlayerList*>(this)->at(role));
    }

    auto contains(Role role) const
    {
        return find(role) != nullptr;
    }
    auto insert(Player&& player)
    {
        if (std::ranges::find(players, player) != players.end())
            throw PlayerExistException { "Player already in list" };
        if (player.role == Role::NONE) {
            if (contains(Role::BLACK)) {
                logger->info("role black occupied, so guess role: white");
                player.role = Role::WHITE;
            } else if (contains(Role::WHITE)) {
                logger->info("role white occupied, so guess role: black");
                player.role = Role::BLACK;
            } else
                throw PlayerListFullException { "No role for player" };
        }
        if (contains(player.role))
            throw RoleOccupiedException { "Role already occupied" };

        logger->info("Insert player: {}", ::to_string(player));
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
        bool confirmed;
    };
    class TimeLimitExceededException : public std::runtime_error {
        using runtime_error::runtime_error;
    };
    class StatusError : public std::logic_error {
        using std::logic_error::logic_error;
    };

    bool should_giveup {};

    State current {};
    std::vector<Position> moves;
    PlayerList players;

    Status status {};
    GameResult result {};
    std::chrono::seconds duration { TIMEOUT };
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    Role local_role { Role::NONE };

    Contest() = default;
    Contest(PlayerList players)
        : players(std::move(players))
    {
    }

    void clear()
    // only keep players
    {
        current = {};
        moves.clear();
        status = {};
        result = {};
        should_giveup = false;
        local_role = Role::NONE;
    }
    void confirm()
    {
        result.confirmed = true;
    }
    void reject()
    {
        if (status != Status::NOT_PREPARED)
            throw StatusError { "Contest already started" };
        players = {};
    }

    void enroll(Player&& player)
    {
        if (status != Status::NOT_PREPARED)
            throw StatusError { "Contest already started" };
        players.insert(std::move(player));
        if (players.contains(Role::BLACK) && players.contains(Role::WHITE)) {
            status = Status::ON_GOING;
            start_time = std::chrono::system_clock::now();
        }
    }

    void play(Player player, Position pos)
    {
        if (status != Status::ON_GOING)
            throw StatusError { "Contest not started" };
        if (current.role != player.role)
            throw std::logic_error { player.name + " not allowed to play" };
        if (current.board[pos]) {
            status = Status::GAME_OVER;
            result = { -player.role, WinType::SUICIDE };
            logger->warn("Play on occupied position {}, playerdata: {}", pos.to_string(), to_string(player));
            return;
        }
        logger->info("contest play " + std::to_string(pos.x) + ", " + std::to_string(pos.y));
        current = current.next_state(pos);
        moves.push_back(pos);

        if (auto winner = current.is_over()) {
            status = Status::GAME_OVER;
            result = { winner, WinType::SUICIDE };
            end_time = std::chrono::system_clock::now();
        }
        if (!current.available_actions().size())
            should_giveup = true;
    }

    void concede(Player player)
    {
        if (status != Status::ON_GOING)
            throw StatusError { "Contest not started" };
        if (players.at(current.role) != player)
            throw std::logic_error { player.name + " not allowed to concede" };
        status = Status::GAME_OVER;
        result = { -player.role, WinType::GIVEUP };
        end_time = std::chrono::system_clock::now();
    }

    void timeout(Player player)
    {
        if (status != Status::ON_GOING)
            throw StatusError { "Contest not started" };
        if (players.at(current.role) != player)
            throw std::logic_error { "not in " + player.name + "'s turn" };
        status = Status::GAME_OVER;
        result = { -player.role, WinType::TIMEOUT };
        end_time = std::chrono::system_clock::now();
    }

    auto round() const -> int { return moves.size(); }

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