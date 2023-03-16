module;

#include <nlohmann/json.hpp>
#include <string_view>

export module nogo.network.data;

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
};

export class Message {
    json j_object_;

public:
    OpCode op;
    string data1;
    string data2;

    Message(OpCode op, string_view data1 = "", string_view data2 = "")
        : j_object_ { { "op", op }, { "data1", data1 }, { "data2", data2 } }
        , op(op)
        , data1(data1)
        , data2(data2)
    {
    }
    Message(string_view msg)
        : j_object_(json::parse(msg))
        , op(j_object_["op"].get<OpCode>())
        , data1(j_object_["data1"].get<string>())
        , data2(j_object_["data2"].get<string>())
    {
    }

    operator string()
    {
        return j_object_.dump();
    }
};