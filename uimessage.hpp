#pragma once
#define export

#include <array>
#include <ctime>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <vector>

#include "contest.hpp"
#include "message.hpp"
#include "rule.hpp"

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

export struct UiMessage : public Message {
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
        GameMetadata(const PlayerCouple& players)
            : size(rank_n)
            , player_opposing(PlayerData(players.player2))
            , player_our(PlayerData(players.player1))
            , turn_timeout(0)
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameMetadata, size, player_opposing, player_our, turn_timeout)
    };
    struct GameResult {
        int winner;
        Contest::WinType win_type;
        GameResult() = default;
        GameResult(const Contest& contest)
            : winner(contest.winner.id)
            , win_type(contest.win_type)
            {
            }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameResult, winner, win_type)
    };
    struct Game {
        std::array<std::array<int, rank_n>, rank_n> chessboard;
        bool is_our_player_playing;
        GameMetadata metadata;
        std::vector<DynamicStatistics> statistics;
        Game() = default;
        Game(const Contest& contest)
            : is_our_player_playing(contest.current.role == contest.players.player1.role)
            , metadata(GameMetadata(contest.players))
        {
            const auto board = contest.current.board.to_2darray();
            for (int i = 0; i < rank_n; ++i)
                for (int j = 0; j < rank_n; ++j)
                    chessboard[i][j] = board[i][j].id;
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Game, chessboard, is_our_player_playing, metadata, statistics)
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
        : Message(OpCode::UPDATE_UI_STATE_OP, std::to_string(std::time(0)), UiState(contest).to_string())
    {
    }
};