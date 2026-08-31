// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Tests for include/haidi_protocol.hpp: frame constants, the stateless codec,
// the alarm-threshold Data ID grid and the three universal unit conversions.

#include "haidi_test_util.hpp"

#include <haidi_protocol.hpp>

#include <gtest/gtest.h>

namespace haidi_test
{
namespace
{

// -----------------------------------------------------------------------------
// The protocol document's own worked examples (section 5.3)
// -----------------------------------------------------------------------------
// These two are the anchor cases for the whole suite: if the codec disagrees
// with the bytes printed in the specification, nothing else is worth checking.

TEST(SpecExamples, SendFrameMatchesDocument) {
    // "Example Send: A5 40 90 08 00 00 00 00 00 00 00 00 7D"
    const MessageBytes expected{0xA5, 0x40, 0x90, 0x08, 0x00, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x7D};
    EXPECT_EQ(encode(HostAddress::COMP, DataId::TOTAL_VOLTAGE_CURRENT_SOC), expected);
}

TEST(SpecExamples, ReturnFrameChecksumMatchesDocument) {
    // "Example Return: A5 01 90 08 02 FC 00 00 75 A1 00 00 52"
    const MessageBytes response{0xA5, 0x01, 0x90, 0x08, 0x02, 0xFC, 0x00,
                                0x00, 0x75, 0xA1, 0x00, 0x00, 0x52};
    EXPECT_EQ(calc_chksum(response), 0x52);

    // 0x02FC = 764 -> 76.4 V, which is what pins the byte order as big-endian.
    EXPECT_EQ(be16(data_of(response), 0), 764);
    // 0x75A1 = 30113, less the 30000 offset -> 11.3 A.
    EXPECT_EQ(to_current_da(be16(data_of(response), 4)), 113);
}

// -----------------------------------------------------------------------------
// Frame constants
// -----------------------------------------------------------------------------

TEST(Constants, FrameLayoutMatchesSpecification) {
    static_assert(MSG_MAX_LEN == 13, "a frame is 13 bytes in both directions");
    static_assert(MSG_LAST_INDEX == 12, "");
    static_assert(MSG_START_INDEX == 0, "");
    static_assert(MSG_ADDRESS_INDEX == 1, "");
    static_assert(MSG_DATA_ID_INDEX == 2, "");
    static_assert(MSG_LEN_INDEX == 3, "");
    static_assert(DATA_OFFSET == 4, "");
    static_assert(DATA_LEN == 8, "");
    static_assert(DATA_OFFSET + DATA_LEN == MSG_LAST_INDEX, "payload abuts the checksum");

    EXPECT_EQ(START_FLAG, 0xA5);
    EXPECT_EQ(LENGTH_FLAG, 0x08);
    EXPECT_EQ(BMS_ADDRESS, 0x01);
    EXPECT_EQ(ALARM_CLASS_COUNT, 9);
    EXPECT_EQ(ALARM_LEVEL_COUNT, 3);

    EXPECT_EQ(MessageBytes{}.size(), MSG_MAX_LEN);
    EXPECT_EQ(DataBytes{}.size(), DATA_LEN);
}

// -----------------------------------------------------------------------------
// calc_chksum
// -----------------------------------------------------------------------------

TEST(CalcChksum, SumsBytesZeroThroughElevenOnly) {
    MessageBytes frame{};
    frame.fill(0);
    frame[0] = 0x10;
    frame[11] = 0x20;
    EXPECT_EQ(calc_chksum(frame), 0x30);

    // Byte 12 is the checksum itself, so it must not feed into its own value.
    frame[MSG_LAST_INDEX] = 0xFF;
    EXPECT_EQ(calc_chksum(frame), 0x30);
}

TEST(CalcChksum, TruncatesToTheLowByte) {
    MessageBytes frame{};
    frame.fill(0xFF);                           // bytes 0..11 sum to 12 * 255 = 3060
    EXPECT_EQ(calc_chksum(frame), 3060 & 0xFF); // 0xF4
}

TEST(CalcChksum, AllZeroFrameChecksumsToZero) { EXPECT_EQ(calc_chksum(MessageBytes{}), 0); }

// -----------------------------------------------------------------------------
// encode
// -----------------------------------------------------------------------------

TEST(Encode, PlacesEveryFieldAndAValidChecksum) {
    const DataBytes payload = bytes8({0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88});
    const MessageBytes frame = encode(HostAddress::BLE, DataId::RTC, payload);

    EXPECT_EQ(frame[MSG_START_INDEX], START_FLAG);
    EXPECT_EQ(frame[MSG_ADDRESS_INDEX], static_cast<uint8_t>(HostAddress::BLE));
    EXPECT_EQ(frame[MSG_DATA_ID_INDEX], static_cast<uint8_t>(DataId::RTC));
    EXPECT_EQ(frame[MSG_LEN_INDEX], LENGTH_FLAG);
    EXPECT_EQ(data_of(frame), payload);
    EXPECT_EQ(frame[MSG_LAST_INDEX], calc_chksum(frame));
}

TEST(Encode, ByteOneCarriesTheSendersOwnAddressNotTheTargets) {
    // Section 5.2: a request's address field identifies the host, not the BMS.
    for (HostAddress host :
         {HostAddress::BLE, HostAddress::GPRS, HostAddress::COMP, HostAddress::BROADCAST}) {
        const MessageBytes frame = encode(host, DataId::STATUS_INFO);
        EXPECT_EQ(frame[MSG_ADDRESS_INDEX], static_cast<uint8_t>(host));
        EXPECT_NE(frame[MSG_ADDRESS_INDEX], BMS_ADDRESS);
    }
}

TEST(Encode, PayloadlessOverloadZeroFills) {
    const MessageBytes frame = encode(HostAddress::COMP, DataId::CAPACITY_VOLTAGE);
    EXPECT_EQ(data_of(frame), DataBytes{});
    EXPECT_EQ(frame, encode(HostAddress::COMP, DataId::CAPACITY_VOLTAGE, DataBytes{}));
}

// -----------------------------------------------------------------------------
// is_known_data_id
// -----------------------------------------------------------------------------

TEST(IsKnownDataId, AcceptsEveryDocumentedRange) {
    for (unsigned raw = 0x50; raw <= 0x6A; ++raw) {
        EXPECT_TRUE(is_known_data_id(static_cast<DataId>(raw))) << "0x" << std::hex << raw;
    }
    for (unsigned raw = 0x70; raw <= 0x8A; ++raw) {
        EXPECT_TRUE(is_known_data_id(static_cast<DataId>(raw))) << "0x" << std::hex << raw;
    }
    for (unsigned raw = 0x90; raw <= 0x9A; ++raw) {
        EXPECT_TRUE(is_known_data_id(static_cast<DataId>(raw))) << "0x" << std::hex << raw;
    }
    for (unsigned raw = 0xD8; raw <= 0xDA; ++raw) {
        EXPECT_TRUE(is_known_data_id(static_cast<DataId>(raw))) << "0x" << std::hex << raw;
    }
}

TEST(IsKnownDataId, RejectsNoneAndEveryGapBetweenTheRanges) {
    // DataId::NONE is 0x00 precisely so it can never be mistaken for a command.
    EXPECT_FALSE(is_known_data_id(DataId::NONE));

    for (uint8_t raw : {0x00, 0x4F, 0x6B, 0x6F, 0x8B, 0x8F, 0x9B, 0xD7, 0xDB, 0xFF}) {
        EXPECT_FALSE(is_known_data_id(static_cast<DataId>(raw))) << "0x" << std::hex << +raw;
    }
}

// -----------------------------------------------------------------------------
// Field readers
// -----------------------------------------------------------------------------

TEST(FieldReaders, Be16ReadsBigEndian) {
    const DataBytes data = bytes8({0x12, 0x34, 0xFF, 0xFF, 0x00, 0x01, 0x80, 0x00});
    static_assert(be16(DataBytes{{0x12, 0x34}}, 0) == 0x1234, "be16 is usable at compile time");

    EXPECT_EQ(be16(data, 0), 0x1234);
    EXPECT_EQ(be16(data, 2), 0xFFFF);
    EXPECT_EQ(be16(data, 4), 0x0001);
    EXPECT_EQ(be16(data, 6), 0x8000);
}

TEST(FieldReaders, Be32ReadsBigEndian) {
    const DataBytes data = bytes8({0x12, 0x34, 0x56, 0x78, 0xFF, 0xFF, 0xFF, 0xFF});
    EXPECT_EQ(be32(data, 0), 0x12345678U);
    EXPECT_EQ(be32(data, 4), 0xFFFFFFFFU);
}

TEST(FieldReaders, DataOfExtractsBytesFourThroughEleven) {
    const MessageBytes frame{0xA5, 0x01, 0x90, 0x08, 0x10, 0x11, 0x12,
                             0x13, 0x14, 0x15, 0x16, 0x17, 0x00};
    EXPECT_EQ(data_of(frame), bytes8({0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17}));
}

// -----------------------------------------------------------------------------
// The 0x70..0x8A alarm-threshold grid
// -----------------------------------------------------------------------------

TEST(AlarmThresholdId, LaysOutTheFullGridInProtocolOrder) {
    static_assert(alarm_threshold_id(AlarmClass::CELL_OVERVOLTAGE, AlarmLevel::LEVEL_1) ==
                      DataId::ALARM_THRESHOLD_FIRST,
                  "the grid starts at 0x70");

    // Level 1 occupies 0x70..0x78, level 2 0x79..0x81, level 3 0x82..0x8A.
    EXPECT_EQ(alarm_threshold_id(AlarmClass::CELL_OVERVOLTAGE, AlarmLevel::LEVEL_1),
              static_cast<DataId>(0x70));
    EXPECT_EQ(alarm_threshold_id(AlarmClass::TEMPERATURE_DIFFERENCE, AlarmLevel::LEVEL_1),
              static_cast<DataId>(0x78));
    EXPECT_EQ(alarm_threshold_id(AlarmClass::CELL_OVERVOLTAGE, AlarmLevel::LEVEL_2),
              static_cast<DataId>(0x79));
    EXPECT_EQ(alarm_threshold_id(AlarmClass::TEMPERATURE_DIFFERENCE, AlarmLevel::LEVEL_2),
              static_cast<DataId>(0x81));
    EXPECT_EQ(alarm_threshold_id(AlarmClass::CELL_OVERVOLTAGE, AlarmLevel::LEVEL_3),
              static_cast<DataId>(0x82));
    EXPECT_EQ(alarm_threshold_id(AlarmClass::TEMPERATURE_DIFFERENCE, AlarmLevel::LEVEL_3),
              DataId::ALARM_THRESHOLD_LAST);

    // The documented current and temperature classes, which have their own
    // payload layouts, land where the document says they do.
    EXPECT_EQ(alarm_threshold_id(AlarmClass::CURRENT, AlarmLevel::LEVEL_1),
              static_cast<DataId>(0x72));
    EXPECT_EQ(alarm_threshold_id(AlarmClass::CURRENT, AlarmLevel::LEVEL_2),
              static_cast<DataId>(0x7B));
    EXPECT_EQ(alarm_threshold_id(AlarmClass::CURRENT, AlarmLevel::LEVEL_3),
              static_cast<DataId>(0x84));
}

TEST(AlarmThresholdId, EveryClassLevelPairRoundTrips) {
    // 9 classes x 3 levels must tile 0x70..0x8A exactly once each.
    std::vector<int> hits(0x8B - 0x70, 0);

    for (uint8_t lvl = 0; lvl < ALARM_LEVEL_COUNT; ++lvl) {
        for (uint8_t cls = 0; cls < ALARM_CLASS_COUNT; ++cls) {
            const auto klass = static_cast<AlarmClass>(cls);
            const auto level = static_cast<AlarmLevel>(lvl);
            const DataId id = alarm_threshold_id(klass, level);

            EXPECT_TRUE(is_alarm_threshold_id(id));
            EXPECT_EQ(alarm_class_of(id), klass);
            EXPECT_EQ(alarm_level_of(id), level);

            ++hits.at(static_cast<uint8_t>(id) - 0x70);
        }
    }

    for (size_t i = 0; i < hits.size(); ++i) {
        EXPECT_EQ(hits[i], 1) << "0x" << std::hex << (0x70 + i) << " was not covered exactly once";
    }
}

TEST(IsAlarmThresholdId, BoundariesAreExclusive) {
    EXPECT_FALSE(is_alarm_threshold_id(static_cast<DataId>(0x6F)));
    EXPECT_TRUE(is_alarm_threshold_id(DataId::ALARM_THRESHOLD_FIRST));
    EXPECT_TRUE(is_alarm_threshold_id(DataId::ALARM_THRESHOLD_LAST));
    EXPECT_FALSE(is_alarm_threshold_id(static_cast<DataId>(0x8B)));
    EXPECT_FALSE(is_alarm_threshold_id(DataId::TOTAL_VOLTAGE_CURRENT_SOC));
    EXPECT_FALSE(is_alarm_threshold_id(DataId::NONE));
}

// -----------------------------------------------------------------------------
// Unit conversions
// -----------------------------------------------------------------------------

TEST(Conversions, CurrentIsBiasedBy30000AndSigned) {
    static_assert(CURRENT_OFFSET == 30000, "");
    static_assert(to_current_da(30000) == 0, "usable at compile time");

    EXPECT_EQ(to_current_da(30000), 0);
    EXPECT_EQ(to_current_da(30113), 113);   // the document's own 11.3 A example
    EXPECT_EQ(to_current_da(29000), -1000); // discharge is negative
    EXPECT_EQ(to_current_da(0), -30000);
}

TEST(Conversions, TemperatureIsBiasedBy40) {
    static_assert(TEMPERATURE_OFFSET == 40, "");

    EXPECT_EQ(to_temperature_c(40), 0);
    EXPECT_EQ(to_temperature_c(65), 25);
    EXPECT_EQ(to_temperature_c(0), -40); // the coldest value the byte can carry
    EXPECT_EQ(to_temperature_c(255), 215);
}

TEST(Conversions, YearIsBiasedBy2000) {
    static_assert(YEAR_OFFSET == 2000, "");

    EXPECT_EQ(to_year(0), 2000);
    EXPECT_EQ(to_year(26), 2026);
    EXPECT_EQ(to_year(255), 2255);
}

} // namespace
} // namespace haidi_test
