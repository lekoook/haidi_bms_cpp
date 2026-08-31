// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#include <haidi_frame_parser.hpp>

namespace haidi
{

void FrameParser::reset() {
    len_ = 0;
    discarded_ = 0;
}

void FrameParser::resync() {
    // buf_[0] is known bad. Drop it, then slide forward to the next byte that
    // could plausibly start a frame. Dropping the whole buffer instead would
    // swallow a genuine frame that began inside a corrupt one -- which is the
    // common case after a single dropped byte on the wire.
    ++discarded_;

    size_t next = 1;
    while (next < len_ && buf_[next] != START_FLAG) {
        ++next;
        ++discarded_;
    }

    const size_t remaining = len_ - next;
    for (size_t i = 0; i < remaining; ++i) {
        buf_[i] = buf_[next + i];
    }
    len_ = remaining;
}

void FrameParser::feed(const uint8_t* data, size_t len, FrameFn sink, void* ctx, ErrorFn on_error) {
    if (data == nullptr) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        const uint8_t byte = data[i];

        // A frame can only begin at the start flag; anything else is junk.
        if (len_ == 0 && byte != START_FLAG) {
            ++discarded_;
            continue;
        }

        buf_[len_] = byte;
        ++len_;

        // The length field is a fixed 0x08 in both directions, so this prunes
        // a false 0xA5 at the fourth byte rather than the thirteenth.
        if (len_ == DATA_OFFSET && buf_[MSG_LEN_INDEX] != LENGTH_FLAG) {
            resync();
            if (on_error != nullptr) {
                on_error(ctx, FrameError::BAD_LENGTH);
            }
            continue;
        }

        if (len_ < MSG_MAX_LEN) {
            continue;
        }

        if (buf_[MSG_LAST_INDEX] == calc_chksum(buf_)) {
            const MessageBytes frame = buf_;
            len_ = 0;
            if (sink != nullptr) {
                sink(ctx, frame);
            }
        } else {
            resync();
            if (on_error != nullptr) {
                on_error(ctx, FrameError::CHECKSUM);
            }
        }
    }
}

} // namespace haidi
