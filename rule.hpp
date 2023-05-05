#pragma once
#ifndef _EXPORT
#define _EXPORT
#endif 

#include <algorithm>
#include <array>
#include <iostream>
#include <ranges>
#include <vector>

#ifdef __GNUC__
#include <range/v3/all.hpp>
#else
namespace ranges = std::ranges;
#endif

_EXPORT constexpr inline auto rank_n = 9;

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
    // constexpr auto operator<=>(const Position& p) const = default;
    friend auto operator<<(std::ostream& os, Position p) -> std::ostream&
    {
        return os << '(' << p.x << ", " << p.y << ')';
    }
};

_EXPORT struct Role {
    int id;
    static const Role BLACK, WHITE, NONE;

    constexpr Role()
        : Role(0)
    {
    }

    constexpr decltype(auto) map(auto&& v_black, auto&& v_white) const
    {
        return id == 1 ? v_black : id == -1 ? v_white
                                            : throw std::runtime_error("invalid role");
    }
    constexpr decltype(auto) map(auto&& v_black, auto&& v_white, auto&& v_none) const
    {
        return id == 1 ? v_black : id == -1 ? v_white
                                            : v_none;
    }
    constexpr auto operator<=>(const Role&) const = default;
    constexpr auto operator-() const { return Role(-id); }
    constexpr explicit operator bool() { return id; }

private:
    constexpr explicit Role(int id)
        : id(id)
    {
    }
};
constexpr Role Role::BLACK { 1 }, Role::WHITE { -1 }, Role::NONE { 0 };

_EXPORT class Board {
    std::array<Role, rank_n * rank_n> arr;

    static constexpr std::array delta { Position { -1, 0 }, Position { 1, 0 }, Position { 0, -1 }, Position { 0, 1 } };
    auto neighbor(Position p) const
    {
        return delta | std::views::transform([&](auto d) { return p + d; })
            | std::views::filter([&](auto p) { return in_border(p); })
            | ranges::to<std::vector>();
    }

public:
    // constexpr auto operator[](this auto&& self, Position p) { return self.arr[p.x * rank_n + p.y]; }
    constexpr auto operator[](Position p) -> Role& { return arr[p.x * rank_n + p.y]; }
    constexpr auto operator[](Position p) const { return arr[p.x * rank_n + p.y]; }

    constexpr bool in_border(Position p) const { return p.x >= 0 && p.y >= 0 && p.x < rank_n && p.y < rank_n; }

    static constexpr auto index()
    {
        std::array<Position, rank_n * rank_n> res;
        for (int i = 0; i < rank_n; i++)
            for (int j = 0; j < rank_n; j++)
                res[i * rank_n + j] = { i, j };
        return res;
    }

    auto _liberties(Position p, Board& visit) const -> bool
    {
        auto& self { *this };
        visit[p] = Role::BLACK;
        return std::ranges::any_of(neighbor(p), [&](auto n) {
            return !self[n];
        }) || std::ranges::any_of(neighbor(p), [&](auto n) {
            return !visit[n] && self[n] == self[p]
                && _liberties(n, visit);
        });
    };
    bool liberties(Position p) const
    {
        auto& self { *this };
        Board visit {};
        return self._liberties(p, visit);
    }

    // judge whether stones around `p` is captured by `p`
    // or `p` is captured by stones around `p`
    bool is_capturing(Position p) const
    {
        // assert(self[p]);

        auto& self { *this };
        return !self.liberties(p)
            || std::ranges::any_of(neighbor(p), [&](auto n) {
                   return self[n] == -self[p]
                       && !self.liberties(n);
               });
    }

    constexpr auto to_2darray() const
    {
        std::array<std::array<Role, rank_n>, rank_n> res;
        for (int i = 0; i < rank_n; i++)
            for (int j = 0; j < rank_n; j++)
                res[i][j] = arr[i * rank_n + j];
        return res;
    }

    friend auto operator<<(std::ostream& os, const Board& board) -> std::ostream&
    {
        auto arr = board.to_2darray();
        for (int i = 0; i < rank_n; i++) {
            for (int j = 0; j < rank_n; j++)
                os << arr[i][j].map("B", "W", "-");
            os << std::endl;
        }
        return os;
    }
};

_EXPORT struct State {
    Board board;
    Role role;
    Position last_move;

    constexpr State(Role role = Role::BLACK)
        : role(role)
    {
    }
    State(Board board, Role role, Position last_move)
        : board(board)
        , role(role)
        , last_move(last_move)
    {
    }

    auto next_state(Position p) const
    {
        State state { board, -role, p };
        state.board[p] = role;
        return state;
    }

    auto available_actions() const
    {
        auto index = Board::index();
        return index | ranges::views::filter([&](auto pos) {
            return !board[pos] && !next_state(pos).board.is_capturing(pos);
        }) | ranges::to<std::vector>();
    }

    constexpr auto is_over() const
    {
        if (last_move && board.is_capturing(last_move)) // win
            return role;
        /*
        if (!available_actions().size()) // lose
            return -role;
        */
        return Role::NONE;
    }
};