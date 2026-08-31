// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Tests for include/haidi_bms.hpp: queueing, transaction lifecycle, timeouts and
// retries, event dispatch, introspection and reset.
//
// Per-command decoding lives in test_commands.cpp and test_multiframe.cpp; this
// file is about the session mechanics that are the same whatever the command.

#include "haidi_test_util.hpp"

#include <gtest/gtest.h>

namespace haidi_test
{
namespace
{

class SessionTest : public ::testing::Test {
    protected:
    Loopback bus;
};

// -----------------------------------------------------------------------------
// Construction and addressing
// -----------------------------------------------------------------------------

TEST_F(SessionTest, DefaultsToTheUpperComputerAddress) {
    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.sent.size(), 1U);
    EXPECT_EQ(bus.last_sent()[MSG_ADDRESS_INDEX], static_cast<uint8_t>(HostAddress::COMP));
}

TEST(SessionAddressing, RequestsCarryTheConfiguredHostAddress) {
    Loopback bus{HostAddress::BLE};
    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    EXPECT_EQ(bus.last_sent()[MSG_ADDRESS_INDEX], static_cast<uint8_t>(HostAddress::BLE));
}

TEST(SessionAddressing, RepliesFromAnotherAddressAreRejected) {
    // A session told the BMS lives at 0x02 must not accept 0x01's traffic.
    Loopback bus{HostAddress::COMP, 0x02};
    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);

    bus.feed(make_reply(DataId::STATUS_INFO, DataBytes{}, BMS_ADDRESS));

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(bus.events[0].error, ErrorCode::BAD_SOURCE_ADDRESS);

    // The real reply may still be in flight, so the transaction stays open.
    EXPECT_TRUE(bus.bms.busy());
}

TEST(SessionAddressing, ARejectedSourceStillLetsTheRealReplyThrough) {
    Loopback bus{HostAddress::COMP, 0x02};
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);

    bus.feed(make_reply(DataId::BATTERY_STATUS, DataBytes{}, 0x7E)); // impostor
    bus.feed(make_reply(DataId::BATTERY_STATUS, bytes8({0x03, 0xE8}), 0x02));

    ASSERT_EQ(bus.events.size(), 2U);
    EXPECT_EQ(bus.events[0].error, ErrorCode::BAD_SOURCE_ADDRESS);
    ASSERT_NE(as_battery_status(bus.events[1]), nullptr);
    EXPECT_EQ(as_battery_status(bus.events[1])->soh_pm, 1000);
    EXPECT_FALSE(bus.bms.busy());
}

// -----------------------------------------------------------------------------
// QueueStatus
// -----------------------------------------------------------------------------

TEST_F(SessionTest, AcceptedRequestsReportQueued) {
    EXPECT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    EXPECT_EQ(bus.sent.size(), 1U) << "the first request goes out immediately";
}

TEST(SessionQueueing, WithoutATxHandlerEveryRequestIsRefused) {
    HaidiBMS bms;
    EXPECT_EQ(bms.poll_capacity_voltage(), QueueStatus::NO_TX_HANDLER);
    EXPECT_EQ(bms.pending(), 0U);
    EXPECT_FALSE(bms.busy());
}

TEST(SessionQueueing, TheTxHandlerCheckPrecedesTheDataIdCheck) {
    // enqueue() tests for a transmit handler first, so a request that is both
    // unroutable and malformed reports the missing handler.
    HaidiBMS bms;
    EXPECT_EQ(bms.poll_alarm_threshold(static_cast<AlarmClass>(9), static_cast<AlarmLevel>(3)),
              QueueStatus::NO_TX_HANDLER);
}

TEST_F(SessionTest, AnOutOfRangeAlarmThresholdIsABadRequest) {
    // An out-of-range level pushes the computed Data ID past 0x8A, and an
    // out-of-range class does the same at the top level.
    EXPECT_EQ(
        bus.bms.poll_alarm_threshold(AlarmClass::CELL_OVERVOLTAGE, static_cast<AlarmLevel>(3)),
        QueueStatus::BAD_REQUEST);
    EXPECT_EQ(bus.bms.poll_alarm_threshold(static_cast<AlarmClass>(9), AlarmLevel::LEVEL_3),
              QueueStatus::BAD_REQUEST);
    EXPECT_EQ(
        bus.bms.poll_alarm_threshold(static_cast<AlarmClass>(200), static_cast<AlarmLevel>(200)),
        QueueStatus::BAD_REQUEST);

    EXPECT_TRUE(bus.sent.empty()) << "a refused request must not reach the wire";
    EXPECT_EQ(bus.bms.pending(), 0U);
}

TEST_F(SessionTest, AnOutOfRangeAlarmClassNeverAliasesOntoAValidCommand) {
    // alarm_threshold_id() is plain arithmetic -- 0x70 + level*9 + class -- so
    // an out-of-range pair can land back inside the valid 0x70..0x8A block
    // instead of escaping it. Class 9 at level 1 computes 0x79, a real command
    // meaning cell overvoltage level 2, so before the range check this call
    // was accepted and quietly read a different alarm than the one asked for.
    for (uint8_t cls = ALARM_CLASS_COUNT; cls < 16; ++cls) {
        for (uint8_t lvl = 0; lvl < ALARM_LEVEL_COUNT; ++lvl) {
            bus.clear();
            bus.bms.reset();

            EXPECT_EQ(bus.bms.poll_alarm_threshold(static_cast<AlarmClass>(cls),
                                                   static_cast<AlarmLevel>(lvl)),
                      QueueStatus::BAD_REQUEST)
                << "class " << +cls << " level " << +lvl;
            EXPECT_TRUE(bus.sent.empty())
                << "class " << +cls << " level " << +lvl << " put a frame on the wire";
        }
    }
}

TEST_F(SessionTest, AnOutOfRangeAlarmLevelNeverAliasesOntoAValidCommand) {
    for (uint8_t lvl = ALARM_LEVEL_COUNT; lvl < 8; ++lvl) {
        for (uint8_t cls = 0; cls < ALARM_CLASS_COUNT; ++cls) {
            bus.clear();
            bus.bms.reset();

            EXPECT_EQ(bus.bms.poll_alarm_threshold(static_cast<AlarmClass>(cls),
                                                   static_cast<AlarmLevel>(lvl)),
                      QueueStatus::BAD_REQUEST)
                << "class " << +cls << " level " << +lvl;
            EXPECT_TRUE(bus.sent.empty());
        }
    }
}

TEST_F(SessionTest, EveryValidAlarmClassAndLevelIsAccepted) {
    for (uint8_t lvl = 0; lvl < ALARM_LEVEL_COUNT; ++lvl) {
        for (uint8_t cls = 0; cls < ALARM_CLASS_COUNT; ++cls) {
            const auto klass = static_cast<AlarmClass>(cls);
            const auto level = static_cast<AlarmLevel>(lvl);

            bus.clear();
            bus.bms.reset();
            ASSERT_EQ(bus.bms.poll_alarm_threshold(klass, level), QueueStatus::QUEUED);
            ASSERT_EQ(bus.sent.size(), 1U);
            EXPECT_EQ(bus.last_sent_id(), alarm_threshold_id(klass, level));
        }
    }
}

TEST_F(SessionTest, TheQueueRefusesWorkPastItsCapacity) {
    for (size_t i = 0; i < HaidiBMS::QUEUE_CAPACITY; ++i) {
        EXPECT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED) << "request " << i;
    }

    EXPECT_EQ(bus.bms.pending(), HaidiBMS::QUEUE_CAPACITY);
    EXPECT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUE_FULL);
    EXPECT_EQ(bus.bms.pending(), HaidiBMS::QUEUE_CAPACITY) << "a refused request must not count";
    EXPECT_EQ(bus.sent.size(), 1U) << "only the head request is ever on the wire";
}

// -----------------------------------------------------------------------------
// One transaction at a time (protocol section 5.3)
// -----------------------------------------------------------------------------

TEST_F(SessionTest, OnlyOneRequestIsOutstandingAtATime) {
    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);

    EXPECT_EQ(bus.sent.size(), 1U) << "two data items cannot be read at once";
    EXPECT_EQ(bus.last_sent_id(), DataId::CAPACITY_VOLTAGE);
    EXPECT_EQ(bus.bms.pending(), 2U);
}

TEST_F(SessionTest, QueuedRequestsGoOutInOrderAsEarlierOnesComplete) {
    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_board_number(), QueueStatus::QUEUED);

    bus.reply(DataId::CAPACITY_VOLTAGE, {});
    ASSERT_EQ(bus.sent.size(), 2U);
    EXPECT_EQ(bus.last_sent_id(), DataId::BATTERY_STATUS);

    bus.reply(DataId::BATTERY_STATUS, {});
    ASSERT_EQ(bus.sent.size(), 3U);
    EXPECT_EQ(bus.last_sent_id(), DataId::BOARD_NUMBER);

    bus.reply(DataId::BOARD_NUMBER, {});
    EXPECT_EQ(bus.bms.pending(), 0U);
    EXPECT_FALSE(bus.bms.busy());
}

// -----------------------------------------------------------------------------
// Timeouts and retries
// -----------------------------------------------------------------------------

TEST_F(SessionTest, DefaultTimeoutPolicyIsExposedAsConstants) {
    EXPECT_EQ(HaidiBMS::DEFAULT_RESPONSE_TIMEOUT_MS, 250U);
    EXPECT_EQ(HaidiBMS::DEFAULT_INTERFRAME_TIMEOUT_MS, 250U);
    EXPECT_EQ(HaidiBMS::DEFAULT_MAX_RETRIES, 1U);
    EXPECT_EQ(HaidiBMS::QUEUE_CAPACITY, HAIDI_REQUEST_QUEUE_CAPACITY);
}

TEST_F(SessionTest, AnUnansweredRequestIsRetriedThenTimesOut) {
    bus.bms.set_response_timeout_ms(100);
    bus.bms.set_max_retries(2);

    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    EXPECT_EQ(bus.sent.size(), 1U);

    bus.bms.tick(100); // first deadline: retransmit
    EXPECT_EQ(bus.sent.size(), 2U);
    EXPECT_TRUE(bus.events.empty());

    bus.bms.tick(200); // second deadline: retransmit again
    EXPECT_EQ(bus.sent.size(), 3U);
    EXPECT_TRUE(bus.events.empty());

    bus.bms.tick(300); // retries exhausted
    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::TIMEOUT);
    EXPECT_EQ(bus.events[0].id, DataId::CAPACITY_VOLTAGE);
    EXPECT_EQ(bus.events[0].error, ErrorCode::NONE);
    EXPECT_EQ(bus.events[0].retries, 2);
    EXPECT_EQ(bus.sent.size(), 3U) << "no fourth transmission after giving up";
    EXPECT_FALSE(bus.bms.busy());
}

TEST_F(SessionTest, ZeroRetriesTimesOutWithoutRetransmitting) {
    bus.bms.set_response_timeout_ms(50);
    bus.bms.set_max_retries(0);

    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED);
    bus.bms.tick(50);

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::TIMEOUT);
    EXPECT_EQ(bus.events[0].retries, 0);
    EXPECT_EQ(bus.sent.size(), 1U);
}

TEST_F(SessionTest, TheDeadlineIsNotReachedEarly) {
    bus.bms.set_response_timeout_ms(100);
    bus.bms.set_max_retries(0);

    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED);
    bus.bms.tick(99);
    EXPECT_TRUE(bus.events.empty());

    bus.bms.tick(100);
    EXPECT_EQ(bus.events.size(), 1U);
}

TEST_F(SessionTest, RetryCountIsPerTransactionNotCumulative) {
    bus.bms.set_response_timeout_ms(100);
    bus.bms.set_max_retries(1);

    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_board_number(), QueueStatus::QUEUED);

    bus.bms.tick(100); // retry the RTC read
    bus.bms.tick(200); // give up on it; the board-number read goes out
    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].retries, 1);

    bus.bms.tick(300); // retry the board-number read
    bus.bms.tick(400); // give up on that too
    ASSERT_EQ(bus.events.size(), 2U);
    EXPECT_EQ(bus.events[1].id, DataId::BOARD_NUMBER);
    EXPECT_EQ(bus.events[1].retries, 1) << "the second transaction started from zero";
}

TEST(SessionTiming, DeadlinesSurviveTheMillisecondCounterWrappingAt2Pow32) {
    // A plain `now >= deadline` breaks every 49.7 days. Straddle the wrap.
    Loopback bus;
    bus.bms.set_response_timeout_ms(100);
    bus.bms.set_max_retries(0);

    bus.bms.tick(0xFFFFFF00U);
    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED); // deadline 0xFFFFFF64

    bus.bms.tick(0xFFFFFF50U); // still before the deadline
    EXPECT_TRUE(bus.events.empty());

    bus.bms.tick(0x00000010U); // past it, having wrapped
    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::TIMEOUT);
}

TEST_F(SessionTest, WithoutTickASingleLostReplyStallsTheSessionPermanently) {
    // The header's warning is an API contract, not a footnote: deadlines are
    // examined nowhere but tick(), so a session that is never ticked cannot
    // give up on anything. The first outward symptom is QUEUE_FULL, which says
    // nothing about the cause -- which is exactly why this is worth pinning.
    bus.bms.set_response_timeout_ms(1);
    bus.bms.set_max_retries(3);

    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);

    for (size_t i = 1; i < HaidiBMS::QUEUE_CAPACITY; ++i) {
        EXPECT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);
    }
    EXPECT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUE_FULL);

    EXPECT_EQ(bus.sent.size(), 1U) << "no retries happened without tick()";
    EXPECT_TRUE(bus.events.empty()) << "and no timeout was ever reported";
    EXPECT_TRUE(bus.bms.busy());
}

// -----------------------------------------------------------------------------
// Event dispatch
// -----------------------------------------------------------------------------

TEST_F(SessionTest, PerRequestHandlerRunsBeforeTheSessionWideOneAndBothRun) {
    Sink sink;
    sink.name = "per-request";
    sink.order = &bus.order;

    ASSERT_EQ(bus.bms.poll_battery_status(&Sink::fn, &sink), QueueStatus::QUEUED);
    bus.reply(DataId::BATTERY_STATUS, {0x01, 0xF4});

    ASSERT_EQ(sink.events.size(), 1U);
    ASSERT_EQ(bus.events.size(), 1U);
    ASSERT_EQ(bus.order.size(), 2U);
    EXPECT_EQ(bus.order[0], "per-request");
    EXPECT_EQ(bus.order[1], "global");
}

TEST_F(SessionTest, PerRequestHandlersSeeTimeoutsAndProtocolErrorsToo) {
    Sink on_timeout;
    bus.bms.set_response_timeout_ms(10);
    bus.bms.set_max_retries(0);

    ASSERT_EQ(bus.bms.poll_rtc(&Sink::fn, &on_timeout), QueueStatus::QUEUED);
    bus.bms.tick(10);

    ASSERT_EQ(on_timeout.events.size(), 1U);
    EXPECT_EQ(on_timeout.events[0].kind, EventKind::TIMEOUT);

    Sink on_error;
    ASSERT_EQ(bus.bms.poll_rtc(&Sink::fn, &on_error), QueueStatus::QUEUED);
    bus.feed(corrupt_checksum(make_reply(DataId::RTC, {})));

    ASSERT_EQ(on_error.events.size(), 1U);
    EXPECT_EQ(on_error.events[0].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(on_error.events[0].error, ErrorCode::CHECKSUM);
}

TEST_F(SessionTest, EachRequestGetsItsOwnHandlerAndContext) {
    Sink first;
    Sink second;

    ASSERT_EQ(bus.bms.poll_capacity_voltage(&Sink::fn, &first), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_battery_status(&Sink::fn, &second), QueueStatus::QUEUED);

    bus.reply(DataId::CAPACITY_VOLTAGE, {});
    EXPECT_EQ(first.events.size(), 1U);
    EXPECT_TRUE(second.events.empty());

    bus.reply(DataId::BATTERY_STATUS, {});
    EXPECT_EQ(first.events.size(), 1U) << "the first handler must not see the second reply";
    EXPECT_EQ(second.events.size(), 1U);
}

TEST_F(SessionTest, EventsBelongingToNoRequestReachOnlyTheSessionWideHandler) {
    Sink sink;
    bus.bms.set_response_timeout_ms(10);
    bus.bms.set_max_retries(0);

    ASSERT_EQ(bus.bms.poll_rtc(&Sink::fn, &sink), QueueStatus::QUEUED);
    bus.bms.tick(10); // the transaction is abandoned
    ASSERT_EQ(sink.events.size(), 1U);
    EXPECT_EQ(sink.events[0].kind, EventKind::TIMEOUT);

    bus.reply(DataId::RTC, {}); // the reply finally turns up, too late

    EXPECT_EQ(sink.events.size(), 1U) << "the orphaned reply must not reach the retired handler";
    ASSERT_EQ(bus.events.size(), 2U);
    EXPECT_EQ(bus.events[1].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(bus.events[1].error, ErrorCode::UNEXPECTED_DATA_ID);
}

TEST_F(SessionTest, ASessionWithNoHandlersAtAllStillPollsAndCompletes) {
    HaidiBMS bms;
    std::vector<MessageBytes> sent;
    bms.set_tx_handler(
        [](void* ctx, const uint8_t* data, size_t len) {
            MessageBytes frame{};
            std::memcpy(frame.data(), data, len);
            static_cast<std::vector<MessageBytes>*>(ctx)->push_back(frame);
        },
        &sent);

    ASSERT_EQ(bms.poll_battery_status(), QueueStatus::QUEUED);
    const MessageBytes reply = make_reply(DataId::BATTERY_STATUS, {});
    bms.feed(reply.data(), reply.size()); // no event handler: result is discarded

    EXPECT_FALSE(bms.busy());
    EXPECT_EQ(bms.pending(), 0U);
}

TEST_F(SessionTest, APollIssuedFromInsideAHandlerIsQueuedNotTransmittedInline) {
    size_t sent_during_dispatch = 0;

    bus.on_event_hook = [&sent_during_dispatch](Loopback& self, const Event&) {
        if (self.sent.size() == 1) { // only react to the first reply
            const size_t before = self.sent.size();
            EXPECT_EQ(self.bms.poll_fault_status(), QueueStatus::QUEUED);
            sent_during_dispatch = self.sent.size() - before;
        }
    };

    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    bus.reply(DataId::CAPACITY_VOLTAGE, {});

    EXPECT_EQ(sent_during_dispatch, 0U) << "nothing may go out mid-callback";
    ASSERT_EQ(bus.sent.size(), 2U) << "but it goes out once the handler returns";
    EXPECT_EQ(bus.last_sent_id(), DataId::FAULT_STATUS);
    EXPECT_TRUE(bus.bms.busy());
}

// emit()'s own comment promises this works: "a callback is free to call
// reset(), which retires the head request underneath us". It did not, until
// complete() learned to skip the pop when reset() had already retired the
// transaction -- without that guard `--queue_count_` wrapped past zero and
// pump() went on to transmit from a slot reset() had abandoned.
TEST_F(SessionTest, AHandlerMayResetTheSessionFromInsideDispatch) {
    bus.on_event_hook = [](Loopback& self, const Event&) { self.bms.reset(); };

    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);
    bus.reply(DataId::CAPACITY_VOLTAGE, {});

    EXPECT_FALSE(bus.bms.busy());
    EXPECT_EQ(bus.bms.pending(), 0U);
}

TEST_F(SessionTest, APollIssuedAfterResettingFromAHandlerIsStillTransmitted) {
    // The other half of the same hazard: reset() empties the queue, the handler
    // then queues fresh work, and complete() must not pop that brand-new
    // request -- it was never sent, so popping it would discard it silently.
    bus.on_event_hook = [](Loopback& self, const Event& event) {
        if (event.id == DataId::CAPACITY_VOLTAGE) {
            self.bms.reset();
            EXPECT_EQ(self.bms.poll_fault_status(), QueueStatus::QUEUED);
        }
    };

    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);
    bus.reply(DataId::CAPACITY_VOLTAGE, {});

    ASSERT_EQ(bus.sent.size(), 2U) << "the post-reset poll must reach the wire";
    EXPECT_EQ(bus.last_sent_id(), DataId::FAULT_STATUS);
    EXPECT_EQ(bus.bms.in_flight(), DataId::FAULT_STATUS);
    EXPECT_EQ(bus.bms.pending(), 1U) << "only the new request survives the reset";
    EXPECT_TRUE(bus.bms.busy());
}

TEST_F(SessionTest, ResettingFromAFaultRecordHandlerEndsTheStreamCleanly) {
    // handle_stream() never reaches complete(), so it carries its own guard.
    // Without it a reset() here left the session idle with the handler's own
    // queued work stranded, since nothing on this path pumps.
    bus.on_event_hook = [](Loopback& self, const Event& event) {
        if (event.id == DataId::FAULT_RECORDS) {
            self.bms.reset();
            EXPECT_EQ(self.bms.poll_battery_status(), QueueStatus::QUEUED);
        }
    };

    ASSERT_EQ(bus.bms.poll_fault_records(), QueueStatus::QUEUED);
    bus.clear();

    // Four 7-byte frames deliver one 27-byte record, which fires the handler.
    for (uint8_t counter = 0; counter < 4; ++counter) {
        DataBytes frame{};
        frame[0] = counter;
        bus.feed(make_reply(DataId::FAULT_RECORDS, frame));
    }

    ASSERT_EQ(bus.sent.size(), 1U) << "the post-reset poll must reach the wire";
    EXPECT_EQ(bus.last_sent_id(), DataId::BATTERY_STATUS);
    EXPECT_EQ(bus.bms.pending(), 1U);
    EXPECT_TRUE(bus.bms.busy());
}

// -----------------------------------------------------------------------------
// Protocol errors surfaced through feed()
// -----------------------------------------------------------------------------
// ErrorCode::UNKNOWN_DATA_ID is deliberately absent here: it is not reachable
// through the public API, because every Data ID the session will accept a reply
// for has already been validated by is_known_data_id() at enqueue time and has
// a decoder. It stays defined as a defensive value, guarding the two
// decode_single()/decode_multi() failure branches, and is documented as such.
//
// ErrorCode::BAD_START_FLAG used to sit alongside it and has since been removed
// outright: nothing could produce it, and nothing could without also extending
// FrameError. A byte that is not a start flag is counted by the framer as junk
// rather than reported -- see FrameParser's own documentation for why.

TEST_F(SessionTest, ABadChecksumIsReportedAgainstTheOutstandingCommand) {
    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED);
    bus.feed(corrupt_checksum(make_reply(DataId::RTC, {})));

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(bus.events[0].error, ErrorCode::CHECKSUM);
    EXPECT_EQ(bus.events[0].id, DataId::RTC);
    EXPECT_TRUE(bus.bms.busy()) << "the transaction stays open for a retry";
}

TEST_F(SessionTest, ABadLengthFieldIsReported) {
    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED);

    MessageBytes frame = make_reply(DataId::RTC, {});
    frame[MSG_LEN_INDEX] = 0x07;
    frame[MSG_LAST_INDEX] = calc_chksum(frame);
    bus.feed(frame);

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(bus.events[0].error, ErrorCode::BAD_LENGTH);
}

TEST_F(SessionTest, AReplyEchoingTheWrongDataIdIsRejected) {
    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED);
    bus.reply(DataId::BATTERY_STATUS, {}); // not what was asked for

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(bus.events[0].error, ErrorCode::UNEXPECTED_DATA_ID);
    EXPECT_EQ(bus.events[0].id, DataId::BATTERY_STATUS) << "the event names what arrived";
    EXPECT_TRUE(bus.bms.busy());
}

TEST_F(SessionTest, AnUnsolicitedFrameIsRejectedWhileIdle) {
    // Section 5.1: the BMS only ever answers, so anything arriving while idle
    // is a late reply to a transaction that has already been abandoned.
    bus.reply(DataId::STATUS_INFO, {});

    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_EQ(bus.events[0].kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(bus.events[0].error, ErrorCode::UNEXPECTED_DATA_ID);
}

TEST_F(SessionTest, FramesMaySpanFeedCalls) {
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);

    const std::vector<uint8_t> bytes = to_bytes(make_reply(DataId::BATTERY_STATUS, {0x03, 0xE8}));
    for (uint8_t b : bytes) {
        bus.bms.feed(&b, 1);
    }

    ASSERT_EQ(bus.events.size(), 1U);
    ASSERT_NE(as_battery_status(bus.events[0]), nullptr);
    EXPECT_EQ(as_battery_status(bus.events[0])->soh_pm, 1000);
}

// -----------------------------------------------------------------------------
// Introspection
// -----------------------------------------------------------------------------

TEST_F(SessionTest, BusyAndPendingAndInFlightTrackTheTransaction) {
    EXPECT_FALSE(bus.bms.busy());
    EXPECT_EQ(bus.bms.pending(), 0U);
    EXPECT_EQ(bus.bms.in_flight(), DataId::NONE);

    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    EXPECT_TRUE(bus.bms.busy());
    EXPECT_EQ(bus.bms.pending(), 1U) << "pending() counts the request in flight";
    EXPECT_EQ(bus.bms.in_flight(), DataId::STATUS_INFO);

    ASSERT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED);
    EXPECT_EQ(bus.bms.pending(), 2U);
    EXPECT_EQ(bus.bms.in_flight(), DataId::STATUS_INFO) << "still the head request";

    bus.reply(DataId::STATUS_INFO, {});
    EXPECT_EQ(bus.bms.in_flight(), DataId::RTC);

    bus.reply(DataId::RTC, {});
    EXPECT_FALSE(bus.bms.busy());
    EXPECT_EQ(bus.bms.in_flight(), DataId::NONE);
}

TEST_F(SessionTest, DiscardedBytesTracksTheFramersJunkCount) {
    EXPECT_EQ(bus.bms.discarded_bytes(), 0U);

    const std::vector<uint8_t> junk{0x00, 0x11, 0x22};
    bus.feed_bytes(junk);

    EXPECT_EQ(bus.bms.discarded_bytes(), 3U);
    EXPECT_TRUE(bus.events.empty()) << "junk is counted, not reported as an event";
}

// -----------------------------------------------------------------------------
// Cached cell and sensor counts
// -----------------------------------------------------------------------------

TEST_F(SessionTest, CachedCountsStartAtZero) {
    EXPECT_EQ(bus.bms.cached_cell_count(), 0);
    EXPECT_EQ(bus.bms.cached_temp_count(), 0);
}

TEST_F(SessionTest, StatusInfoPopulatesTheCachedCounts) {
    // 0x94 Byte0 is the series count and Byte1 the sensor count.
    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    bus.reply(DataId::STATUS_INFO, {16, 4, 1, 1, 0x0F, 0x00, 0x2A, 65});

    EXPECT_EQ(bus.bms.cached_cell_count(), 16);
    EXPECT_EQ(bus.bms.cached_temp_count(), 4);
}

TEST_F(SessionTest, BmuCellTempCountSumsThePerBoardCounts) {
    // 0x51 reports per-board counts: cells in Byte1..3, sensors in Byte4..6.
    ASSERT_EQ(bus.bms.poll_bmu_cell_temp_count(), QueueStatus::QUEUED);
    bus.reply(DataId::BMU_CELL_TEMP_COUNT, {3, 8, 8, 8, 2, 2, 2, 0});

    EXPECT_EQ(bus.bms.cached_cell_count(), 24);
    EXPECT_EQ(bus.bms.cached_temp_count(), 6);
}

TEST_F(SessionTest, TheMostRecentReplyWins) {
    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    bus.reply(DataId::STATUS_INFO, {16, 4});
    EXPECT_EQ(bus.bms.cached_cell_count(), 16);

    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    bus.reply(DataId::STATUS_INFO, {8, 2});
    EXPECT_EQ(bus.bms.cached_cell_count(), 8);
    EXPECT_EQ(bus.bms.cached_temp_count(), 2);
}

// -----------------------------------------------------------------------------
// reset()
// -----------------------------------------------------------------------------

TEST_F(SessionTest, ResetAbandonsTheTransactionAndClearsTheQueueSilently) {
    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);
    ASSERT_TRUE(bus.bms.busy());

    bus.bms.reset();

    EXPECT_FALSE(bus.bms.busy());
    EXPECT_EQ(bus.bms.pending(), 0U);
    EXPECT_EQ(bus.bms.in_flight(), DataId::NONE);
    EXPECT_TRUE(bus.events.empty()) << "a caller waiting on a reply simply never hears about it";
}

TEST_F(SessionTest, ResetDropsAPartiallyReceivedFrame) {
    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);

    const std::vector<uint8_t> bytes = to_bytes(make_reply(DataId::BATTERY_STATUS, {0x03, 0xE8}));
    bus.feed_bytes({bytes.begin(), bytes.begin() + 6});
    bus.bms.reset();
    bus.feed_bytes({bytes.begin() + 6, bytes.end()});

    EXPECT_TRUE(bus.events.empty()) << "the tail of a dropped frame must not complete";
}

TEST_F(SessionTest, ResetZeroesTheDiscardedByteCount) {
    bus.feed_bytes({0x00, 0x11});
    ASSERT_EQ(bus.bms.discarded_bytes(), 2U);

    bus.bms.reset();

    EXPECT_EQ(bus.bms.discarded_bytes(), 0U);
}

TEST_F(SessionTest, TheSessionIsUsableAgainAfterReset) {
    ASSERT_EQ(bus.bms.poll_capacity_voltage(), QueueStatus::QUEUED);
    bus.bms.reset();
    bus.clear();

    ASSERT_EQ(bus.bms.poll_battery_status(), QueueStatus::QUEUED);
    ASSERT_EQ(bus.sent.size(), 1U);
    EXPECT_EQ(bus.last_sent_id(), DataId::BATTERY_STATUS);

    bus.reply(DataId::BATTERY_STATUS, {0x03, 0xE8});
    ASSERT_EQ(bus.events.size(), 1U);
    EXPECT_NE(as_battery_status(bus.events[0]), nullptr);
}

TEST_F(SessionTest, ResetKeepsWiringAndCachedCountsIntact) {
    ASSERT_EQ(bus.bms.poll_status_info(), QueueStatus::QUEUED);
    bus.reply(DataId::STATUS_INFO, {16, 4});
    ASSERT_EQ(bus.bms.cached_cell_count(), 16);

    bus.bms.reset();

    EXPECT_EQ(bus.bms.cached_cell_count(), 16) << "reset() abandons work, not configuration";
    EXPECT_EQ(bus.bms.cached_temp_count(), 4);
    EXPECT_EQ(bus.bms.poll_rtc(), QueueStatus::QUEUED) << "the tx handler is still registered";
}

} // namespace
} // namespace haidi_test
