// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

// Payload decoding, and the metadata that describes how a paginated reply is
// reassembled. Internal: nothing in include/ exposes it, and the test suite is
// built so that it cannot include it (see tests/CMakeLists.txt). It is kept
// apart from the session so the decoders carry no transaction state.

#include <haidi_event.hpp>
#include <haidi_payloads.hpp>
#include <haidi_protocol.hpp>

#include <cstddef>
#include <cstdint>

namespace haidi
{

// How a paginated reply is put back together.
//
// The frame-counter base is NOT uniform across the protocol -- 0x55/0x56/0x57
// and 0x6A count from 1, while 0x62/0x63/0x95/0x96 and 0x64 count from 0 -- so
// it has to travel with the command rather than being assumed. It is what the
// protocol document specifies, which is not always what a device does: the
// session treats `frame_base` as the documented default and reads the real base
// off the frames it receives, so a pack that numbers against its own spec still
// decodes.
struct MultiFrameInfo {
    bool multi = false;          // false: a single 8-byte reply
    bool streaming = false;      // 0x64: unbounded, ends on the 0xFF sentinel
    uint8_t frame_base = 0;      // documented value of Byte0 in the first frame
    uint8_t bytes_per_frame = 0; // payload bytes carried after Byte0
    uint8_t frame_count = 0;     // frames expected, 0 when it depends on the pack
    uint8_t total_bytes = 0;     // reassembled length, 0 when pack-dependent
};

// The 0x64 end-of-stream sentinel: "0xFF means all faults have been
// transmitted".
static constexpr uint8_t FRAME_NO_END = 0xFF;

// Returns the pagination rules for `id`. `multi == false` for every
// single-frame command.
MultiFrameInfo multiframe_info(DataId id);

// Frames needed to carry `count` cells (0x95, three cells per frame) or
// `count` temperature sensors (0x96, seven per frame).
uint8_t cell_voltage_frames(uint8_t cell_count);
uint8_t cell_temperature_frames(uint8_t temp_count);

// Decodes a single-frame reply. Returns false when `id` has no single-frame
// decoder, in which case `out` is left untouched.
bool decode_single(DataId id, const DataBytes& data, EventPayload& out);

// Decodes a reassembled paginated reply. `len` is the number of bytes actually
// collected, which may be short of the nominal total if the reply was
// truncated. Returns false when `id` is not a paginated command.
bool decode_multi(DataId id, const uint8_t* buf, size_t len, EventPayload& out);

// Decodes one 27-byte Table 1 fault record.
bool decode_fault_record(const uint8_t* record, size_t len, FaultRecord& out);

} // namespace haidi
