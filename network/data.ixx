module;

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

export module network.data;

using nlohmann::json;

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

export class invalid_message : public std::runtime_error {
    using runtime_error::runtime_error;
};

export struct NetworkData {
    OPCODE op;
    std::string data1;
    std::string data2;

    NetworkData(std::string message)
    {
        try {
            json doc = json::parse(message);
            this->op = static_cast<OPCODE>(doc["op"].get<int>());
            this->data1 = doc["data1"].get<std::string>();
            this->data2 = doc["data2"].get<std::string>();
        } catch (json::parse_error& e) {
            throw invalid_message(message);
        }
    }
    std::string encode() const
    {
        auto self = *this;
        nlohmann::json j_object = {
            { "op", static_cast<int>(self.op) },
            { "data1", self.data1 },
            { "data2", self.data2 }
        };
        return j_object.dump() + "\n";
    }
};