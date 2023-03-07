export module Contest;

import std;
import Rule;

using namespace std;
namespace fs = std::filesystem;
namespace ranges = std::ranges;

/*
// A shortcut to the result type of F(Args...).
template <typename F, typename... Args>
using result_t = std::invoke_result_t<std::decay_t<F>,(std::decay_t<Args>...)>;
*/

// A shortcut to the type of a duration D.
template <typename D>
using duration_t = std::chrono::duration<
    typename D::rep, typename D::period>;

// Run a function asynchronously if timeout is non-zero.
//
// The return value is an optional<result_t>.
// The optional is "empty" if the async execution timed out.
template <typename TO, typename F, typename... Args>
inline auto with_timeout(const TO& timeout, F&& f, Args&&... args)
{
    if (timeout == duration_t<TO>::zero())
        return std::optional { f(args...) };

    // std::printf("launching...\n");
    auto future = std::async(std::launch::async,
        std::forward<F>(f), std::forward<Args...>(args...));
    auto status = future.wait_for(timeout);

    return status == std::future_status::ready
        ? std::optional { future.get() }
        : std::nullopt;
}

template <typename T>
inline std::string to_string(const T& value)
{
    std::ostringstream ss;
    ss << value;
    return ss.str();
}

export class Contest {
public:
    class StonePositionitionOccupiedException : public std::logic_error {
        using logic_error::logic_error;
    };
    class TimeLimitExceededException : public std::runtime_error {
        using runtime_error::runtime_error;
    };

    State current {};
    using PlayerType = function<Position(State)>;
    PlayerType player1, player2;
    int winner { 0 };
    Contest(const PlayerType& player1, const PlayerType& player2)
        : player1(player1)
        , player2(player2)
    {
    }

    int round() const
    {
        return (int)current.moves.size();
    }

    bool play()
    {
        auto&& player { current.role ? player1 : player2 };
        auto p { with_timeout(1000ms, player, current) };
        if (!p) {
            winner = -current.role;
            throw TimeLimitExceededException { to_string(current.role) + " exceeds the time limit." };
        }
        if (current.board[*p]) {
            winner = -current.role;
            throw StonePositionitionOccupiedException { to_string(current.role) + " choose a occupied Positionition." };
        }
        current = current.next_state(*p);
        winner = current.is_over();
        return !winner;
    }
};