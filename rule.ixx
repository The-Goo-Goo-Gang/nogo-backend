export module nogo.rule;

import std;

export constexpr int rank_n = 9;

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
        Position res = *this;
        return res += p;
    }
    constexpr Position& operator-=(Position p)
    {
        x -= p.x, y -= p.y;
        return *this;
    }
    constexpr Position operator-(Position p) const
    {
        Position res = *this;
        return res -= p;
    }
    constexpr auto operator<=>(const Position& p) const = default;
};

export class Board {
    std::array<int, rank_n * rank_n> arr;

public:
    constexpr int& operator[](Position p) { return arr[p.x * rank_n + p.y]; }
    constexpr int operator[](Position p) const { return arr[p.x * rank_n + p.y]; }

    constexpr bool in_border(Position p) const { return p.x >= 0 && p.y >= 0 && p.x < rank_n && p.y < rank_n; }

    static constexpr auto index()
    {
        std::array<Position, rank_n * rank_n> res;
        for (int i = 0; i < rank_n; i++)
            for (int j = 0; j < rank_n; j++)
                res[i * rank_n + j] = { i, j };
        return res;
    }

    static constexpr std::array delta { Position { -1, 0 }, Position { 1, 0 }, Position { 0, -1 }, Position { 0, 1 } };

    constexpr auto _liberties(Position p, Board& visit) const -> bool
    {
        auto self = *this;
        visit[p] = true;
        return std::ranges::any_of(delta, [self, p, &visit](auto d) constexpr -> bool {
            Position n = p + d;
            return self.in_border(n)
                && (!self[n] || self[n] == self[p] && !visit[n] && self._liberties(n, visit));
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
            || std::ranges::any_of(delta, [p, self](auto d) {
                   Position n = p + d;
                   return self.in_border(n) && self[n] == -self[p]
                       && !self.liberties(n);
               });
    }
};

export struct Role {
    enum Value {
        WHITE = -1,
        BLACK = 1
    };
    constexpr Role(Value value)
        : value(value)
    {
    }
    constexpr operator int() const
    {
        return value;
    }
    constexpr explicit operator bool() const
    {
        return value == BLACK;
    }
    constexpr void reverse()
    {
        auto& self = *this;
        self = self ? WHITE : BLACK;
    }
    friend std::ostream& operator<<(std::ostream& out, Role role)
    {
        return out << (role ? "BLACK" : "WHITE");
    }

private:
    Value value;
};

export class State {
public:
    Board board {};
    std::vector<Position> moves {};
    Role role;

    constexpr State(Role role = Role::BLACK)
        : role(role)
    {
    }

    constexpr State next_state(Position p) const
    {
        // assert(!board[p]);
        auto self = *this;

        auto state { self };
        state.board[p] = state.role;
        state.moves.push_back(p);
        state.role.reverse();
        return state;
    }

    constexpr Position revoke()
    {
        if (!moves.size())
            return {};

        auto p { moves.back() };
        moves.pop_back();
        board[p] = 0;
        role.reverse();
        return p;
    }

    constexpr int is_over() const
    {
        if (moves.size() && board.is_capturing(moves.back())) // win
            return role;
        if (!available_actions().size()) // lose
            return -role;
        return 0;
    }
    constexpr std::vector<Position> available_actions() const
    {
        auto temp_board { board };
        return Board::index() | std::ranges::views::filter([&temp_board, this](auto Position) {
            if (temp_board[Position])
                return false;
            temp_board[Position] = this->role;
            bool res { !temp_board.is_capturing(Position) };
            temp_board[Position] = 0;
            return res;
        }) | std::ranges::to<std::vector>();
    }
};