/**
 * Copyright (c) 2011-2022 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LIBBITCOIN_SYSTEM_CONSTRAINTS_HPP
#define LIBBITCOIN_SYSTEM_CONSTRAINTS_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>
#include <bitcoin/system/constants.hpp>
#include <bitcoin/system/define.hpp>

namespace libbitcoin {
namespace system {

// C++11: std::conditional
// C++14: std::conditional_t
// C++17: std::conjunction/conjunction_v
// C++17: std::disjunction/disjunction_v
// C++17: std::negation/negation_v
// C++11: std::integral_constant

// C++14: use enable_if_t
template <bool Bool, typename Type = void>
using enable_if_t = typename std::enable_if<Bool, Type>::type;

/// Values.

template <size_t Value>
using if_odd = enable_if_t<is_odd(Value), bool>;

template <size_t Value>
using if_even = enable_if_t<is_even(Value), bool>;

template <size_t Value>
using if_non_zero = enable_if_t<!is_zero(Value), bool>;

template <size_t Value, size_t Size>
using if_equal = enable_if_t<Value == Size, bool>;

template <size_t Left, size_t Right>
using if_greater = enable_if_t<(Left > Right), bool>;

template <size_t Left, size_t Right>
using if_not_greater = enable_if_t<!(Left > Right), bool>;

template <size_t Left, size_t Right>
using if_lesser = enable_if_t<(Left < Right), bool>;

template <size_t Left, size_t Right>
using if_not_lesser = enable_if_t<!(Left < Right), bool>;

/// Types.

template <typename Left, typename Right>
using if_same = std::is_same<Left, Right>;

template <typename Type>
using if_byte = enable_if_t<!(width<Type>() > width<uint8_t>()), bool>;

template <typename Type>
using if_bytes = enable_if_t<(width<Type>() > width<uint8_t>()), bool>;

template <typename Type, size_t Size>
using if_size_of = enable_if_t<sizeof(Type) == Size, bool>;

template <typename Type>
using if_const = enable_if_t<std::is_const<Type>::value, bool>;

template <typename Type>
using if_non_const = enable_if_t<!std::is_const<Type>::value, bool>;

template <typename Base, typename Type>
using if_base_of = enable_if_t<
    std::is_base_of<Base, Type>::value, bool>;

template <typename Type>
using if_byte_insertable = enable_if_t<
    std::is_base_of<std::vector<uint8_t>, Type>::value ||
    std::is_base_of<std::string, Type>::value, bool>;

template <typename Type>
using if_default_constructible = enable_if_t<
    std::is_default_constructible<Type>::value, bool>;

template <typename Type>
using if_trivially_constructible = enable_if_t<
    std::is_trivially_constructible<Type>::value, bool>;

template <typename Type>
using if_unique_object_representations = enable_if_t<
    std::has_unique_object_representations<Type>::value, bool>;

template <typename Left, typename Right>
using if_same_width = enable_if_t<width<Left>() == width<Right>(), bool>;

template <typename Left, typename Right>
using if_lesser_width = enable_if_t<width<Left>() < width<Right>(), bool>;

template <typename Left, typename Right>
using if_not_lesser_width = enable_if_t<width<Left>() >= width<Right>(),
    bool>;

/// Integer types (specializable, non-floating math, no bool).

template <typename Type>
using if_integer = enable_if_t<is_integer<Type>(), bool>;

template <typename Type>
using if_signed_integer = enable_if_t<is_integer<Type>() &&
    std::is_signed<Type>::value, bool>;

template <typename Type>
using if_unsigned_integer = enable_if_t<is_integer<Type>() &&
    !std::is_signed<Type>::value, bool>;

template <typename Left, typename Right>
using if_same_signed_integer = enable_if_t<
    is_integer<Left>() && is_integer<Right>() &&
    (std::is_signed<Left>::value == std::is_signed<Right>::value), bool>;

template <typename Left, typename Right>
using if_not_same_signed_integer = enable_if_t<
    is_integer<Left>() && is_integer<Right>() &&
    (std::is_signed<Left>::value != std::is_signed<Right>::value), bool>;

/// Integral integer types (native, non-floating math, no bool).

template <typename Type>
using if_integral_integer = enable_if_t<is_integer<Type>() &&
    std::is_integral<Type>::value, bool>;

template <typename Type>
using if_non_integral_integer = enable_if_t<is_integer<Type>() &&
    !std::is_integral<Type>::value, bool>;

/// Type determination by required byte width and sign.

template <size_t Bytes, if_not_greater<Bytes, sizeof(int64_t)> = true>
using signed_type =
    std::conditional_t<Bytes == 0, signed_size_t,
        std::conditional_t<Bytes == 1, int8_t,
            std::conditional_t<Bytes == 2, int16_t,
                std::conditional_t<Bytes <= 4, int32_t, int64_t>>>>;

template <size_t Bytes, if_not_greater<Bytes, sizeof(uint64_t)> = true>
using unsigned_type =
    std::conditional_t< Bytes == 0, size_t,
        std::conditional_t<Bytes == 1, uint8_t,
            std::conditional_t<Bytes == 2, uint16_t,
                std::conditional_t<Bytes <= 4, uint32_t, uint64_t>>>>;

/// Endianness.

// C++20: replace with std::bit_cast.
// This should be in math/bytes but required here for representations.
template <typename Result, typename Integer,
    if_integral_integer<Integer> = true,
    if_trivially_constructible<Result> = true>
constexpr Result bit_cast(Integer value) noexcept
{
    Result result;
    std::copy_n(&value, sizeof(Result), &result);
    return result;
}

constexpr bool is_big_endian_representation() noexcept
{
    union foo { uint8_t u1[2]; uint16_t u2; } volatile const bar{ 0x0001 };
    return to_bool(bar.u1[1]);
}

constexpr bool is_little_endian_representation() noexcept
{
    union foo { uint8_t u1[2]; uint16_t u2; } volatile const bar{ 0x0001 };
    return to_bool(bar.u1[0]);
}

constexpr bool is_unknown_endian_representation() noexcept
{
    return !is_big_endian_representation() &&
        !is_little_endian_representation();
}

constexpr auto is_big_endian = is_big_endian_representation();
constexpr auto is_little_endian = is_little_endian_representation();
constexpr auto is_unknown_endian = is_unknown_endian_representation();
static_assert(!is_unknown_endian, "unsupported integer representation");

template <typename Integer>
using if_big_endian_integral_integer = enable_if_t<is_big_endian &&
    is_integer<Integer>() && std::is_integral<Integer>::value, bool>;

template <typename Integer>
using if_little_endian_integral_integer = enable_if_t<is_little_endian &&
    is_integer<Integer>() && std::is_integral<Integer>::value, bool>;

} // namespace system
} // namespace libbitcoin

#endif
