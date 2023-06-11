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
    START_LOCAL_GAME_OP = 100000, // 开始一局本地游戏
    UPDATE_UI_STATE_OP, // 更新 UI 状态
    LOCAL_GAME_TIMEOUT_OP, // Deprecated
    LOCAL_GAME_MOVE_OP, // 本地游戏落子（data1 = Position, data2 = Role）
    CONNECT_TO_REMOTE_OP, // 连接到远程服务器
    CONNECT_RESULT_OP, // 返回连接结果
    WIN_PENDING_OP, // 即将胜利
    // -------- Chat --------
    CHAT_SEND_MESSAGE_OP, // 向某人发送消息
    CHAT_SEND_BROADCAST_MESSAGE_OP, // 广播一条消息
    CHAT_RECEIVE_MESSAGE_OP, // 收到消息
    CHAT_USERNAME_UPDATE_OP, // 用户昵称更新
    // -------- Contest Request --------
    SYNC_ONLINE_SETTINGS_OP, // 同步多人游戏设置（data1 = 用户名, data2 = 限时）
    SEND_REQUEST_OP, // 发送对局申请（data1 = Host:Port, data2 = Role）
    SEND_REQUEST_BY_USERNAME_OP, // 发送对局申请（data1 = 用户名, data2 = Role）
    RECEIVE_REQUEST_OP, // 收到新的申请
    ACCEPT_REQUEST_OP, // 接受队列中的第一个申请
    REJECT_REQUEST_OP, // 拒绝队列中的第一个申请
    RECEIVE_REQUEST_RESULT_OP, // 发送的申请收到结果
    // -------- Game Replay --------
    REPLAY_START_MOVE_OP, // 对局回放 开始介入
    REPLAY_MOVE_OP, // 对局回放介入 落子
    REPLAY_STOP_MOVE_OP, // 对局回放退出介入
    // -------- Bot --------
    BOT_HOSTING_OP, // 切换 AI 托管状态
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