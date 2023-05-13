#pragma once

#include <range/v3/all.hpp>
namespace ranges::views {
auto join_with = join;
};

#ifndef _EXPORT
#define _EXPORT
#endif

template <typename T>
constexpr inline auto integer_cast(std::string_view str)
{
    T result;
    auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
    switch (ec) {
    case std::errc::invalid_argument:
        throw std::invalid_argument { "no conversion" };
    case std::errc::result_out_of_range:
        throw std::out_of_range { "out of range" };
    default:
        return result;
    };
}
constexpr auto stoi(std::string_view str)
{
    return integer_cast<int>(str);
}
constexpr auto stoull(std::string_view str)
{
    return integer_cast<unsigned long long>(str);
}