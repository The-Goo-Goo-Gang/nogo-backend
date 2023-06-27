#pragma once

#include <range/v3/all.hpp>
namespace ranges::views {
auto join_with = join;
};

#ifndef _EXPORT
#define _EXPORT
#endif

#include <charconv>
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
constexpr inline auto stoi(std::string_view str)
{
    return integer_cast<int>(str);
}
constexpr inline auto stoull(std::string_view str)
{
    return integer_cast<unsigned long long>(str);
}

#include <sstream>
template <typename T>
constexpr inline auto lexical_cast(const auto& v) -> T
{
    std::stringstream ss;
    T r;
    ss << v;
    ss >> r;
    return r;
}

constexpr inline auto to_string(const auto& v)
{
    return lexical_cast<std::string>(v);
}

#ifdef _WIN32
constexpr inline auto operator"" uZ(unsigned long long val) -> size_t
{
    return val;
}
#endif