#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

#include "utility.hpp"

using nlohmann::json;
using std::string;
using std::string_view;

_EXPORT enum class OpCode : int {
    READY_OP = 200000,
    REJECT_OP,
    MOVE_OP,
    GIVEUP_OP,
    TIMEOUT_END_OP,
    SUICIDE_END_OP,
    GIVEUP_END_OP,
    LEAVE_OP,
    CHAT_OP,
    // -------- Extend OpCode --------
    START_LOCAL_GAME_OP = 100000,
    UPDATE_UI_STATE_OP,
    // Deprecated
    LOCAL_GAME_TIMEOUT_OP,
    LOCAL_GAME_MOVE_OP,
    CONNECT_TO_REMOTE_OP,
    CONNECT_RESULT_OP,
    WIN_PENDING_OP,
    // -------- Chat --------
    CHAT_SEND_MESSAGE_OP,
    CHAT_SEND_BROADCAST_MESSAGE_OP,
    CHAT_RECEIVE_MESSAGE_OP,
    CHAT_USERNAME_UPDATE_OP,
    // -------- Contest Request --------
    UPDATE_USERNAME_OP,
    SEND_REQUEST_OP,
    SEND_REQUEST_BY_USERNAME_OP,
    RECEIVE_REQUEST_OP,
    ACCEPT_REQUEST_OP,
    REJECT_REQUEST_OP,
    RECEIVE_REQUEST_RESULT_OP,
    // -------- Game Replay --------
    REPLAY_START_MOVE_OP,
    REPLAY_MOVE_OP,
    REPLAY_STOP_MOVE_OP,
    // -------- Extend OpCode End --------
};

_EXPORT enum class PlayerType {
    LOCAL_HUMAN_PLAYER,
    REMOTE_HUMAN_PLAYER,
    BOT_PLAYER,
};

_EXPORT struct Message {
    OpCode op;
    string data1, data2;

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