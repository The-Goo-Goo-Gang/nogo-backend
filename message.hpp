#pragma once
#define export

#include <nlohmann/json.hpp>
#include <string_view>

using nlohmann::json;
using std::string;
using std::string_view;

export enum class OpCode : int {
    READY_OP = 200000,
    REJECT_OP,
    MOVE_OP,
    GIVEUP_OP,
    TIMEOUT_END_OP,
    SUICIDE_END_OP,
    GIVEUP_END_OP,
    LEAVE_OP,
    CHAT_OP,
    //-----Extend OpCode ------------
    START_LOCAL_GAME_OP = 100000,
    UPDATE_UI_STATE_OP,
    // TODO: Only for temporary use, need to be removed in stage 2
    LOCAL_GAME_TIMEOUT_OP
};

export enum class PlayerType {
    LOCAL_HUMAN_PLAYER,
    REMOTE_HUMAN_PLAYER,
    BOT_PLAYER,
};

export struct Message {
    OpCode op;
    string data1;
    string data2;

    Message() = default;
    Message(OpCode op, string_view data1 = "", string_view data2 = "")
        : op(op)
        , data1(data1)
        , data2(data2)
    {
    }
    Message(string_view sv)
    {
        from_json(json::parse(sv), *this);
    }
    auto to_string() -> string
    {
        return json(*this).dump();
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Message, op, data1, data2)
};