#pragma once
#define export

#include <nlohmann/json.hpp>
#include <string_view>
#include <array>
#include <ctime>

#include "rule.hpp"
#include "contest.hpp"
#include "message.hpp"

using nlohmann::json;
using std::string;
using std::string_view;

namespace nlohmann {
	template <struct T>
	void to_json(nlohmann::json& j, const std::optional<T>& v)
	{
		if (v.has_value())
			j = *v;
		else
			j = nullptr;
	}
}
export constexpr inline auto rank_n = 9;

export class UiMessage {
  public:
    struct DynamicStatistics{
        string id,name,value;
        DynamicStatistics(){

        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(DynamicStatistics, id, name, value)
    };
    enum class PlayerType{
        LocalHumanPlayer,
        OnlineHumanPlayer,
        BotPlayer,
    };
    struct PlayerData{
        string name,avatar;
        PlayerType type;
        int chess_type;
        PlayerData(Player player):
            name(player.name),
            avatar(""),
            type(PlayerType::LocalHumanPlayer),
            chess_type(player.role.id)
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerData, name, avatar, type, chess_type)
    };
    struct GameMetadata{
        int size;
        PlayerType player_opposing, player_our;
        int turn_timeout;
        GameMetadata(const Contest& contest):
            size(rank_n),
            player_opposing(PlayerType(contest.players.player2)),
            player_our(PlayerType(contest.players.player1)),
            turn_timeout(0)
        {
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameMetadata, size, player_opposing, player_our, turn_timeout)
    };
    struct Game{
        std::array<std::array<int>> chessboard;
        bool is_our_player_playing;
        GameMetadata gamemetadata;
        std::array<DynamicStatistics> statistics;
        Game(const Contest& contest):
            is_our_player_playing(contest.current.role == contest.players.player1.role);
            gamemetadata(GameMetadata(contest));
        {
            for(int i = 0; i < rank_n; i++) for(int j = 0; j < rank_n; j++)
                chessboard[i][j] = contest.current.board.arr[Position{i,j}].id;
        }
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Game, chessboard, is_our_player_playing, gamemetadata, statistics);
    };
    struct UiState{
        json data;
        bool is_gaming;
        Game game;
        UiState(const Contest& contest):
            is_gaming(contest.status == Contest::Status::ON_GOING);
        {
            if(data2.is_gaming){
                game = Game(contest);
            }
            data = json{{"is_gaming",is_gaming},{"game",game}};
        }
    };
    void MakeMessage()
    {
        op = OpCode::UPDATE_UI_STATE_OP;
        data1 = std::time(0);
        data2 = UiState(contest);
        data = json{{"op", op},{"data1",to_string(data1)},{"data2",data2.data.dump()}};
    }    
    UiMessage(const Contest& _contest)
        : contest(_contest)
    {
    }
    operator string()
    {
        MakeMessage();
        return data.dump();
    }
  private:
    json data;
    OpCode op;
    time_t data1;
    UiState data2;
    const Contest& contest;
};