// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#include "haidi_decode.hpp"

#include <haidi_bms.hpp>

#include <algorithm>

namespace haidi
{

namespace
{

// Wraparound-safe deadline test. A plain `now >= deadline` breaks every 49.7
// days, when the millisecond counter rolls over 2^32.
bool expired(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

uint8_t popcount32(uint32_t value) {
    uint8_t bits = 0;
    while (value != 0) {
        value &= value - 1;
        ++bits;
    }
    return bits;
}

// Counter of the first frame in the transfer: the lowest bit set in the mask.
// Callers guarantee `mask != 0`.
uint8_t lowest_frame_no(uint32_t mask) {
    uint8_t index = 0;
    while ((mask & 1U) == 0) {
        mask >>= 1U;
        ++index;
    }
    return index;
}

// Frames received back-to-back from `base`. A transfer that lost a frame in the
// middle stops here, so the gap is dropped rather than decoded as payload.
uint8_t contiguous_frames(uint32_t mask, uint8_t base) {
    uint8_t run = 0;
    while ((mask & (1U << (base + run))) != 0) {
        ++run;
    }
    return run;
}

} // namespace

HaidiBMS::HaidiBMS(HostAddress host_address, uint8_t bms_address)
    : host_address_(host_address), bms_address_(bms_address) {}

void HaidiBMS::set_tx_handler(TxFn fn, void* ctx) {
    tx_fn_ = fn;
    tx_ctx_ = ctx;
}

void HaidiBMS::set_event_handler(EventFn fn, void* ctx) {
    event_fn_ = fn;
    event_ctx_ = ctx;
}

void HaidiBMS::reset() {
    parser_.reset();
    queue_head_ = 0;
    queue_count_ = 0;
    state_ = State::IDLE;
    retries_ = 0;
    seen_mask_ = 0;
    frames_seen_ = 0;
    expected_frames_ = 0;
    expected_items_ = 0;
    record_len_ = 0;
}

// -----------------------------------------------------------------------------
// Queueing
// -----------------------------------------------------------------------------

QueueStatus HaidiBMS::enqueue(const Request& req) {
    if (tx_fn_ == nullptr) {
        return QueueStatus::NO_TX_HANDLER;
    }
    if (!is_known_data_id(req.id)) {
        return QueueStatus::BAD_REQUEST;
    }
    if (queue_count_ >= QUEUE_CAPACITY) {
        return QueueStatus::QUEUE_FULL;
    }

    queue_[(queue_head_ + queue_count_) % QUEUE_CAPACITY] = req;
    ++queue_count_;

    // While an event is being dispatched the head request has not been retired
    // yet, so transmitting here would put two frames on a bus that allows one.
    // complete() pumps the queue as soon as the handler returns.
    if (!dispatching_) {
        pump();
    }
    return QueueStatus::QUEUED;
}

QueueStatus HaidiBMS::request(DataId id, EventFn fn, void* ctx) {
    return enqueue(Request{id, DataBytes{}, 0, fn, ctx});
}

QueueStatus HaidiBMS::request_paginated(DataId id, uint8_t hint, EventFn fn, void* ctx) {
    return enqueue(Request{id, DataBytes{}, hint, fn, ctx});
}

QueueStatus HaidiBMS::request_mos(DataId id, bool on, EventFn fn, void* ctx) {
    DataBytes data{};
    data[0] = on ? 1 : 0;
    return enqueue(Request{id, data, 0, fn, ctx});
}

// -----------------------------------------------------------------------------
// Transaction lifecycle
// -----------------------------------------------------------------------------

void HaidiBMS::transmit() {
    const MessageBytes frame = encode(host_address_, head().id, head().data);
    if (tx_fn_ != nullptr) {
        tx_fn_(tx_ctx_, frame.data(), frame.size());
    }
    deadline_ms_ = now_ms_ + response_timeout_ms_;
}

void HaidiBMS::begin_awaiting() {
    // Cleared, not just rewound: a frame that never arrives leaves its slot
    // untouched, and without this the previous paginated reply's bytes would
    // sit there to be decoded as this one's payload.
    assembly_.fill(0);
    seen_mask_ = 0;
    frames_seen_ = 0;
    record_len_ = 0;
    retries_ = 0;

    // Resolve the frame count now rather than at enqueue time, so a count
    // learned from a 0x94 earlier in the same queue is already available.
    const MultiFrameInfo info = multiframe_info(head().id);
    expected_frames_ = info.frame_count;

    expected_items_ = 0;
    if (head().id == DataId::CELL_VOLTAGES) {
        expected_items_ = head().hint != 0 ? head().hint : cached_cell_count_;
        expected_frames_ = cell_voltage_frames(expected_items_);
    } else if (head().id == DataId::CELL_TEMPERATURES) {
        expected_items_ = head().hint != 0 ? head().hint : cached_temp_count_;
        expected_frames_ = cell_temperature_frames(expected_items_);
    }
}

void HaidiBMS::pump() {
    if (state_ != State::IDLE || queue_count_ == 0) {
        return;
    }
    state_ = State::AWAITING;
    begin_awaiting();
    transmit();
}

void HaidiBMS::complete() {
    // A handler may have called reset() from inside emit(), which already
    // retired the head request and set state_ to IDLE underneath us. Popping
    // again would wrap queue_count_ past zero, after which pump()'s
    // `queue_count_ == 0` guard passes and it transmits from a slot reset()
    // abandoned. Skipping the pop also preserves a request the handler queued
    // after resetting, which would otherwise be discarded unsent.
    if (state_ == State::AWAITING) {
        queue_head_ = (queue_head_ + 1) % QUEUE_CAPACITY;
        --queue_count_;
    }
    state_ = State::IDLE;
    pump();
}

void HaidiBMS::emit(const Event& event) {
    // An event belongs to the head request exactly when one is in flight. A
    // frame that arrives after its transaction has already timed out has no
    // owner, and reaches the session-wide handler alone.
    //
    // The handler is copied out before dispatching: a callback is free to call
    // reset(), which retires the head request underneath us.
    EventFn per_request = nullptr;
    void* per_request_ctx = nullptr;
    if (state_ == State::AWAITING && queue_count_ > 0) {
        per_request = head().on_event;
        per_request_ctx = head().on_event_ctx;
    }

    // Saved and restored rather than cleared, so a nested emit cannot re-enable
    // transmission part-way through an outer dispatch.
    const bool nested = dispatching_;
    dispatching_ = true;

    if (per_request != nullptr) {
        per_request(per_request_ctx, event);
    }
    if (event_fn_ != nullptr) {
        event_fn_(event_ctx_, event);
    }

    if (!nested) {
        dispatching_ = false;
    }
}

// -----------------------------------------------------------------------------
// Receiving
// -----------------------------------------------------------------------------

void HaidiBMS::frame_trampoline(void* ctx, const MessageBytes& frame) {
    static_cast<HaidiBMS*>(ctx)->on_frame(frame);
}

void HaidiBMS::error_trampoline(void* ctx, FrameError error) {
    static_cast<HaidiBMS*>(ctx)->on_frame_error(error);
}

void HaidiBMS::feed(const uint8_t* data, size_t len) {
    parser_.feed(data, len, &HaidiBMS::frame_trampoline, this, &HaidiBMS::error_trampoline);
}

void HaidiBMS::on_frame_error(FrameError error) {
    Event event;
    event.kind = EventKind::PROTOCOL_ERROR;
    event.id = state_ == State::AWAITING ? head().id : DataId::CAPACITY_VOLTAGE;
    event.error = error == FrameError::CHECKSUM ? ErrorCode::CHECKSUM : ErrorCode::BAD_LENGTH;
    emit(event);
}

void HaidiBMS::note_counts(DataId id, const DataBytes& data) {
    // 0x94 Byte0/Byte1, and 0x51 Byte1-3/Byte4-6 summed over its three boards,
    // tell us how many cells and temperature sensors the pack has, which is
    // what sizes 0x95 and 0x96.
    if (id == DataId::STATUS_INFO) {
        cached_cell_count_ = data[0];
        cached_temp_count_ = data[1];
    } else if (id == DataId::BMU_CELL_TEMP_COUNT) {
        cached_cell_count_ = static_cast<uint8_t>(data[1] + data[2] + data[3]);
        cached_temp_count_ = static_cast<uint8_t>(data[4] + data[5] + data[6]);
    }
}

void HaidiBMS::on_frame(const MessageBytes& frame) {
    const auto id = static_cast<DataId>(frame[MSG_DATA_ID_INDEX]);

    // Section 5.1: the BMS only ever answers; nothing arrives unsolicited. A
    // frame received while idle is therefore a late reply to a transaction
    // that has already timed out.
    if (state_ != State::AWAITING) {
        Event event;
        event.kind = EventKind::PROTOCOL_ERROR;
        event.id = id;
        event.error = ErrorCode::UNEXPECTED_DATA_ID;
        emit(event);
        return;
    }

    if (frame[MSG_ADDRESS_INDEX] != bms_address_) {
        Event event;
        event.kind = EventKind::PROTOCOL_ERROR;
        event.id = id;
        event.error = ErrorCode::BAD_SOURCE_ADDRESS;
        emit(event);
        return; // keep waiting: the real reply may still be in flight
    }

    if (id != head().id) {
        Event event;
        event.kind = EventKind::PROTOCOL_ERROR;
        event.id = id;
        event.error = ErrorCode::UNEXPECTED_DATA_ID;
        emit(event);
        return;
    }

    const DataBytes data = data_of(frame);
    note_counts(id, data);

    const MultiFrameInfo info = multiframe_info(id);
    if (!info.multi) {
        handle_single(id, data);
    } else if (info.streaming) {
        handle_stream(id, data);
    } else {
        handle_multi(id, data);
    }
}

void HaidiBMS::handle_single(DataId id, const DataBytes& data) {
    Event event;
    event.id = id;
    event.frames = 1;

    if (id == DataId::DISCHARGE_MOS_CONTROL || id == DataId::CHARGE_MOS_CONTROL) {
        const bool requested = head().data[0] != 0;
        const bool reported = data[0] != 0;
        event.payload.mos_control_ack = MosControlAck{requested, reported};

        // The protocol defines no NAK, so a state echo that differs from the
        // request is the only way the device can refuse a write.
        if (requested != reported) {
            event.kind = EventKind::PROTOCOL_ERROR;
            event.error = ErrorCode::WRITE_REJECTED;
        }
        emit(event);
        complete();
        return;
    }

    if (!decode_single(id, data, event.payload)) {
        event.kind = EventKind::PROTOCOL_ERROR;
        event.error = ErrorCode::UNKNOWN_DATA_ID;
        emit(event);
        complete();
        return;
    }

    emit(event);
    complete();
}

void HaidiBMS::handle_multi(DataId id, const DataBytes& data) {
    const MultiFrameInfo info = multiframe_info(id);
    const uint8_t frame_no = data[0];

    // The counter is taken at face value. Which base a command counts from is
    // documented per-command but not honoured by every device -- so rather than
    // rejecting anything below the spec's base, the transfer files each frame
    // under its own counter and works the base out in finish_multi() from what
    // actually arrived. The ceiling is 0x95 at full width: 48 cells over 16
    // frames, numbered from 1. That also rejects the 0x64 end-of-stream
    // sentinel (FRAME_NO_END, 0xFF), which has no meaning on a bounded reply.
    if (frame_no > MAX_FRAME_INDEX) {
        Event event;
        event.kind = EventKind::PROTOCOL_ERROR;
        event.id = id;
        event.error = ErrorCode::FRAME_SEQUENCE;
        emit(event);
        return;
    }

    const size_t offset = static_cast<size_t>(frame_no) * info.bytes_per_frame;
    for (size_t i = 0; i < info.bytes_per_frame && (offset + i) < ASSEMBLY_LEN; ++i) {
        assembly_[offset + i] = data[1 + i];
    }

    const uint32_t bit = 1U << frame_no;
    if ((seen_mask_ & bit) == 0) {
        seen_mask_ |= bit;
        frames_seen_ = popcount32(seen_mask_);
    }

    if (expected_frames_ != 0 && frames_seen_ >= expected_frames_) {
        finish_multi(id);
        return;
    }

    // More frames to come. With no known frame count (0x95/0x96 on a pack
    // whose cell count we have never read) the inter-frame timeout is what
    // ends the reply.
    deadline_ms_ = now_ms_ + interframe_timeout_ms_;
}

void HaidiBMS::finish_multi(DataId id) {
    const MultiFrameInfo info = multiframe_info(id);

    // Where the payload starts is whatever counter the device began at, not
    // what the spec says it should have been. A pack that numbers 0x95 from 1
    // when its own protocol document says 0 would otherwise leave the first
    // frame's worth of cells unwritten and shift every real reading three
    // places along.
    const uint8_t base = seen_mask_ != 0 ? lowest_frame_no(seen_mask_) : info.frame_base;
    const size_t start = static_cast<size_t>(base) * info.bytes_per_frame;

    // Only the unbroken run from `base` is payload. A reply missing a middle
    // frame comes out short rather than full-length with a hole in it, which
    // matters because a hole in 0x95 is indistinguishable from three cells
    // genuinely reading 0 mV.
    const uint8_t run = seen_mask_ != 0 ? contiguous_frames(seen_mask_, base) : 0;

    // Trim to the nominal length where one is known: the last frame of a
    // 16-byte reply carried over three 7-byte frames is part padding, and a
    // 16-cell 0x95 reply arrives in six frames holding room for 18.
    size_t len = std::min(static_cast<size_t>(run) * info.bytes_per_frame, ASSEMBLY_LEN - start);
    len = std::min(len, MAX_REASSEMBLY_LEN);
    if (info.total_bytes != 0 && len > info.total_bytes) {
        len = info.total_bytes;
    }
    if (expected_items_ != 0) {
        const size_t nominal = id == DataId::CELL_VOLTAGES
                                   ? static_cast<size_t>(expected_items_) * 2
                                   : static_cast<size_t>(expected_items_);
        len = std::min(len, nominal);
    }

    Event event;
    event.id = id;
    event.frames = frames_seen_;

    if (!decode_multi(id, assembly_.data() + start, len, event.payload)) {
        event.kind = EventKind::PROTOCOL_ERROR;
        event.error = ErrorCode::UNKNOWN_DATA_ID;
    }

    emit(event);
    complete();
}

void HaidiBMS::handle_stream(DataId id, const DataBytes& data) {
    const uint8_t frame_no = data[0];

    // "0xFF means all faults have been transmitted."
    if (frame_no == FRAME_NO_END) {
        complete();
        return;
    }

    for (size_t i = 1; i < DATA_LEN && record_len_ < FAULT_BUFFER_LEN; ++i) {
        record_[record_len_] = data[i];
        ++record_len_;
    }
    ++frames_seen_;

    if (record_len_ >= FAULT_BUFFER_LEN) {
        Event event;
        event.id = id;
        event.frames = FAULT_FRAMES_PER_RECORD;
        if (decode_fault_record(record_.data(), record_len_, event.payload.fault_record)) {
            emit(event);
        }
        record_len_ = 0;
    }

    // Same hazard as complete(): a handler may have called reset() while the
    // record was dispatching, ending the stream. Do not re-arm a deadline on a
    // transaction that no longer exists -- and do pump, because this path never
    // reaches complete() and would otherwise strand work the handler queued.
    if (state_ != State::AWAITING) {
        pump();
        return;
    }

    deadline_ms_ = now_ms_ + interframe_timeout_ms_;
}

// -----------------------------------------------------------------------------
// Time
// -----------------------------------------------------------------------------

void HaidiBMS::tick(uint32_t now_ms) {
    now_ms_ = now_ms;

    if (state_ != State::AWAITING || !expired(now_ms_, deadline_ms_)) {
        return;
    }

    const DataId id = head().id;
    const MultiFrameInfo info = multiframe_info(id);

    // A paginated reply that stopped early is still useful: every paginated
    // payload carries its own count or length, so a short reply is
    // self-describing rather than an error. This is also the normal completion
    // path for 0x95/0x96 when the pack's cell count is unknown.
    if (info.multi && !info.streaming && frames_seen_ > 0) {
        finish_multi(id);
        return;
    }

    // A fault-record stream that stopped without the 0xFF sentinel.
    if (info.streaming && frames_seen_ > 0) {
        Event event;
        event.kind = EventKind::PROTOCOL_ERROR;
        event.id = id;
        event.error = ErrorCode::TRUNCATED;
        event.frames = frames_seen_;
        emit(event);
        complete();
        return;
    }

    // Nothing arrived at all. An unimplemented Data ID looks exactly like a
    // dead link here, because the protocol has no "unsupported command" reply.
    if (retries_ < max_retries_) {
        ++retries_;
        transmit();
        return;
    }

    Event event;
    event.kind = EventKind::TIMEOUT;
    event.id = id;
    event.error = ErrorCode::NONE;
    event.retries = retries_;
    emit(event);
    complete();
}

} // namespace haidi
