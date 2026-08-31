// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file haidi_frame_parser.hpp
 * @brief Byte-stream framer: chunked serial input in, whole verified frames out.
 */

#include <haidi_protocol.hpp>

#include <cstddef>
#include <cstdint>

namespace haidi
{

/**
 * @brief Structural faults the framer can detect without protocol knowledge.
 *
 * A byte that simply is not a start flag is counted by FrameParser::discarded()
 * rather than reported here, because a resynchronising link would otherwise
 * produce one report per junk byte.
 */
enum class FrameError : uint8_t {
    BAD_LENGTH, ///< Byte 3 was not the fixed length field, 0x08.
    CHECKSUM    ///< The trailing checksum did not match the frame contents.
};

/**
 * @brief Reassembles an arbitrarily chunked byte stream into 13-byte frames.
 *
 * Frames are emitted only once complete and checksum-verified. The parser knows
 * nothing about Data IDs -- correlating a reply to a request is the session's
 * job -- but it does enforce the two structural invariants that hold in both
 * directions, a leading 0xA5 and a length field of 0x08, which lets it reject a
 * false start at the fourth byte rather than the thirteenth.
 *
 * On a bad frame it drops a single byte and rescans, rather than discarding the
 * whole buffer, so a genuine frame that began inside a corrupt one still
 * survives. That is the common case after one dropped byte on the wire.
 *
 * @see HaidiBMS::feed which owns an instance of this and drives it.
 */
class FrameParser {
    public:
    /** @brief Called once per complete, checksum-verified frame. */
    using FrameFn = void (*)(void* ctx, const MessageBytes& frame);

    /** @brief Called once per rejected candidate frame. */
    using ErrorFn = void (*)(void* ctx, FrameError error);

    /**
     * @brief Consumes received bytes, emitting whole frames as they complete.
     *
     * Any number of frames, including none, may be emitted by one call, and a
     * single frame may span several calls.
     *
     * @param data     Bytes received from the transport. Ignored when @c nullptr.
     * @param len      Number of bytes available at @p data.
     * @param sink     Invoked once per valid frame; may be @c nullptr.
     * @param ctx      Opaque pointer passed to @p sink and @p on_error.
     * @param on_error Invoked once per rejected frame; optional.
     */
    void feed(const uint8_t* data, size_t len, FrameFn sink, void* ctx, ErrorFn on_error = nullptr);

    /** @brief Discards any partially accumulated frame and zeroes discarded(). */
    void reset();

    /**
     * @brief Bytes currently held in the partial-frame buffer.
     * @return A count below ::MSG_MAX_LEN; a complete frame is never held.
     */
    [[nodiscard]] size_t buffered() const { return len_; }

    /**
     * @brief Bytes thrown away as junk.
     *
     * A steadily climbing count means a noisy link or a baud-rate mismatch.
     *
     * @return Bytes discarded since construction or the last reset().
     */
    [[nodiscard]] uint32_t discarded() const { return discarded_; }

    private:
    // Drops buf_[0] and compacts the remainder forward to the next plausible
    // start flag. Used both on a bad checksum and on a failed structural
    // check, so that a real frame beginning inside a corrupt one survives.
    void resync();

    MessageBytes buf_{};
    size_t len_ = 0;
    uint32_t discarded_ = 0;
};

} // namespace haidi
