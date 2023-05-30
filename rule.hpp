#pragma once

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>
#include <vector>

#include "utility.hpp"

_EXPORT struct Position {
    int x { -1 }, y { -1 };
    constexpr Position(int x, int y)
        : x(x)
        , y(y)
    {
    }
    constexpr Position() = default;
    constexpr Position operator+(Position p) const
    {
        return { x + p.x, y + p.y };
    }
    constexpr explicit operator bool() const { return x >= 0 && y >= 0; }
    constexpr auto operator<=>(const Position& p) const = default;
    auto to_string() const -> std::string
    {
        return std::string(1, 'A' + x) + std::to_string(y + 1);
    }
    constexpr explicit Position(std::string_view str)
        : Position(str[0] - 'A', stoi(str.substr(1)) - 1)
    {
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Position, x, y)
};

_EXPORT struct Role {
    int id;
    static const Role BLACK, WHITE, NONE;

    constexpr Role()
        : Role(0)
    {
    }

    constexpr decltype(auto) map(auto&& v_black, auto&& v_white, auto&& v_none) const
    {
        return id == 1 ? v_black
            : id == -1 ? v_white
                       : v_none;
    }
    constexpr auto operator<=>(const Role&) const = default;
    constexpr auto operator-() const { return Role(-id); }
    constexpr explicit operator bool() const { return id; }

    auto to_string() const -> std::string
    {
        return map("BLACK", "WHITE", "NONE");
    }

    explicit constexpr Role(std::string_view str)
        : Role(str == "b"    ? 1
                : str == "w" ? -1
                             : 0)
    {
    }

    explicit constexpr operator int() const { return id; }

private:
    constexpr explicit Role(int id)
        : id(id)
    {
    }
};
constexpr Role Role::BLACK { 1 }, Role::WHITE { -1 }, Role::NONE { 0 };

class BoardBase {
    static constexpr std::array delta { Position { -1, 0 }, Position { 1, 0 }, Position { 0, -1 }, Position { 0, 1 } };

protected:
    auto neighbor(Position p) const
    {
        return delta | std::views::transform([&](auto d) { return p + d; })
            | std::views::filter([&](auto p) { return in_border(p); })
            | ranges::to<std::vector>();
    }

public:
    using Board_ptr = std::shared_ptr<BoardBase>;
    auto to_2dvector() const
    {
        auto rank = get_rank();
        std::vector<std::vector<Role>> res;
        res.resize(rank);
        for (int i = 0; i < rank; i++) {
            res[i].resize(rank);
            for (int j = 0; j < rank; j++) {
                res[i][j] = (*this)[{ i, j }];
            }
        }
        return res;
    }

    virtual auto index() -> std::vector<Position> = 0;

    virtual Role operator[](Position p) = 0;
    virtual const Role& operator[](Position p) const = 0;
    virtual auto in_border(Position p) const -> bool = 0;

    virtual bool liberties(Position p) const = 0;
    virtual bool is_capturing(Position p) const = 0;
    virtual auto get_rank() const -> int = 0;
    virtual auto to_string() const -> std::string = 0;
    virtual Board_ptr clone() const = 0;

    friend auto operator<<(std::ostream& os, const BoardBase& board) -> std::ostream&
    {
        os << board.to_string() << std::endl;
        return os;
    }
};

_EXPORT using Board_ptr = std::shared_ptr<BoardBase>;

template <int Rank>
_EXPORT class Board : public BoardBase, std::enable_shared_from_this<Board<Rank>> {
private:
    std::array<Role, Rank * Rank> arr { Role::NONE };

    constexpr auto _liberties(Position p, Board<Rank>& visit) const -> bool
    {
        auto& self { *this };
        visit[p] = Role::BLACK;
        return std::ranges::any_of(neighbor(p), [&](auto n) {
            return !self[n];
        }) || std::ranges::any_of(neighbor(p), [&](auto n) {
            return !visit[n] && self[n] == self[p]
                && _liberties(n, visit);
        });
    }

public:
    Role operator[](Position p) override { return arr[p.x * Rank + p.y]; }
    const Role& operator[](Position p) const override { return arr[p.x * Rank + p.y]; }

    auto index() -> std::vector<Position> override
    {
        std::vector<Position> res;
        res.reserve(Rank * Rank);
        for (int i = 0; i < Rank; i++) {
            for (int j = 0; j < Rank; j++) {
                res.emplace_back(i, j);
            }
        }
        return res;
    }

    bool in_border(Position p) const override { return p.x >= 0 && p.y >= 0 && p.x < Rank && p.y < Rank; }

    bool liberties(Position p) const override
    {
        auto& self { *this };
        Board<Rank> visit {};
        return self._liberties(p, visit);
    }

    bool is_capturing(Position p) const override
    {
        auto& self { *this };
        return !self.liberties(p)
            || std::ranges::any_of(neighbor(p), [&](auto n) {
                   return self[n] == -self[p]
                       && !self.liberties(n);
               });
    }

    auto get_rank() const -> int override { return Rank; }

    auto to_string() const -> std::string override
    {
        auto& self { *this };
        std::string res;
        for (int i = 0; i < Rank; i++) {
            for (int j = 0; j < Rank; j++) {
                res += self[{ i, j }].map("B", "W", "-");
            }
            res += '\n';
        }
        return res;
    }

    Board_ptr clone() const override
    {
        auto& self { *this };
        auto res = std::make_shared<Board<Rank>>(self);
        return res;
    }
};

_EXPORT struct State {
    Board_ptr board {};
    Role role {};
    Position last_move {};

    State(Board_ptr board = std::make_shared<Board<9>>(), Role role = Role::BLACK)
        : board(board)
        , role(role)
    {
    }
    State(Board_ptr board, Role role, Position last_move)
        : board(board)
        , role(role)
        , last_move(last_move)
    {
    }

    auto next_state(Position p) const
    {
        State state { board->clone(), -role, p };
        (*state.board)[p] = role;
        return state;
    }

    auto available_actions() const
    {
        auto index = board->index();
        return index | ranges::views::filter([&](auto pos) {
            return !(*board)[pos] && !next_state(pos).board->is_capturing(pos);
        }) | ranges::to<std::vector>();
    }

    constexpr auto is_over() const
    {
        if (last_move && board->is_capturing(last_move)) // win
            return role;
        /*
        if (!available_actions().size()) // lose
            return -role;
        */
        return Role::NONE;
    }
};