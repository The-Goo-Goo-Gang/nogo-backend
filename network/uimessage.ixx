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
        string id,name,value;
    };
    enum class PlayerType{
        LocalHumanPlayer,
        OnlineHumanPlayer,
        BotPlayer,
    };
    struct Player{
        string name,avatar;
        PlayerType type;
        int chess_type;
    };
    struct GameMetadata{
        int size;
        Player player_opposing, player_our;
        int turn_timeout;
    };
    struct Game{
        std::array<std::array<int>> chessboard;
        bool is_our_player_playing;
        GameMetadata gamemetadata;
        std::array<DynamicStatistics> statistics;
    };
    struct UiState{
        json data;
        bool is_gaming;
        Game game;
    };

  private:
    json data;
    OpCode op;
    time_t data1;
    UiState data2;
    const Contest& contest;
    void MakePlayerMessage(Player& player_front,const Player& player_back){
        player_front.name = player_back.name;
        player_front.avatar = NULL;
        player_front.type = NULL;
        player_front.chess_type = player_back.role.id;
    }
  public:
    void MakeMessage()
    {
        void* ptr;
        op = OpCode::UPDATE_UI_STATE_OP;
        data1 = std::time(0);
        ptr = &data2;
        ptr -> is_gaming = (contest.status == Contest::Status::ON_GOING);
        if(!(ptr -> is_gaming)){
            data = json{ { "is_gaming", ptr->is_gaming}, { "game", NULL} } ;
        }
        else{
            data = json{ { "is_gaming", ptr->is_gaming}, { "game", ptr -> game} } ;
            ptr = &(ptr -> game);
            int rank_n = 9;
            for(int i = 0; i < rank_n; i++) for(int j = 0; j < rank_n; j++){
                ptr -> chessboard[i][j] = (bool)(contest.current.board.arr[Position{i,j}]);
            }
            ptr -> is_our_player_playing = (contest.current.role == contest.players.player1.role);
            ptr -> statistics.clear();
            ptr = &(ptr -> gamemetadata);
            ptr -> size = rank_n;
            ptr -> turn_timeout = NULL;
            MakePlayerMessage(ptr -> player_opposing, contest.players.player2);
            MakePlayerMessage(ptr -> player_our, contest.players.player1);
        }
        data = json{ { "op", op }, { "data1", to_string (data1) }, { "data2", data2.data.dump() } };
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
};