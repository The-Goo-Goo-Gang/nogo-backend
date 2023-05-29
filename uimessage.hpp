#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <string_view>
#include <vector>

#include "contest.hpp"
#include "message.hpp"
#include "rule.hpp"
#include "utility.hpp"

using nlohmann::json;
using std::string;
using std::string_view;

namespace nlohmann {
template <class T>
void to_json(nlohmann::json& j, const std::optional<T>& v)
{
    if (v.has_value())
        j = *v;
    else
        j = nullptr;
}
template <class T>
void from_json(const nlohmann::json& j, std::optional<T>& v)
{
    if (j.is_null())
        v = std::nullopt;
    else
        v = j.get<T>();
}
}

_EXPORT struct UiMessage : public Message {
    struct DynamicStatistics {
        string id, name, value;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(DynamicStatistics, id, name, value)
    };
    struct PlayerData {
        string name, avatar;
        PlayerType type;
        int chess_type;
        PlayerData() = default;
        PlayerData(const Player& player)
            : name(player.name)
            , type(PlayerType::LOCAL_HUMAN_PLAYER)
            , chess_type(player.role.id)
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerData, name, avatar, type, chess_type)
    };
    struct GameMetadata {
        int size;
        PlayerData player_opposing, player_our;
        int turn_timeout;
        GameMetadata() = default;
        GameMetadata(const Contest& contest)
            : size(contest.board_size)
            , player_opposing(PlayerData(contest.players.at(-contest.local_role)))
            , player_our(PlayerData(contest.players.at(contest.local_role)))
            , turn_timeout(contest.duration.count())
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameMetadata, size, player_opposing, player_our, turn_timeout)
    };
    struct GameResult {
        int winner;
        Contest::WinType win_type;
        GameResult() = default;
        GameResult(const Contest& contest)
            : winner(contest.result.winner.id)
            , win_type(contest.result.win_type)
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameResult, winner, win_type)
    };
    struct Game {
        std::vector<std::vector<int>> chessboard;
        int now_playing;
        int move_count;
        long long start_time;
        long long end_time;
        std::optional<Position> last_move;
        std::vector<Position> disabled_positions;
        GameMetadata metadata;
        std::vector<DynamicStatistics> statistics;
        std::string encoded;
        bool is_replaying;
        Game() = default;
        Game(const Contest& contest)
            : now_playing(contest.current.role.id)
            , move_count(contest.round())
            , metadata(GameMetadata(contest))
            , last_move(contest.moves.empty() ? std::nullopt : std::optional<Position>(contest.moves.back()))
            , start_time { std::chrono::duration_cast<std::chrono::milliseconds>(contest.start_time.time_since_epoch()).count() }
            , end_time { contest.status == Contest::Status::GAME_OVER ? std::chrono::duration_cast<std::chrono::milliseconds>(contest.end_time.time_since_epoch()).count() : 0 }
            , encoded { contest.encode() }
            , is_replaying(contest.is_replaying)
        {
            auto rank = contest.current.board->get_rank();
            auto actions = contest.current.available_actions();
            auto index = BoardBase::index(contest.current.board->get_rank());
            disabled_positions = index
                | ranges::views::filter([&](auto pos) { return !(*contest.current.board)[pos] && std::find(actions.begin(), actions.end(), pos) == actions.end(); })
                | ranges::to<std::vector>();
            const auto board = contest.current.board->to_2dvector();
            chessboard.resize(rank);
            for (int i = 0; i < rank; ++i) {
                chessboard[i].resize(rank);
                for (int j = 0; j < rank; ++j) {
                    chessboard[i][j] = board[i][j].id;
                }
            }
        }

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Game, is_replaying, chessboard, now_playing, move_count, metadata, statistics, disabled_positions, last_move, start_time, end_time, encoded)
    };
    struct UiState {
        bool is_gaming;
        Contest::Status status;
        std::optional<Game> game;
        GameResult game_result;
        UiState(const Contest& contest)
            : is_gaming(contest.status == Contest::Status::ON_GOING)
            , status(contest.status)
            , game(contest.status != Contest::Status::NOT_PREPARED ? std::optional<Game>(contest) : std::nullopt)
            , game_result(GameResult(contest))
        {
        }
        auto to_string() -> string
        {
            return json(*this).dump();
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(UiState, is_gaming, status, game, game_result)
    };
    UiMessage(const Contest& contest)
        : Message(OpCode::UPDATE_UI_STATE_OP, std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()), UiState(contest).to_string())
    {
    }
};