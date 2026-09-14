// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Paginated and streaming replies driven through HaidiBMS's public API.
//
// Byte0 of each frame is the frame counter and Byte1..7 the payload. The
// counter base is NOT uniform: 0x55/0x56/0x57/0x6A count from 1, while
// 0x62/0x63/0x95/0x96/0x64 count from 0. Most cases here encode the base the
// protocol actually specifies, so a regression in the library's own pagination
// metadata surfaces as a wrong payload count.
//
// The session does not, however, require a device to honour that base: it reads
// the real one off the frames that arrive. The cases below therefore also feed
// each convention to a command documented for the other, because real packs
// have been seen numbering 0x95 from 1 against their own protocol document.

#include "haidi_test_util.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace haidi_test
{
namespace
{

/// Splits `text` into paginated frames for `id`, counting from `base`.
std::vector<MessageBytes> text_frames(DataId id, const std::string& text, uint8_t base,
                                      size_t frame_count) {
    std::vector<MessageBytes> frames;
    for (size_t i = 0; i < frame_count; ++i) {
        uint8_t chunk[7] = {0, 0, 0, 0, 0, 0, 0};
        for (size_t j = 0; j < 7; ++j) {
            const size_t index = (i * 7) + j;
            chunk[j] = index < text.size() ? static_cast<uint8_t>(text[index]) : 0;
        }
        frames.push_back(
            make_multi_frame(id, static_cast<uint8_t>(base + i), chunk, sizeof(chunk)));
    }
    return frames;
}

/// Reads a TextPayload back as a std::string; it is not null-terminated.
std::string text_of(const TextPayload& payload) {
    return std::string(payload.text.data(), payload.len);
}

std::string text_of(const VersionPayload& payload) {
    return std::string(payload.text.data(), payload.len);
}

class MultiFrameTest : public ::testing::Test {
    protected:
    Loopback bus;
};

// -----------------------------------------------------------------------------
// Text commands: counter base 1
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, ManufacturerName_0x55_IsSixteenBytesOverThreeFrames) {
    const std::string name = "HAIDI POWER CO.."; // exactly 16 bytes
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_manufacturer_name(); },
                             text_frames(DataId::MANUFACTURER_NAME, name, 1, 3));

    ASSERT_EQ(events.size(), 1U);
    const TextPayload* payload = as_manufacturer_name(events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->len, 16) << "trimmed to the nominal length, not the 21 bytes carried";
    EXPECT_EQ(text_of(*payload), name);
    EXPECT_EQ(events[0].frames, 3);
    EXPECT_FALSE(bus.bms.busy());
}

TEST_F(MultiFrameTest, BatteryName_0x56_IsThirtyTwoBytesOverFiveFrames) {
    const std::string name = "48V 100Ah LiFePO4 Battery Pack 1"; // 32 bytes
    const std::vector<Event> events = bus.round_trip_multi(
        [&] { return bus.bms.poll_battery_name(); }, text_frames(DataId::BATTERY_NAME, name, 1, 5));

    ASSERT_EQ(events.size(), 1U);
    ASSERT_NE(as_battery_name(events[0]), nullptr);
    EXPECT_EQ(as_battery_name(events[0])->len, 32);
    EXPECT_EQ(text_of(*as_battery_name(events[0])), name);
}

TEST_F(MultiFrameTest, BatterySerialNumber_0x57) {
    const std::string serial = "SN20260902000000001";
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_battery_serial_number(); },
                             text_frames(DataId::BATTERY_SERIAL_NUMBER, serial, 1, 5));

    ASSERT_EQ(events.size(), 1U);
    ASSERT_NE(as_battery_serial_number(events[0]), nullptr);
    EXPECT_EQ(text_of(*as_battery_serial_number(events[0])).substr(0, serial.size()), serial);
}

TEST_F(MultiFrameTest, SnSerialNumber_0x6A) {
    const std::string serial = "ABCDEFGH";
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_sn_serial_number(); },
                             text_frames(DataId::SN_SERIAL_NUMBER, serial, 1, 5));

    ASSERT_EQ(events.size(), 1U);
    ASSERT_NE(as_sn_serial_number(events[0]), nullptr);
    EXPECT_EQ(text_of(*as_sn_serial_number(events[0])).substr(0, serial.size()), serial);
}

TEST_F(MultiFrameTest, TextIsNotNullTerminated) {
    // The header is explicit about this, and a caller that assumes otherwise
    // walks off the end of the array.
    const std::string name = "0123456789ABCDEF";
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_manufacturer_name(); },
                             text_frames(DataId::MANUFACTURER_NAME, name, 1, 3));

    ASSERT_EQ(events.size(), 1U);
    const TextPayload* payload = as_text(events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->len, 16);
    EXPECT_NE(payload->text[15], '\0') << "the last byte is content, not a terminator";
    EXPECT_EQ(payload->text[15], 'F');
}

// -----------------------------------------------------------------------------
// Version commands: counter base 0
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, SoftwareVersion_0x62_IsFourteenBytesOverTwoFramesFromZero) {
    const std::string version = "SW-V4.0.1-2026"; // 14 bytes
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_software_version(); },
                             text_frames(DataId::SOFTWARE_VERSION, version, 0, 2));

    ASSERT_EQ(events.size(), 1U);
    const VersionPayload* payload = as_software_version(events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->len, 14);
    EXPECT_EQ(text_of(*payload), version);
    EXPECT_EQ(events[0].frames, 2);
}

TEST_F(MultiFrameTest, HardwareVersion_0x63) {
    const std::string version = "HW-REV-C000001";
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_hardware_version(); },
                             text_frames(DataId::HARDWARE_VERSION, version, 0, 2));

    ASSERT_EQ(events.size(), 1U);
    ASSERT_NE(as_hardware_version(events[0]), nullptr);
    EXPECT_EQ(text_of(*as_hardware_version(events[0])), version);
}

// -----------------------------------------------------------------------------
// Frame ordering and duplicates
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, FramesArrivingOutOfOrderStillReassembleCorrectly) {
    const std::string name = "0123456789ABCDEF";
    std::vector<MessageBytes> frames = text_frames(DataId::MANUFACTURER_NAME, name, 1, 3);
    std::reverse(frames.begin(), frames.end());

    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_manufacturer_name(); }, frames);

    ASSERT_EQ(events.size(), 1U);
    ASSERT_NE(as_text(events[0]), nullptr);
    EXPECT_EQ(text_of(*as_text(events[0])), name) << "each frame is placed by its counter";
}

TEST_F(MultiFrameTest, ADuplicateFrameIsNotCountedTwice) {
    const std::string name = "0123456789ABCDEF";
    const std::vector<MessageBytes> frames = text_frames(DataId::MANUFACTURER_NAME, name, 1, 3);

    ASSERT_EQ(bus.bms.poll_manufacturer_name(), QueueStatus::QUEUED);
    bus.feed(frames[0]);
    bus.feed(frames[0]); // the same frame again
    bus.feed(frames[1]);

    EXPECT_TRUE(bus.events.empty()) << "a duplicate must not stand in for the missing frame";
    EXPECT_TRUE(bus.bms.busy());

    bus.feed(frames[2]);
    ASSERT_EQ(bus.events.size(), 1U);
    ASSERT_NE(as_text(bus.events[0]), nullptr);
    EXPECT_EQ(text_of(*as_text(bus.events[0])), name);
    EXPECT_EQ(bus.events[0].frames, 3) << "three distinct frames, not four";
}

// -----------------------------------------------------------------------------
// Frame counter range
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, ACounterBelowTheDocumentedBaseIsAccepted) {
    // 0x55 is documented as counting from 1. A device that counts from 0
    // anyway is decoded rather than rejected: the base comes from the frames,
    // not from the table. This is the mirror of the 0x95 case below, and the
    // reason the old FRAME_SEQUENCE behaviour here had to go.
    const std::string name = "HAIDI POWER CO.."; // exactly 16 bytes
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_manufacturer_name(); },
                             text_frames(DataId::MANUFACTURER_NAME, name, 0, 3));

    ASSERT_EQ(events.size(), 1U);
    const TextPayload* payload = as_text(events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->len, 16);
    EXPECT_EQ(text_of(*payload), name) << "decoded from frame 0, not shifted by the table's base";
    EXPECT_FALSE(bus.bms.busy());
}

TEST_F(MultiFrameTest, TheEndSentinelIsRejectedOnANonStreamingCommand) {
    // 0xFF is the 0x64 end-of-stream marker and has no meaning here.
    ASSERT_EQ(bus.bms.poll_software_version(), QueueStatus::QUEUED);
    bus.feed(make_multi_frame(DataId::SOFTWARE_VERSION, 0xFF, {'x'}));

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].error, ErrorCode::FRAME_SEQUENCE);
}

TEST_F(MultiFrameTest, ACounterPastTheReassemblyBufferIsAFrameSequenceError) {
    // 16 is the highest counter any command can legitimately use -- 0x95's 48
    // cells over 16 frames, numbered from 1 -- so 17 is the first rejected one.
    ASSERT_EQ(bus.bms.poll_software_version(), QueueStatus::QUEUED);
    bus.feed(make_multi_frame(DataId::SOFTWARE_VERSION, 17, {'x'}));

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].error, ErrorCode::FRAME_SEQUENCE);
    EXPECT_TRUE(bus.bms.busy()) << "the transaction stays open";
}

// -----------------------------------------------------------------------------
// Short replies end on the inter-frame timeout, and are NOT errors
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, APaginatedReplyThatStopsEarlyIsASuccessfulShortResponse) {
    // Every paginated payload carries its own length, so a short reply is
    // self-describing rather than a failure.
    bus.bms.set_interframe_timeout_ms(100);
    ASSERT_EQ(bus.bms.poll_manufacturer_name(), QueueStatus::QUEUED);

    const std::vector<MessageBytes> frames =
        text_frames(DataId::MANUFACTURER_NAME, "HAIDI..", 1, 3);
    bus.feed(frames[0]);
    EXPECT_TRUE(bus.events.empty());

    bus.bms.tick(100); // the inter-frame deadline, not an error path

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::RESPONSE) << "short, but still a response";
    EXPECT_EQ(bus.events[0].error, ErrorCode::NONE);
    EXPECT_EQ(bus.events[0].frames, 1);

    const TextPayload* payload = as_text(bus.events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->len, 7) << "one frame's worth of bytes";
    EXPECT_FALSE(bus.bms.busy());
}

// -----------------------------------------------------------------------------
// 0x95 cell voltages: three cells per frame, counter base 0
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, CellVoltages_0x95_WithAnExplicitCount) {
    // Six cells over two frames, three per frame, two bytes each.
    const std::vector<MessageBytes> frames{
        make_multi_frame(DataId::CELL_VOLTAGES, 0, {0x0D, 0x48, 0x0D, 0x49, 0x0D, 0x4A}),
        make_multi_frame(DataId::CELL_VOLTAGES, 1, {0x0D, 0x4B, 0x0D, 0x4C, 0x0D, 0x4D}),
    };

    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_cell_voltages(6); }, frames);

    ASSERT_EQ(events.size(), 1U);
    const CellVoltages* payload = as_cell_voltages(events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->count, 6);
    EXPECT_EQ(payload->mv[0], 3400);
    EXPECT_EQ(payload->mv[1], 3401);
    EXPECT_EQ(payload->mv[2], 3402);
    EXPECT_EQ(payload->mv[3], 3403);
    EXPECT_EQ(payload->mv[4], 3404);
    EXPECT_EQ(payload->mv[5], 3405);
    EXPECT_EQ(events[0].frames, 2);
}

TEST_F(MultiFrameTest, CellVoltages_0x95_TrimsThePaddingOnTheLastFrame) {
    // Four cells still need two frames, and the second carries room for three.
    const std::vector<MessageBytes> frames{
        make_multi_frame(DataId::CELL_VOLTAGES, 0, {0x0D, 0x48, 0x0D, 0x49, 0x0D, 0x4A}),
        make_multi_frame(DataId::CELL_VOLTAGES, 1, {0x0D, 0x4B, 0xFF, 0xFF, 0xFF, 0xFF}),
    };

    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_cell_voltages(4); }, frames);

    ASSERT_EQ(events.size(), 1U);
    const CellVoltages* payload = as_cell_voltages(events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->count, 4) << "the padding past the real count is dropped";
    EXPECT_EQ(payload->mv[3], 3403);
}

TEST_F(MultiFrameTest, CellVoltages_0x95_FallsBackToTheCachedCount) {
    // A 0x94 reply first, then an uncounted 0x95 sized from what it cached.
    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    bus.reply(DataId::STATUS_INFO, {3, 1});
    ASSERT_EQ(bus.bms.cached_cell_count(), 3);
    bus.clear();

    const std::vector<Event> events = bus.round_trip_multi(
        [&] { return bus.bms.poll_cell_voltages(); },
        {make_multi_frame(DataId::CELL_VOLTAGES, 0, {0x0D, 0x48, 0x0D, 0x49, 0x0D, 0x4A})});

    ASSERT_EQ(events.size(), 1U) << "one frame was enough for three cells";
    const CellVoltages* payload = as_cell_voltages(events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->count, 3);
    EXPECT_EQ(payload->mv[2], 3402);
}

TEST_F(MultiFrameTest, CellVoltages_0x95_WithNoCountEndsOnTheInterFrameTimeout) {
    // Neither an explicit count nor a cached one: the transfer can only end on
    // the clock, which is why tick() is not optional for this command.
    bus.bms.set_interframe_timeout_ms(100);
    ASSERT_EQ(bus.bms.cached_cell_count(), 0);
    ASSERT_EQ(bus.bms.poll_cell_voltages(), QueueStatus::QUEUED);

    bus.feed(make_multi_frame(DataId::CELL_VOLTAGES, 0, {0x0D, 0x48, 0x0D, 0x49, 0x0D, 0x4A}));
    bus.feed(make_multi_frame(DataId::CELL_VOLTAGES, 1, {0x0D, 0x4B, 0x0D, 0x4C, 0x0D, 0x4D}));
    EXPECT_TRUE(bus.events.empty()) << "nothing completes it but the clock";

    bus.bms.tick(100);

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::RESPONSE);
    const CellVoltages* payload = as_cell_voltages(bus.events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->count, 6) << "the payload is self-describing";
}

TEST_F(MultiFrameTest, CellVoltages_0x95_NeverCompletesWithoutTick) {
    // The header's warning, made concrete: every frame arrives, and the read
    // still hangs forever.
    ASSERT_EQ(bus.bms.poll_cell_voltages(), QueueStatus::QUEUED);
    bus.feed(make_multi_frame(DataId::CELL_VOLTAGES, 0, {0x0D, 0x48, 0x0D, 0x49, 0x0D, 0x4A}));

    EXPECT_TRUE(bus.events.empty());
    EXPECT_TRUE(bus.bms.busy()) << "stalled on a link that delivered everything";
}

// -----------------------------------------------------------------------------
// 0x95 on a pack that numbers against its own spec
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, CellVoltages_0x95_DecodesAPackThatCountsFromOne) {
    // The protocol document says 0x95 counts from 0. Packs exist that count
    // from 1 regardless. Filing frames under a base-corrected index left cells
    // 1-3 unwritten and shifted every real reading three places along, which is
    // what this guards.
    const std::vector<MessageBytes> frames{
        make_multi_frame(DataId::CELL_VOLTAGES, 1, {0x0D, 0x48, 0x0D, 0x49, 0x0D, 0x4A}),
        make_multi_frame(DataId::CELL_VOLTAGES, 2, {0x0D, 0x4B, 0x0D, 0x4C, 0x0D, 0x4D}),
    };

    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_cell_voltages(6); }, frames);

    ASSERT_EQ(events.size(), 1U);
    const CellVoltages* payload = as_cell_voltages(events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->count, 6);
    EXPECT_EQ(payload->mv[0], 3400) << "no leading hole where frame 0 would have been";
    EXPECT_EQ(payload->mv[1], 3401);
    EXPECT_EQ(payload->mv[2], 3402);
    EXPECT_EQ(payload->mv[3], 3403);
    EXPECT_EQ(payload->mv[4], 3404);
    EXPECT_EQ(payload->mv[5], 3405) << "and nothing fell off the end at the trim";
}

TEST_F(MultiFrameTest, CellVoltages_0x95_CapturedFromRealHardware) {
    // Byte-for-byte the three frames a real 8-cell pack put on the wire,
    // checksums included, in answer to `A5 40 95 08 ...`. It counts from 1
    // although its own protocol document specifies 0, which is the deviation
    // that started all of this. Nine cell slots arrive for eight cells; the
    // ninth is padding the count trims away.
    const std::vector<std::vector<uint8_t>> capture{
        {0xA5, 0x01, 0x95, 0x08, 0x01, 0x0B, 0xB8, 0x0B, 0xB8, 0x0B, 0xB8, 0x00, 0x8D},
        {0xA5, 0x01, 0x95, 0x08, 0x02, 0x0B, 0xB8, 0x0B, 0xB8, 0x0B, 0xB8, 0x00, 0x8E},
        {0xA5, 0x01, 0x95, 0x08, 0x03, 0x0B, 0xB8, 0x0B, 0xB8, 0x0B, 0xB8, 0x00, 0x8F},
    };

    ASSERT_EQ(bus.bms.poll_cell_voltages(8), QueueStatus::QUEUED);
    for (const std::vector<uint8_t>& frame : capture) {
        bus.feed_bytes(frame);
    }

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::RESPONSE) << "the capture's checksums are genuine";
    const CellVoltages* payload = as_cell_voltages(bus.events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->count, 8);
    for (uint8_t i = 0; i < 8; ++i) {
        EXPECT_EQ(payload->mv[i], 3000) << "cell " << static_cast<unsigned>(i + 1);
    }
}

TEST_F(MultiFrameTest, CellVoltages_0x95_FortyEightCellsCountedFromOne) {
    // The widest reply the protocol allows, on the awkward base: 48 cells over
    // 16 frames numbered 1..16. Counter 16 used to be out of range outright.
    std::vector<MessageBytes> frames;
    for (uint8_t i = 0; i < 16; ++i) {
        const auto first = static_cast<uint16_t>(3400 + (i * 3));
        const uint8_t payload[6] = {
            static_cast<uint8_t>(first >> 8),       static_cast<uint8_t>(first & 0xFF),
            static_cast<uint8_t>((first + 1) >> 8), static_cast<uint8_t>((first + 1) & 0xFF),
            static_cast<uint8_t>((first + 2) >> 8), static_cast<uint8_t>((first + 2) & 0xFF),
        };
        frames.push_back(
            make_multi_frame(DataId::CELL_VOLTAGES, static_cast<uint8_t>(i + 1), payload, 6));
    }

    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_cell_voltages(48); }, frames);

    ASSERT_EQ(events.size(), 1U);
    const CellVoltages* payload = as_cell_voltages(events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->count, 48) << "the sixteenth frame is in range";
    EXPECT_EQ(payload->mv[0], 3400);
    EXPECT_EQ(payload->mv[47], 3447);
}

TEST_F(MultiFrameTest, CellVoltages_0x95_AMissingMiddleFrameShortensTheReply) {
    // Frames 0 and 2 arrive, 1 does not. Reporting nine cells with three zeros
    // in the middle would be indistinguishable from three cells genuinely at
    // 0 mV, so the reply stops at the gap instead.
    bus.bms.set_interframe_timeout_ms(100);
    ASSERT_EQ(bus.bms.poll_cell_voltages(9), QueueStatus::QUEUED);
    bus.feed(make_multi_frame(DataId::CELL_VOLTAGES, 0, {0x0D, 0x48, 0x0D, 0x49, 0x0D, 0x4A}));
    bus.feed(make_multi_frame(DataId::CELL_VOLTAGES, 2, {0x0D, 0x4B, 0x0D, 0x4C, 0x0D, 0x4D}));
    bus.bms.tick(100);

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::RESPONSE);
    const CellVoltages* payload = as_cell_voltages(bus.events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->count, 3) << "only the unbroken run from the first frame";
    EXPECT_EQ(payload->mv[0], 3400);
    EXPECT_EQ(payload->mv[2], 3402);
}

TEST_F(MultiFrameTest, APaginatedReplyNeverInheritsThePreviousOnesBytes) {
    // 0x63 fills the reassembly buffer with version text, then a 0x95 that
    // loses its first frame must not decode that text as cell voltages.
    ASSERT_EQ(bus.bms.poll_hardware_version(), QueueStatus::QUEUED);
    for (const MessageBytes& frame :
         text_frames(DataId::HARDWARE_VERSION, "HW-V2.10-20260", 0, 2)) {
        bus.feed(frame);
    }
    bus.clear();

    bus.bms.set_interframe_timeout_ms(100);
    ASSERT_EQ(bus.bms.poll_cell_voltages(6), QueueStatus::QUEUED);
    bus.feed(make_multi_frame(DataId::CELL_VOLTAGES, 1, {0x0D, 0x4B, 0x0D, 0x4C, 0x0D, 0x4D}));
    bus.bms.tick(100);

    ASSERT_EQ(bus.events.size(), 1U);
    const CellVoltages* payload = as_cell_voltages(bus.events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->count, 3) << "the one frame that arrived, and nothing else";
    EXPECT_EQ(payload->mv[0], 3403);
    EXPECT_EQ(payload->mv[1], 3404);
    EXPECT_EQ(payload->mv[2], 3405);
}

// -----------------------------------------------------------------------------
// 0x96 cell temperatures: seven sensors per frame, counter base 0
// -----------------------------------------------------------------------------

TEST_F(MultiFrameTest, CellTemperatures_0x96_WithAnExplicitCount) {
    const std::vector<Event> events = bus.round_trip_multi(
        [&] { return bus.bms.poll_cell_temperatures(4); },
        {make_multi_frame(DataId::CELL_TEMPERATURES, 0, {65, 66, 35, 40, 0, 0, 0})});

    ASSERT_EQ(events.size(), 1U);
    const CellTemperatures* payload = as_cell_temperatures(events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->count, 4) << "trimmed to the requested count";
    EXPECT_EQ(payload->celsius[0], 25); // every byte carries the 40 offset
    EXPECT_EQ(payload->celsius[1], 26);
    EXPECT_EQ(payload->celsius[2], -5);
    EXPECT_EQ(payload->celsius[3], 0);
}

TEST_F(MultiFrameTest, CellTemperatures_0x96_SpansTwoFramesPastSeven) {
    const std::vector<MessageBytes> frames{
        make_multi_frame(DataId::CELL_TEMPERATURES, 0, {41, 42, 43, 44, 45, 46, 47}),
        make_multi_frame(DataId::CELL_TEMPERATURES, 1, {48, 49, 0, 0, 0, 0, 0}),
    };

    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_cell_temperatures(9); }, frames);

    ASSERT_EQ(events.size(), 1U);
    const CellTemperatures* payload = as_cell_temperatures(events[0]);
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->count, 9);
    EXPECT_EQ(payload->celsius[0], 1);
    EXPECT_EQ(payload->celsius[8], 9);
    EXPECT_EQ(events[0].frames, 2);
}

TEST_F(MultiFrameTest, CellTemperatures_0x96_FallsBackToTheCachedSensorCount) {
    ASSERT_EQ(bus.bms.poll_bmu_cell_temp_count(), QueueStatus::QUEUED);
    bus.reply(DataId::BMU_CELL_TEMP_COUNT, {1, 4, 0, 0, 2, 0, 0, 0});
    ASSERT_EQ(bus.bms.cached_temp_count(), 2);
    bus.clear();

    const std::vector<Event> events = bus.round_trip_multi(
        [&] { return bus.bms.poll_cell_temperatures(); },
        {make_multi_frame(DataId::CELL_TEMPERATURES, 0, {65, 35, 0, 0, 0, 0, 0})});

    ASSERT_EQ(events.size(), 1U);
    const CellTemperatures* payload = as_cell_temperatures(events[0]);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->count, 2);
    EXPECT_EQ(payload->celsius[0], 25);
    EXPECT_EQ(payload->celsius[1], -5);
}

// -----------------------------------------------------------------------------
// 0x64 fault records: one request, a stream of replies
// -----------------------------------------------------------------------------

/// One Table 1 record with plausible field values; `sum_ok` sets a valid trailing sum.
std::array<uint8_t, FAULT_RECORD_LEN> make_fault_record(uint8_t record_id, bool sum_ok) {
    std::array<uint8_t, FAULT_RECORD_LEN> record{};
    record[0x00] = 26; // year, offset by 2000
    record[0x01] = 9;
    record[0x02] = 2;
    record[0x03] = 14;
    record[0x04] = 35;
    record[0x05] = 59;
    record[0x06] = record_id;
    record[0x07] = 1;    // the condition arose
    record[0x08] = 0x02; // total voltage 560 -> 56.0 V
    record[0x09] = 0x30;
    record[0x0A] = 0x71; // current 29000 -> -100.0 A
    record[0x0B] = 0x48;
    record[0x0C] = 0x03; // soc 800 -> 80.0 %
    record[0x0D] = 0x20;
    record[0x0E] = 0x03; // both MOSFETs conducting
    record[0x0F] = 0x0D; // highest cell 3400 mV
    record[0x10] = 0x48;
    record[0x11] = 7;
    record[0x12] = 0x0D; // lowest cell 3333 mV
    record[0x13] = 0x05;
    record[0x14] = 12;
    record[0x15] = 70; // highest temperature 30 C
    record[0x16] = 3;
    record[0x17] = 35; // lowest temperature -5 C
    record[0x18] = 1;
    record[0x19] = 0x2A; // fault code

    uint8_t sum = 0;
    for (size_t i = 0; i < 0x1A; ++i) {
        sum = static_cast<uint8_t>(sum + record[i]);
    }
    record[0x1A] = sum_ok ? sum : static_cast<uint8_t>(sum + 1);
    return record;
}

/// Splits a record into the four 7-byte frames the protocol carries it in.
std::vector<MessageBytes> fault_frames(const std::array<uint8_t, FAULT_RECORD_LEN>& record,
                                       uint8_t first_counter) {
    std::array<uint8_t, 28> padded{}; // four frames carry 28 bytes; the last is slack
    std::copy(record.begin(), record.end(), padded.begin());

    std::vector<MessageBytes> frames;
    for (size_t i = 0; i < 4; ++i) {
        frames.push_back(make_multi_frame(DataId::FAULT_RECORDS,
                                          static_cast<uint8_t>(first_counter + i),
                                          padded.data() + (i * 7), 7));
    }
    return frames;
}

MessageBytes fault_sentinel() {
    return make_multi_frame(DataId::FAULT_RECORDS, 0xFF, {0, 0, 0, 0, 0, 0, 0});
}

TEST_F(MultiFrameTest, FaultRecords_0x64_DecodesOneRecordFromFourFrames) {
    const std::vector<MessageBytes> frames =
        fault_frames(make_fault_record(static_cast<uint8_t>(FaultRecordId::OVERCURRENT), true), 0);

    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_fault_records(); }, frames);

    ASSERT_EQ(events.size(), 1U);
    const FaultRecord* record = as_fault_record(events[0]);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->year, 2026);
    EXPECT_EQ(record->month, 9);
    EXPECT_EQ(record->day, 2);
    EXPECT_EQ(record->hour, 14);
    EXPECT_EQ(record->minute, 35);
    EXPECT_EQ(record->second, 59);
    EXPECT_EQ(record->record_id, static_cast<uint8_t>(FaultRecordId::OVERCURRENT));
    EXPECT_TRUE(record->occurred);
    EXPECT_EQ(record->total_voltage_dv, 560);
    EXPECT_EQ(record->current_da, -1000);
    EXPECT_EQ(record->soc_pm, 800);
    EXPECT_TRUE(record->charge_mos_on);
    EXPECT_TRUE(record->discharge_mos_on);
    EXPECT_EQ(record->highest_cell_mv, 3400);
    EXPECT_EQ(record->highest_cell_number, 7);
    EXPECT_EQ(record->lowest_cell_mv, 3333);
    EXPECT_EQ(record->lowest_cell_number, 12);
    EXPECT_EQ(record->highest_temp_c, 30);
    EXPECT_EQ(record->highest_temp_number, 3);
    EXPECT_EQ(record->lowest_temp_c, -5);
    EXPECT_EQ(record->lowest_temp_number, 1);
    EXPECT_EQ(record->fault_code, 0x2A);
    EXPECT_TRUE(record->checksum_ok);
    EXPECT_EQ(events[0].frames, 4);
}

TEST_F(MultiFrameTest, FaultRecords_0x64_StreamsOneEventPerRecord) {
    ASSERT_EQ(bus.bms.poll_fault_records(), QueueStatus::QUEUED);

    for (const MessageBytes& frame : fault_frames(
             make_fault_record(static_cast<uint8_t>(FaultRecordId::START_CHARGING), true), 0)) {
        bus.feed(frame);
    }
    for (const MessageBytes& frame : fault_frames(
             make_fault_record(static_cast<uint8_t>(FaultRecordId::END_CHARGING), true), 4)) {
        bus.feed(frame);
    }

    ASSERT_EQ(bus.events.size(), 2U) << "one request, a stream of records";
    ASSERT_NE(as_fault_record(bus.events[0]), nullptr);
    ASSERT_NE(as_fault_record(bus.events[1]), nullptr);
    EXPECT_EQ(as_fault_record(bus.events[0])->record_id,
              static_cast<uint8_t>(FaultRecordId::START_CHARGING));
    EXPECT_EQ(as_fault_record(bus.events[1])->record_id,
              static_cast<uint8_t>(FaultRecordId::END_CHARGING));
    EXPECT_TRUE(bus.bms.busy()) << "the stream has not been terminated yet";
}

TEST_F(MultiFrameTest, FaultRecords_0x64_SentinelEndsTheStreamWithoutAnEvent) {
    ASSERT_EQ(bus.bms.poll_fault_records(), QueueStatus::QUEUED);
    for (const MessageBytes& frame : fault_frames(make_fault_record(0x03, true), 0)) {
        bus.feed(frame);
    }
    ASSERT_EQ(bus.events.size(), 1U);

    bus.feed(fault_sentinel());

    EXPECT_EQ(bus.events.size(), 1U) << "the 0xFF sentinel itself produces no event";
    EXPECT_FALSE(bus.bms.busy());
    EXPECT_EQ(bus.bms.pending(), 0U);
}

TEST_F(MultiFrameTest, FaultRecords_0x64_AnEmptyLogIsJustTheSentinel) {
    ASSERT_EQ(bus.bms.poll_fault_records(), QueueStatus::QUEUED);
    bus.feed(fault_sentinel());

    EXPECT_TRUE(bus.events.empty()) << "no records, no events";
    EXPECT_FALSE(bus.bms.busy());
}

TEST_F(MultiFrameTest, FaultRecords_0x64_ABadTrailingSumIsAdvisoryNotFatal) {
    // Table 1's checksum has an ambiguous byte range, so a mismatch is reported
    // on the record rather than causing it to be dropped.
    const std::vector<MessageBytes> frames = fault_frames(make_fault_record(0x05, false), 0);
    const std::vector<Event> events =
        bus.round_trip_multi([&] { return bus.bms.poll_fault_records(); }, frames);

    ASSERT_EQ(events.size(), 1U) << "the record is still delivered";
    const FaultRecord* record = as_fault_record(events[0]);
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->checksum_ok);
    EXPECT_EQ(record->record_id, 0x05) << "and its fields are still decoded";
}

TEST_F(MultiFrameTest, FaultRecords_0x64_AStreamThatStopsWithoutTheSentinelIsTruncated) {
    bus.bms.set_interframe_timeout_ms(100);
    ASSERT_EQ(bus.bms.poll_fault_records(), QueueStatus::QUEUED);

    const std::vector<MessageBytes> frames = fault_frames(make_fault_record(0x07, true), 0);
    bus.feed(frames[0]);
    bus.feed(frames[1]); // half a record, then silence
    EXPECT_TRUE(bus.events.empty());

    bus.bms.tick(100);

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(bus.events[0].error, ErrorCode::TRUNCATED)
        << "unlike a paginated read, an unterminated stream is an error";
    EXPECT_EQ(bus.events[0].frames, 2);
    EXPECT_FALSE(bus.bms.busy());
}

TEST_F(MultiFrameTest, FaultRecords_0x64_PerRequestHandlerFiresOncePerRecord) {
    Sink sink;
    ASSERT_EQ(bus.bms.poll_fault_records(&Sink::fn, &sink), QueueStatus::QUEUED);

    for (const MessageBytes& frame : fault_frames(make_fault_record(0x01, true), 0)) {
        bus.feed(frame);
    }
    for (const MessageBytes& frame : fault_frames(make_fault_record(0x02, true), 4)) {
        bus.feed(frame);
    }
    bus.feed(fault_sentinel());

    EXPECT_EQ(sink.events.size(), 2U) << "the poll's own handler sees every record";
}

} // namespace
} // namespace haidi_test
