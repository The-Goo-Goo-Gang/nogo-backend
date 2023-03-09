module;

#include <nlohmann/json.hpp>
#include <string>

export module nogo.network.data;

using nlohmann::json;
using std::string;

export enum class OPCODE : int {
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

export class message {
    json j_object_;

public:
    OPCODE op;
    string data1;
    string data2;

    message(OPCODE op, string data1, string data2)
        : j_object_ { { "op", op }, { "data1", data1 }, { "data2", data2 } }
        , op(op)
        , data1(data1)
        , data2(data2)
    {
    }
    message(string msg)
        : j_object_(json::parse(msg))
        , op(static_cast<OPCODE>(j_object_["op"].get<int>()))
        , data1(j_object_["data1"].get<string>())
        , data2(j_object_["data2"].get<string>())
    {
    }

    operator string()
    {
        return j_object_.dump();
    }
};