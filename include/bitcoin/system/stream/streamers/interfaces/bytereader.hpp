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
#ifndef LIBBITCOIN_SYSTEM_STREAM_STREAMERS_INTERFACES_BYTEREADER_HPP
#define LIBBITCOIN_SYSTEM_STREAM_STREAMERS_INTERFACES_BYTEREADER_HPP

#include <iostream>
#include <string>
#include <bitcoin/system/crypto/crypto.hpp>
#include <bitcoin/system/data/data.hpp>
#include <bitcoin/system/define.hpp>
#include <bitcoin/system/error/error.hpp>

namespace libbitcoin {
namespace system {
    
/// A byte reader interface.
class bytereader
{
public:
    /// Read big endian (explicit specializations of read_big_endian).
    virtual uint16_t read_2_bytes_big_endian() NOEXCEPT = 0;
    virtual uint32_t read_4_bytes_big_endian() NOEXCEPT = 0;
    virtual uint64_t read_8_bytes_big_endian() NOEXCEPT = 0;

    /// Little endian integer readers (specializations of read_little_endian).
    virtual uint16_t read_2_bytes_little_endian() NOEXCEPT = 0;
    virtual uint32_t read_4_bytes_little_endian() NOEXCEPT = 0;
    virtual uint64_t read_8_bytes_little_endian() NOEXCEPT = 0;

    /// Read Bitcoin variable integer (1, 3, 5, or 9 bytes, little-endian).
    virtual uint64_t read_variable() NOEXCEPT = 0;

    /// Cast read_variable to size_t, facilitates read_bytes(read_size()).
    /// Returns zero and invalidates stream if would exceed read limit.
    virtual size_t read_size(size_t limit=max_size_t) NOEXCEPT = 0;

    /// Convert read_4_bytes_little_endian to an error code.
    virtual code read_error_code() NOEXCEPT = 0;

    /// Read into stream until buffer is exhausted.
    virtual std::ostream& read(std::ostream& out) NOEXCEPT = 0;

    /// Read hash (explicit specializations of read_forward).
    virtual mini_hash read_mini_hash() NOEXCEPT = 0;
    virtual short_hash read_short_hash() NOEXCEPT = 0;
    virtual hash_digest read_hash() NOEXCEPT = 0;
    virtual long_hash read_long_hash() NOEXCEPT = 0;

    /// Read/peek one byte (invalidates an empty stream).
    virtual uint8_t peek_byte() NOEXCEPT = 0;
    virtual uint8_t read_byte() NOEXCEPT = 0;

    /// Read all remaining bytes.
    virtual data_chunk read_bytes() NOEXCEPT = 0;

    /// Read size bytes, return size is guaranteed.
    virtual data_chunk read_bytes(size_t size) NOEXCEPT = 0;
    virtual void read_bytes(uint8_t* buffer, size_t size) NOEXCEPT = 0;

    /// Read Bitcoin length-prefixed string.
    /// Returns empty and invalidates stream if would exceed read limit.
    virtual std::string read_string(size_t limit=max_size_t) NOEXCEPT = 0;

    /// Read string, truncated at size or first null.
    /// This is only used for reading Bitcoin heading command text.
    virtual std::string read_string_buffer(size_t size) NOEXCEPT = 0;

    /// Advance the iterator.
    virtual void skip_byte() NOEXCEPT = 0;
    virtual void skip_bytes(size_t size) NOEXCEPT = 0;

    /// Rewind the iterator.
    virtual void rewind_byte() NOEXCEPT = 0;
    virtual void rewind_bytes(size_t size) NOEXCEPT = 0;

    /// The stream is empty (or invalid).
    virtual bool is_exhausted() const NOEXCEPT = 0;

    /// Get the current absolute position.
    virtual size_t get_position() NOEXCEPT = 0;

    /// Clear invalid state and set absolute position.
    virtual void set_position(size_t absolute) NOEXCEPT = 0;

    /// Limit stream upper bound to current position plus size (default resets).
    virtual void set_limit(size_t size=max_size_t) NOEXCEPT = 0;

    /// Invalidate the stream.
    virtual void invalidate() NOEXCEPT = 0;

    /// The stream is valid.
    virtual operator bool() const NOEXCEPT = 0;

    /// The stream is invalid.
    virtual bool operator!() const NOEXCEPT = 0;
};

} // namespace system
} // namespace libbitcoin

#endif
