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
#ifndef LIBBITCOIN_SYSTEM_CRYPTO_SCRYPT_HPP
#define LIBBITCOIN_SYSTEM_CRYPTO_SCRYPT_HPP

#include <bitcoin/system/crypto/pbkd_sha256.hpp>
#include <bitcoin/system/data/data.hpp>
#include <bitcoin/system/define.hpp>
#include <bitcoin/system/data/data.hpp>
#include <bitcoin/system/math/math.hpp>

namespace libbitcoin {
namespace system {

/// tools.ietf.org/html/rfc7914
/// en.wikipedia.org/wiki/Scrypt  [Colin Percival]
/// en.wikipedia.org/wiki/Salsa20 [Daniel J. Bernstein]

/// [W]ork must be a power of 2 greater than 1.
/// [R]esources must be non-zero and <= max_size_t/128.
/// [P]arallelism must be non-zero.
/// Implementation constraints as fn(size_t), rfc7914 may be more restrictive.
template<size_t W, size_t R, size_t P>
constexpr auto is_scrypt_args =
    !is_zero(R) && !is_zero(P) && 
    !is_multiply_overflow(R, 2_size * 64_size) &&
    (W > one) && (power2(floored_log2(W)) == W);

/// Concurrent increases memory consumption from minimum to maximum.
template<size_t W, size_t R, size_t P, bool Concurrent = false,
    bool_if<is_scrypt_args<W, R, P>> If = true>
class scrypt
{
public:
    static constexpr auto block_size = 64_size;

    /// Peak variable memory consumption for non-concurrent execution.
    static constexpr auto minimum_memory = 1_u64 *
        (1 * (3 * (1 * 1 * block_size))) + // (denormalizing optimization)
        (1 * (1 * (2 * R * block_size))) - (1 * (add1(R) * block_size)) +
        (1 * (W * (2 * R * block_size))) +
        (1 * (P * (2 * R * block_size)));

    /// Peak variable memory consumption for fully-concurrent execution.
    static constexpr auto maximum_memory = 1_u64 *
        (P * (3 * (1 * 1 * block_size))) + // (denormalizing optimization)
        (P * (1 * (2 * R * block_size))) - (P * (add1(R) * block_size)) +
        (P * (W * (2 * R * block_size))) +
        (1 * (P * (2 * R * block_size)));

    /// False if out of memory or size > pbkdf2::maximum_size.
    static bool hash(const data_slice& phrase, const data_slice& salt,
        uint8_t* buffer, size_t size) NOEXCEPT;

    /// Returns null hash if out of memory.
    template<size_t Size,
        if_not_greater<Size, pbkd::sha256::maximum_size> = true>
    static data_array<Size> hash(const data_slice& phrase,
        const data_slice& salt) NOEXCEPT
    {
        data_array<Size> out{};
        hash(phrase, salt, out.data(), Size);
        return out;
    }

protected:
    using word_t    = uint32_t;
    using words_t   = std_array<word_t,   block_size / sizeof(word_t)>;
    using block_t   = std_array<uint8_t,  block_size>;
    using rblock_t  = std_array<block_t,  R * 2_size>;
    using prblock_t = std_array<rblock_t, P>;
    using wrblock_t = std_array<rblock_t, W>;

    template<typename Block>
    static inline auto allocate() NOEXCEPT;
    static constexpr auto parallel() NOEXCEPT;
    static constexpr words_t& add(words_t& to, const words_t& from) NOEXCEPT;
    static constexpr block_t& xor_(block_t& to, const block_t& from) NOEXCEPT;
    static constexpr rblock_t& xor_(rblock_t& to, const rblock_t& from) NOEXCEPT;
    static inline size_t index(const rblock_t& rblock) NOEXCEPT;

    template <size_t A, size_t B, size_t C, size_t D>
    static constexpr void salsa_qr(words_t& words) NOEXCEPT;
    static block_t& salsa_8(block_t& block) NOEXCEPT;
    static bool block_mix(rblock_t& rblock) NOEXCEPT;
    static bool romix(rblock_t& rblock) NOEXCEPT;
};

/// Litecoin/BIP38 scrypt arguments.
static_assert(is_scrypt_args< 1024, 1, 1>);
static_assert(is_scrypt_args<16384, 8, 8>);

/// Litecoin/BIP38 minimum/maximum peak variable memory consumption.
static_assert(scrypt< 1024, 1, 1>::minimum_memory == 131'392_u64);
static_assert(scrypt<16384, 8, 8>::minimum_memory == 16'786'048_u64);
static_assert(scrypt< 1024, 1, 1>::maximum_memory == 131'392_u64);
static_assert(scrypt<16384, 8, 8>::maximum_memory == 134'231'040_u64);

} // namespace system
} // namespace libbitcoin

#include <bitcoin/system/impl/crypto/scrypt.ipp>

#endif
