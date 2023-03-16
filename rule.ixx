module;

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

export module nogo.rule;

export constexpr inline auto rank_n = 9;

export struct Position {
    int x, y;
    constexpr Position(int x, int y)
        : x(x)
        , y(y)
    {
    }
    constexpr Position(int x)
        : Position(x, x)
    {
    }

    constexpr Position()
        : Position(-1)
    {
    }

    constexpr Position& operator+=(Position p)
    {
        x += p.x, y += p.y;
        return *this;
    }
    constexpr Position operator+(Position p) const
    {
        auto res = *this;
        return res += p;
    }
    constexpr Position& operator-=(Position p)
    {
        x -= p.x, y -= p.y;
        return *this;
    }
    constexpr Position operator-(Position p) const
    {
        auto res = *this;
        return res -= p;
    }

    constexpr operator bool() const { return x >= 0 && y >= 0; }
    constexpr auto operator<=>(const Position& p) const = default;
};

export class Role {
    int id;
    constexpr explicit Role(int id)
        : id(id)
    {
    }

public:
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
    constexpr auto operator<=>(const Role&) const = default;
    constexpr auto operator-() const { return Role(-id); }
    constexpr operator bool() { return id; }
};
constexpr Role Role::BLACK { 1 }, Role::WHITE { -1 }, Role::NONE { 0 };

export class Board {
    std::array<Role, rank_n * rank_n> arr;

    // static method with module causes ICE, fuck Visual C++!
    constexpr auto neighbor(Position p) const
    {
        constexpr std::array delta { Position { -1, 0 }, Position { 1, 0 }, Position { 0, -1 }, Position { 0, 1 } };
        return delta | std::views::transform([&](auto d) { return p + d; });
    }

public:
    // constexpr auto operator[](this auto&& p) { return arr[p.x * rank_n + p.y]; }
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

    constexpr auto _liberties(Position p, Board& visit) const -> bool
    {
        auto self = *this;
        visit[p] = Role::BLACK;
        return std::ranges::any_of(neighbor(p), [&](auto n) constexpr {
            return self.in_border(n) && !self[n];
        }) || std::ranges::any_of(neighbor(p), [&](auto n) constexpr {
            return self.in_border(n) && !visit[n] && self[n] == self[p]
                && _liberties(n, visit);
        });
    };
    constexpr bool liberties(Position p) const
    {
        auto self = *this;
        Board visit {};
        return self._liberties(p, visit);
    }

    // judge whether stones around `p` is captured by `p`
    // or `p` is captured by stones around `p`
    constexpr bool is_capturing(Position p) const
    {
        // assert(self[p]);

        auto self = *this;
        return !self.liberties(p)
            || std::ranges::any_of(neighbor(p), [&](auto n) {
                   return self.in_border(n) && self[n] == -self[p]
                       && !self.liberties(n);
               });
    }
};

export struct State {
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
        return Board::index() | std::ranges::views::filter([&](auto pos) -> bool {
            return !board[pos] && !next_state(pos).board.is_capturing(pos);
        }) | ranges::to<std::vector>();
    }

    constexpr auto is_over() const
    {
        if (last_move && board.is_capturing(last_move)) // win
            return role;
        if (!available_actions().size()) // lose
            return -role;
        return Role::NONE;
    }
};