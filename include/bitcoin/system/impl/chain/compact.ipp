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
#ifndef LIBBITCOIN_SYSTEM_CHAIN_COMPACT_NUMBER_IPP
#define LIBBITCOIN_SYSTEM_CHAIN_COMPACT_NUMBER_IPP

#include <cstdint>
// DELETEMENOW
#include <bitcoin/system/define.hpp>
#include <bitcoin/system/math/math.hpp>

namespace libbitcoin {
namespace system {
namespace chain {

//*****************************************************************************
// CONSENSUS:
// Zero is a sufficient negative/zero/overflow sentinel:
// "if (negative || overflow || big == 0) return 0;" and only if the mantissa
// is zero can a logical shift within the domain produce a zero (fail early).
//*****************************************************************************
//*****************************************************************************
// CONSENSUS:
// Satoshi is more permissive, allowing an exponent 34 with a single byte
// mantissa, however this is not necessary to validate any value produced
// by compression, nor is it possible for any such value to affect consensus.
// This is because header.bits values are generated by compress during retarget
// and must match exactly for a header to be valid. Compress cannot generate an
// exponent greater than 33, which is the result of shifting away a negative.
// In any case, an exponent greater than 29 (28 after negative normalization)
// exceeds the mainnet maximum of 0xffffff^28 (0x7fffff^32 for regtest). The
// regtest limit can be approximated as 0x7fff^33 or 0x7f^34, but again, these
// cannot be generated by compress, so they cannot come to be validated.
//*****************************************************************************
//*****************************************************************************
// CONSENSUS:
// Due to an implementation artifact, the representation is not uniform. A high
// bit in the mantissa is pushed into the exponent, dropping mantissa by one
// bit (an order of magnitude). Precision is naturally lost in compression, but
// the loss is not uniform due to this shifting out of the "sign" bit. There is
// of course never an actual negative mantissa sign in exponential notation of
// an unsigned number, so this was a mistake, likely a side effect of working
// with signed numeric types in an unsigned domain.
//*****************************************************************************

// private

constexpr typename compact::parse
compact::to_compact(small_type small) NOEXCEPT
{
    return
    {
        get_right(small, sub1(precision)),
        narrow_cast<exponent_type>(shift_right(small, precision)),
        mask_left<small_type>(small, e_width)
    };
}

constexpr typename compact::small_type
compact::from_compact(const parse& compact) NOEXCEPT
{
    return bit_or
    (
        shift_left(wide_cast<uint32_t>(compact.exponent), precision),
        compact.mantissa
    );
}

// public

constexpr compact::span_type
compact::expand(small_type exponential) NOEXCEPT
{
    auto compact = to_compact(exponential);

    if (compact.negative)
        return 0;

    // This is strict validation.
    if (compact.exponent == add1(e_max) &&
        is_negated(compact.mantissa) &&
        byte_width(compact.mantissa) == sub1(m_bytes))
    {
        compact.exponent--;
        compact.mantissa <<= raise(one);
    }

    // Above exists only because negatives were inadvertently excluded.
    
    return base256e::expand(from_compact(compact));
}

constexpr compact::small_type
compact::compress(const span_type& number) NOEXCEPT
{
    auto compact = to_compact(base256e::compress(number));

    // Below exists only to work around negatives being inadvertently excluded.

    if (compact.negative)
    {
        compact.exponent++;
        compact.mantissa >>= raise(one);
        compact.negative = false;
    }

    return from_compact(compact);
}

} // namespace chain
} // namespace system
} // namespace libbitcoin

#endif
