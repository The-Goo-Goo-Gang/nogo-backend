module;

#include <nlohmann/json.hpp>
#include <string_view>
#include <array>
#include <ctime>

export module nogo.network.uimessage;

export import nogo.rule;
export import nogo.contest;
export import nogo.network.data;

using nlohmann::json;
using std::string;
using std::string_view;

export class UiMessage {
  public:
    struct DynamicStatistics{
        array<json> data;
        array<string> id,name,value;
    };
    enum class PlayerType{
        LocalHumanPlayer,
        OnlineHumanPlayer,
        BotPlayer,
    };
    struct Player{
        json data;
        string name,avatar;
        PlayerType type;
        int chess_type;
    };
    struct GameMetadata{
        json data;
        int size;
        struct Player player_opposing, player_our;
        int turn_timeout;
    };
    struct Game{
        json data;
        array<array<int>> chessboard;
        bool is_our_player_playing;
        struct GameMetadata gamemetadata;
        DynamicStatistics statistics;
    };
    struct UiState{
        json data;
        bool is_gaming;
        struct Game game;
    };

  private:
    json data;
    OpCode op;
    time_t data1;
    struct UiState data2;
    const Contest& contest;
    void MakePlayerMessage(auto& player_front,const auto& player_back){
        player_front.name = player_back.name;
        player_front.avatar = NULL;
        player_front.type = NULL;
        player_front.type = (bool)player_back.role;
    }
  public:
    void MakeMessage()
    {
        void* ptr;
        op = OpCode::UPDATE_UI_STATE_OP;
        data1 = std::time(0);
        data = json{ { "op", op }, { "data1", data1 }, { "data2", data2.data } };
        ptr = &data2;
        ptr -> is_gaming = (contest.status == Contest::Status::ON_GOING);
        if(!ptr -> is_gaming){
            ptr -> data = json{ { "is_gaming", ptr -> is_gaming }, { "game", NULL } };
            return;
        }
        else{
            ptr -> data = json{ { "is_gaming", ptr -> is_gaming }, { "game", ptr -> game.data } };
            ptr = &(ptr -> game);
            int rank_n = 9;
            for(int i = 0; i < rank_n; i++) for(int j = 0; j < rank_n; j++){
                ptr -> chessboard[i][j] = (bool)(contest.current.board.arr[Position{i,j}]);
            }
            ptr -> is_our_player_playing = (contest.current.role == contest.players.player1.role);
            ptr -> data = json{ { "chessboard", ptr -> chessboard }, 
                                { "is_our_player_playing", ptr -> is_our_player_playing},
                                { "gamemetadata", ptr -> gamemetadata.data},
                                { "statistics", ptr -> statistics.data}};
            ptr -> statistics.data.clear();
            ptr = &(ptr -> gamemetadata);
            ptr -> size = rank_n;
            ptr -> turn_timeout = NULL;
            ptr -> data = json{ { "size", ptr -> size },
                                { "player_opposing", ptr -> player_opposing.data }
                                { "player_our", ptr -> player_our.data}
                                { "turn_timeout", ptr -> turn_timeout}};
            MakePlayerMessage(ptr -> player_opposing, contest.players.player2);
            MakePlayerMessage(ptr -> player_our, contest.players.player1);
        }
    }
    Message(const Contest& _contest)
        : contest(_contest)
    {
        MakeMessage();
    }
    operator string()
    {
        return data.dump();
    }
};