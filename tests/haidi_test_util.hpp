// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

// Shared scaffolding for the public-interface test suite.
//
// Everything here is built out of what include/ exposes and nothing else. The
// test target does not put src/ on its include path, so this header could not
// reach the internal decode layer even if it wanted to -- which is the point.
// Payload decoding is therefore verified the way a user would observe it: poll,
// let the driver transmit, feed a synthesised reply back, inspect the Event.

#include <haidi_bms.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace haidi_test
{

using namespace haidi; // NOLINT(google-build-using-namespace) -- test-local convenience

// --- frame builders ---------------------------------------------------------

/// Packs up to eight bytes into a payload, zero-filling the remainder.
inline DataBytes bytes8(std::initializer_list<uint8_t> bytes) {
    DataBytes data{};
    size_t i = 0;
    for (uint8_t b : bytes) {
        if (i < DATA_LEN) {
            data[i++] = b;
        }
    }
    return data;
}

/// A well-formed BMS reply frame carrying `data`, with a correct checksum.
inline MessageBytes make_reply(DataId id, const DataBytes& data,
                               uint8_t source_address = BMS_ADDRESS) {
    MessageBytes frame{};
    frame[MSG_START_INDEX] = START_FLAG;
    frame[MSG_ADDRESS_INDEX] = source_address;
    frame[MSG_DATA_ID_INDEX] = static_cast<uint8_t>(id);
    frame[MSG_LEN_INDEX] = LENGTH_FLAG;
    for (size_t i = 0; i < DATA_LEN; ++i) {
        frame[DATA_OFFSET + i] = data[i];
    }
    frame[MSG_LAST_INDEX] = calc_chksum(frame);
    return frame;
}

inline MessageBytes make_reply(DataId id, std::initializer_list<uint8_t> bytes) {
    return make_reply(id, bytes8(bytes));
}

/// One frame of a paginated reply: Byte0 is the frame counter, Byte1..7 payload.
///
/// The documented counter base is NOT uniform -- 0x55/0x56/0x57/0x6A count from
/// 1, while 0x62/0x63/0x95/0x96/0x64 count from 0 -- but the session accepts
/// either on any paginated command, so the base a caller passes decides which
/// convention the test exercises.
inline MessageBytes make_multi_frame(DataId id, uint8_t counter,
                                     std::initializer_list<uint8_t> seven) {
    DataBytes data{};
    data[0] = counter;
    size_t i = 1;
    for (uint8_t b : seven) {
        if (i < DATA_LEN) {
            data[i++] = b;
        }
    }
    return make_reply(id, data);
}

/// Same, taking the seven payload bytes as a pointer so callers can slice.
inline MessageBytes make_multi_frame(DataId id, uint8_t counter, const uint8_t* payload,
                                     size_t len) {
    DataBytes data{};
    data[0] = counter;
    for (size_t i = 0; i < len && (i + 1) < DATA_LEN; ++i) {
        data[i + 1] = payload[i];
    }
    return make_reply(id, data);
}

/// Breaks a frame's trailing checksum without touching anything else.
inline MessageBytes corrupt_checksum(MessageBytes frame) {
    frame[MSG_LAST_INDEX] = static_cast<uint8_t>(frame[MSG_LAST_INDEX] + 1U);
    return frame;
}

/// Flattens a frame into a byte vector, for feeding in arbitrary chunks.
inline std::vector<uint8_t> to_bytes(const MessageBytes& frame) {
    return std::vector<uint8_t>(frame.begin(), frame.end());
}

// --- loopback session -------------------------------------------------------

/// A HaidiBMS wired to capture what it transmits and record what it emits.
///
/// Stands in for a real BMS on the far end of a serial port: `reply()` answers
/// whatever the driver last sent. Non-copyable and non-movable, inherited from
/// HaidiBMS itself, which is what makes registering `this` as the handler ctx
/// safe.
struct Loopback {
    HaidiBMS bms;

    std::vector<MessageBytes> sent; ///< Frames the driver put on the wire.
    std::vector<Event> events;      ///< Events the session-wide handler saw.
    std::vector<std::string> order; ///< Handler call order, for ordering tests.

    /// Runs inside the session-wide handler, for reentrancy tests.
    std::function<void(Loopback&, const Event&)> on_event_hook;

    explicit Loopback(HostAddress host = HostAddress::COMP, uint8_t bms_address = BMS_ADDRESS)
        : bms(host, bms_address) {
        bms.set_tx_handler(&Loopback::tx_trampoline, this);
        bms.set_event_handler(&Loopback::ev_trampoline, this);
        bms.tick(0);
    }

    static void tx_trampoline(void* ctx, const uint8_t* data, size_t len) {
        auto& self = *static_cast<Loopback*>(ctx);
        MessageBytes frame{};
        std::memcpy(frame.data(), data, len < MSG_MAX_LEN ? len : MSG_MAX_LEN);
        self.sent.push_back(frame);
    }

    static void ev_trampoline(void* ctx, const Event& event) {
        auto& self = *static_cast<Loopback*>(ctx);
        self.events.push_back(event);
        self.order.emplace_back("global");
        if (self.on_event_hook) {
            self.on_event_hook(self, event);
        }
    }

    void feed(const MessageBytes& frame) { bms.feed(frame.data(), frame.size()); }
    void feed_bytes(const std::vector<uint8_t>& bytes) { bms.feed(bytes.data(), bytes.size()); }

    /// Answers the outstanding request with a reply carrying `data`.
    void reply(DataId id, const DataBytes& data) { feed(make_reply(id, data)); }
    void reply(DataId id, std::initializer_list<uint8_t> bytes) { feed(make_reply(id, bytes)); }

    [[nodiscard]] const MessageBytes& last_sent() const { return sent.back(); }
    [[nodiscard]] DataId last_sent_id() const {
        return static_cast<DataId>(sent.back()[MSG_DATA_ID_INDEX]);
    }

    void clear() {
        sent.clear();
        events.clear();
        order.clear();
    }

    /// Polls, answers with one frame, and returns the single resulting Event.
    ///
    /// `poll` is a callable so each test names the real public method under
    /// test; HaidiBMS deliberately keeps its generic request() private, so
    /// there is no by-DataId escape hatch to use instead.
    template<typename PollFn>
    Event round_trip(PollFn poll, DataId id, const DataBytes& data) {
        events.clear();
        EXPECT_EQ(poll(), QueueStatus::QUEUED);
        EXPECT_FALSE(sent.empty());
        if (!sent.empty()) {
            EXPECT_EQ(last_sent_id(), id) << "the driver polled a different Data ID";
        }
        reply(id, data);
        EXPECT_EQ(events.size(), 1U) << "expected exactly one event for " << to_string(id);
        return events.empty() ? Event{} : events.front();
    }

    template<typename PollFn>
    Event round_trip(PollFn poll, DataId id, std::initializer_list<uint8_t> bytes) {
        return round_trip(poll, id, bytes8(bytes));
    }

    /// Polls, feeds every frame in order, and returns all resulting events.
    template<typename PollFn>
    std::vector<Event> round_trip_multi(PollFn poll, const std::vector<MessageBytes>& frames) {
        events.clear();
        EXPECT_EQ(poll(), QueueStatus::QUEUED);
        for (const MessageBytes& frame : frames) {
            feed(frame);
        }
        return events;
    }
};

/// A per-request handler that records the events it was given.
struct Sink {
    std::string name = "sink";
    std::vector<Event> events;
    std::vector<std::string>* order = nullptr;

    static void fn(void* ctx, const Event& event) {
        auto& self = *static_cast<Sink*>(ctx);
        self.events.push_back(event);
        if (self.order != nullptr) {
            self.order->push_back(self.name);
        }
    }
};

} // namespace haidi_test
