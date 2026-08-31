// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Tests for include/haidi_frame_parser.hpp: the byte-stream framer that turns
// arbitrarily chunked serial input into whole, checksum-verified frames.

#include "haidi_test_util.hpp"

#include <haidi_frame_parser.hpp>

#include <gtest/gtest.h>

namespace haidi_test
{
namespace
{

// Collects whatever the parser emits.
struct FrameSink {
    std::vector<MessageBytes> frames;
    std::vector<FrameError> errors;

    static void on_frame(void* ctx, const MessageBytes& frame) {
        static_cast<FrameSink*>(ctx)->frames.push_back(frame);
    }
    static void on_error(void* ctx, FrameError error) {
        static_cast<FrameSink*>(ctx)->errors.push_back(error);
    }
};

class FrameParserTest : public ::testing::Test {
    protected:
    FrameParser parser;
    FrameSink sink;

    void feed(const std::vector<uint8_t>& bytes) {
        parser.feed(bytes.data(), bytes.size(), &FrameSink::on_frame, &sink, &FrameSink::on_error);
    }

    // Feeds one byte at a time, to prove frames survive arbitrary chunking.
    void feed_bytewise(const std::vector<uint8_t>& bytes) {
        for (uint8_t b : bytes) {
            parser.feed(&b, 1, &FrameSink::on_frame, &sink, &FrameSink::on_error);
        }
    }
};

// -----------------------------------------------------------------------------
// The happy path
// -----------------------------------------------------------------------------

TEST_F(FrameParserTest, EmitsAWholeFrameFedInOneCall) {
    const MessageBytes frame = make_reply(DataId::STATUS_INFO, {1, 2, 3, 4, 5, 6, 7, 8});
    feed(to_bytes(frame));

    ASSERT_EQ(sink.frames.size(), 1U);
    EXPECT_EQ(sink.frames[0], frame);
    EXPECT_TRUE(sink.errors.empty());
    EXPECT_EQ(parser.buffered(), 0U);
    EXPECT_EQ(parser.discarded(), 0U);
}

TEST_F(FrameParserTest, ReassemblesAFrameSplitAcrossCalls) {
    const MessageBytes frame = make_reply(DataId::RTC, {26, 9, 2, 12, 30, 0, 0, 0});
    feed_bytewise(to_bytes(frame));

    ASSERT_EQ(sink.frames.size(), 1U);
    EXPECT_EQ(sink.frames[0], frame);
    EXPECT_EQ(parser.buffered(), 0U);
}

TEST_F(FrameParserTest, EmitsBothOfTwoBackToBackFrames) {
    const MessageBytes first = make_reply(DataId::BATTERY_STATUS, {0x03, 0xE8});
    const MessageBytes second = make_reply(DataId::BOARD_NUMBER, {2, 4});

    std::vector<uint8_t> stream = to_bytes(first);
    const std::vector<uint8_t> tail = to_bytes(second);
    stream.insert(stream.end(), tail.begin(), tail.end());
    feed(stream);

    ASSERT_EQ(sink.frames.size(), 2U);
    EXPECT_EQ(sink.frames[0], first);
    EXPECT_EQ(sink.frames[1], second);
}

TEST_F(FrameParserTest, BufferedReportsPartialFrameLength) {
    const MessageBytes frame = make_reply(DataId::STATUS_INFO, {});
    std::vector<uint8_t> partial = to_bytes(frame);
    partial.resize(6);
    feed(partial);

    EXPECT_EQ(parser.buffered(), 6U);
    EXPECT_TRUE(sink.frames.empty());
}

// -----------------------------------------------------------------------------
// Junk and structural rejection
// -----------------------------------------------------------------------------

TEST_F(FrameParserTest, CountsLeadingJunkWithoutReportingAnError) {
    // The header is explicit: a byte that simply is not a start flag is counted
    // by discarded(), not reported, so a resynchronising link does not produce
    // one error callback per junk byte.
    const MessageBytes frame = make_reply(DataId::FAULT_STATUS, {});
    std::vector<uint8_t> stream{0x00, 0x11, 0x22, 0x33};
    const std::vector<uint8_t> tail = to_bytes(frame);
    stream.insert(stream.end(), tail.begin(), tail.end());
    feed(stream);

    ASSERT_EQ(sink.frames.size(), 1U);
    EXPECT_EQ(sink.frames[0], frame);
    EXPECT_EQ(parser.discarded(), 4U);
    EXPECT_TRUE(sink.errors.empty()) << "junk bytes must not raise FrameError";
}

TEST_F(FrameParserTest, RejectsABadLengthFieldAfterFourBytes) {
    MessageBytes frame = make_reply(DataId::STATUS_INFO, {});
    frame[MSG_LEN_INDEX] = 0x07; // not LENGTH_FLAG
    frame[MSG_LAST_INDEX] = calc_chksum(frame);

    // Only the first four bytes are needed for the parser to give up: the
    // length check exists precisely so a false 0xA5 is pruned early.
    std::vector<uint8_t> head = to_bytes(frame);
    head.resize(4);
    feed(head);

    ASSERT_EQ(sink.errors.size(), 1U);
    EXPECT_EQ(sink.errors[0], FrameError::BAD_LENGTH);
    EXPECT_TRUE(sink.frames.empty());
}

TEST_F(FrameParserTest, RejectsABadChecksum) {
    const MessageBytes frame = corrupt_checksum(make_reply(DataId::STATUS_INFO, {1, 2, 3}));
    feed(to_bytes(frame));

    ASSERT_EQ(sink.errors.size(), 1U);
    EXPECT_EQ(sink.errors[0], FrameError::CHECKSUM);
    EXPECT_TRUE(sink.frames.empty());
}

// -----------------------------------------------------------------------------
// Resynchronisation
// -----------------------------------------------------------------------------

TEST_F(FrameParserTest, AGenuineFrameBeginningInsideACorruptOneStillSurvives) {
    // This is the behaviour the header calls out: on a bad frame the parser
    // drops a single byte and rescans, rather than discarding the buffer. It is
    // the common case after one dropped byte on the wire.
    const MessageBytes good = make_reply(DataId::BATTERY_STATUS, {0x01, 0x02});

    // A truncated frame (start flag, valid length, then cut short) immediately
    // followed by a real one.
    std::vector<uint8_t> stream{START_FLAG, BMS_ADDRESS, 0x90, LENGTH_FLAG, 0xAA, 0xBB};
    const std::vector<uint8_t> tail = to_bytes(good);
    stream.insert(stream.end(), tail.begin(), tail.end());
    feed(stream);

    ASSERT_EQ(sink.frames.size(), 1U) << "the real frame must survive the corrupt prefix";
    EXPECT_EQ(sink.frames[0], good);
}

TEST_F(FrameParserTest, RecoversOnTheNextFrameAfterABadChecksum) {
    const MessageBytes bad = corrupt_checksum(make_reply(DataId::STATUS_INFO, {9}));
    const MessageBytes good = make_reply(DataId::BOARD_NUMBER, {1, 2});

    feed(to_bytes(bad));
    feed(to_bytes(good));

    EXPECT_EQ(sink.errors.size(), 1U);
    ASSERT_EQ(sink.frames.size(), 1U);
    EXPECT_EQ(sink.frames[0], good);
}

// -----------------------------------------------------------------------------
// reset() and defensive inputs
// -----------------------------------------------------------------------------

TEST_F(FrameParserTest, ResetDropsThePartialFrameAndZeroesDiscarded) {
    feed({0x00, 0x00}); // two junk bytes
    std::vector<uint8_t> partial = to_bytes(make_reply(DataId::RTC, {}));
    partial.resize(5);
    feed(partial); // five bytes of a real frame

    EXPECT_EQ(parser.discarded(), 2U);
    EXPECT_EQ(parser.buffered(), 5U);

    parser.reset();

    EXPECT_EQ(parser.discarded(), 0U);
    EXPECT_EQ(parser.buffered(), 0U);
}

TEST_F(FrameParserTest, ResetMeansTheRemainderOfAFrameIsNoLongerAccepted) {
    const MessageBytes frame = make_reply(DataId::RTC, {1, 2, 3, 4, 5, 6, 7, 8});
    const std::vector<uint8_t> bytes = to_bytes(frame);

    feed({bytes.begin(), bytes.begin() + 6});
    parser.reset();
    feed({bytes.begin() + 6, bytes.end()});

    EXPECT_TRUE(sink.frames.empty()) << "the tail alone must not complete a frame";
}

TEST_F(FrameParserTest, NullDataIsIgnored) {
    parser.feed(nullptr, 13, &FrameSink::on_frame, &sink, &FrameSink::on_error);

    EXPECT_TRUE(sink.frames.empty());
    EXPECT_EQ(parser.buffered(), 0U);
    EXPECT_EQ(parser.discarded(), 0U);
}

TEST_F(FrameParserTest, ANullErrorHandlerIsSafeOnABadFrame) {
    const MessageBytes bad = corrupt_checksum(make_reply(DataId::STATUS_INFO, {}));
    const std::vector<uint8_t> bytes = to_bytes(bad);

    // on_error defaults to nullptr; a rejected frame must not dereference it.
    parser.feed(bytes.data(), bytes.size(), &FrameSink::on_frame, &sink);

    EXPECT_TRUE(sink.frames.empty());
    EXPECT_TRUE(sink.errors.empty());
}

TEST_F(FrameParserTest, ANullFrameSinkIsSafeOnAGoodFrame) {
    const MessageBytes frame = make_reply(DataId::STATUS_INFO, {});
    const std::vector<uint8_t> bytes = to_bytes(frame);

    parser.feed(bytes.data(), bytes.size(), nullptr, &sink, &FrameSink::on_error);

    EXPECT_EQ(parser.buffered(), 0U) << "the frame is still consumed";
}

TEST_F(FrameParserTest, ZeroLengthFeedIsANoOp) {
    const uint8_t byte = START_FLAG;
    parser.feed(&byte, 0, &FrameSink::on_frame, &sink, &FrameSink::on_error);

    EXPECT_EQ(parser.buffered(), 0U);
    EXPECT_EQ(parser.discarded(), 0U);
}

// -----------------------------------------------------------------------------
// Direction independence
// -----------------------------------------------------------------------------

TEST_F(FrameParserTest, AcceptsAHostRequestFrameToo) {
    // The framer knows nothing about Data IDs or direction: it enforces only the
    // two structural invariants that hold both ways.
    const MessageBytes request = encode(HostAddress::COMP, DataId::TOTAL_VOLTAGE_CURRENT_SOC);
    feed(to_bytes(request));

    ASSERT_EQ(sink.frames.size(), 1U);
    EXPECT_EQ(sink.frames[0], request);
}

} // namespace
} // namespace haidi_test
