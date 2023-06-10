#include ".\bot.hpp "
#include <functional>

#ifndef CPU_ONLY
#define CPU_ONLY
#endif
#include <caffe/caffe.hpp>

using namespace std;

int Test(int times, std::function<std::optional<Position>(const State&)> BlackBot, std::function<std::optional<Position>(const State&)> WhiteBot)
{
    int succeed = { 0 };
    for (int i = 0; i < times; i++) {
        State state = {};
        while (state.available_actions().size() && !state.is_over()) {
            // cout << state.board->to_string();
            std::optional<Position> tempp;
            if (state.role == Role::BLACK) {
                tempp = *BlackBot(state);
            } else {
                tempp = *WhiteBot(state);
            }
            if (tempp.has_value()) {
                Position p = tempp.value();
                state = state.next_state(p);
            } else {
                break;
            }
        }
        if (state.is_over()) {
            if (state.role == Role::BLACK) {
                succeed++;
            }
        } else {
            if (state.role == Role::WHITE) {
                succeed++;
            }
        }
    }
    cout << "black : white =" << succeed << ":" << times - succeed << endl;
    return succeed;
}

int main()
{
    Test(2, mcts_bot_player_generator(0), mcts_bot_player_generator(0.1));
    Test(2, mcts_bot_player_generator(0.1), mcts_bot_player_generator(0));
    return 0;
}