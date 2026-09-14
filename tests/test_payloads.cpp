// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Tests for include/haidi_payloads.hpp: the buffer-bound constants and the
// member functions the payload structs expose. These are pure value types, so
// no session is involved.

#include "haidi_test_util.hpp"

#include <haidi_event.hpp>
#include <haidi_payloads.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

namespace haidi_test
{
namespace
{

// -----------------------------------------------------------------------------
// Buffer bounds
// -----------------------------------------------------------------------------

TEST(PayloadConstants, MatchTheProtocolsFrameBudgets) {
    static_assert(MAX_CELLS == 48, "0x95 carries at most 48 cells");
    static_assert(MAX_TEMPERATURES == 21, "0x96's frame budget allows 21 sensors");
    static_assert(MAX_TEXT_LEN == 32, "0x56/0x57/0x6A are 32 bytes");
    static_assert(MAX_VERSION_LEN == 14, "0x62/0x63 are 14 bytes");
    static_assert(FIRMWARE_INDEX_LEN == 8, "0x54 fits one frame");
    static_assert(FAULT_RECORD_LEN == 27, "one Table 1 record");

    // 0x95 dominates the reassembly buffer: 48 cells at two bytes each.
    static_assert(MAX_REASSEMBLY_LEN == MAX_CELLS * 2, "");

    EXPECT_EQ(CellVoltages{}.mv.size(), MAX_CELLS);
    EXPECT_EQ(CellTemperatures{}.celsius.size(), MAX_TEMPERATURES);
    EXPECT_EQ(TextPayload{}.text.size(), MAX_TEXT_LEN);
    EXPECT_EQ(VersionPayload{}.text.size(), MAX_VERSION_LEN);
    EXPECT_EQ(FirmwareIndex{}.text.size(), FIRMWARE_INDEX_LEN);
}

// -----------------------------------------------------------------------------
// The union's design constraint
// -----------------------------------------------------------------------------

TEST(PayloadTraits, EveryPayloadIsAPodSoItCanLiveInTheUnion) {
    // EventPayload is a bare union targeting builds with exceptions disabled, so
    // it can only hold trivially copyable types. This is the constraint that
    // rules out std::variant, and it is worth asserting rather than assuming.
    static_assert(std::is_trivially_copyable<Event>::value, "");
    static_assert(std::is_trivially_copyable<EventPayload>::value, "");
    static_assert(std::is_default_constructible<Event>::value, "");

    static_assert(std::is_trivially_copyable<CapacityVoltage>::value, "");
    static_assert(std::is_trivially_copyable<FaultRecord>::value, "");
    static_assert(std::is_trivially_copyable<CellVoltages>::value, "");
    static_assert(std::is_trivially_copyable<CellTemperatures>::value, "");
    static_assert(std::is_trivially_copyable<TextPayload>::value, "");
    static_assert(std::is_trivially_copyable<VersionPayload>::value, "");
    static_assert(std::is_trivially_copyable<CellBalancingBits>::value, "");
    static_assert(std::is_trivially_copyable<FaultStatus>::value, "");
    static_assert(std::is_trivially_copyable<MosControlAck>::value, "");

    // The union must be at least as large as its largest member.
    static_assert(sizeof(EventPayload) >= sizeof(CellVoltages), "");
}

// -----------------------------------------------------------------------------
// TextPayload and VersionPayload string access
// -----------------------------------------------------------------------------

/// Builds a payload of type `T` holding `bytes`, with `len` set independently.
template<typename T>
T text_payload(std::string_view bytes, uint8_t len) {
    T payload{};
    payload.text.fill('#'); // garbage past #len, which no accessor may expose
    std::copy(bytes.begin(), bytes.end(), payload.text.begin());
    payload.len = len;
    return payload;
}

TEST(TextPayloadTest, StaysAnAggregateSoTheDecoderCanFillItByField) {
    static_assert(std::is_aggregate<TextPayload>::value, "");
    static_assert(std::is_aggregate<VersionPayload>::value, "");
}

TEST(TextPayloadTest, ConvertsToStdStringThroughEveryCommonSpelling) {
    const TextPayload payload = text_payload<TextPayload>("HELLO", 5);

    EXPECT_EQ(std::string(payload), "HELLO");
    EXPECT_EQ(std::string(payload.begin(), payload.end()), "HELLO");
    EXPECT_EQ(std::string(payload.data(), payload.size()), "HELLO");
    EXPECT_EQ(payload.view(), "HELLO");

    const std::string_view view = payload;
    EXPECT_EQ(view, "HELLO");

    std::string appended = "name: ";
    appended += payload;
    EXPECT_EQ(appended, "name: HELLO");

    std::string assigned;
    assigned = payload;
    EXPECT_EQ(assigned, "HELLO");
}

TEST(TextPayloadTest, IteratesOnlyTheValidBytes) {
    const TextPayload payload = text_payload<TextPayload>("HELLO", 5);

    EXPECT_EQ(std::distance(payload.begin(), payload.end()), 5);
    EXPECT_TRUE(std::none_of(payload.begin(), payload.end(), [](char c) { return c == '#'; }));

    std::string copied;
    for (const char c : payload) {
        copied.push_back(c);
    }
    EXPECT_EQ(copied, "HELLO");
}

TEST(TextPayloadTest, ZeroLengthIsEmpty) {
    const TextPayload payload = text_payload<TextPayload>("", 0);

    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(payload.size(), 0U);
    EXPECT_EQ(std::string(payload), "");
}

TEST(TextPayloadTest, SizeClampsAnOutOfRangeLenToTheArray) {
    EXPECT_EQ(text_payload<TextPayload>("", 200).size(), MAX_TEXT_LEN);
    EXPECT_EQ(text_payload<VersionPayload>("", 200).size(), MAX_VERSION_LEN);
    EXPECT_EQ(std::string(text_payload<VersionPayload>("", 200)).size(), MAX_VERSION_LEN);
}

TEST(TextPayloadTest, KeepsEmbeddedNulsBecauseNothingIsTrimmed) {
    using namespace std::string_view_literals;
    const TextPayload payload = text_payload<TextPayload>("A\0B\0"sv, 4);

    EXPECT_EQ(payload.size(), 4U);
    EXPECT_EQ(std::string(payload), std::string("A\0B\0", 4));
}

TEST(VersionPayloadTest, ConvertsToStdStringLikeTextPayload) {
    const VersionPayload payload = text_payload<VersionPayload>("V1.2.3", 6);

    EXPECT_EQ(std::string(payload), "V1.2.3");
    EXPECT_EQ(std::string(payload.begin(), payload.end()), "V1.2.3");
    EXPECT_FALSE(payload.empty());
}

// -----------------------------------------------------------------------------
// CellBalancingBits (0x97)
// -----------------------------------------------------------------------------

TEST(CellBalancingBitsTest, Bit0IsCell1AndBit47IsCell48) {
    // The header numbers cells from 1 but indexes from 0, so index 0 is cell 1.
    CellBalancingBits bits{};
    bits.bits = bytes8({0x01, 0, 0, 0, 0, 0x80, 0, 0});

    EXPECT_TRUE(bits.cell_balancing(0)) << "byte 0 bit 0 is cell 1";
    EXPECT_TRUE(bits.cell_balancing(47)) << "byte 5 bit 7 is cell 48";
    EXPECT_FALSE(bits.cell_balancing(1));
    EXPECT_FALSE(bits.cell_balancing(46));
}

TEST(CellBalancingBitsTest, MapsEveryIndexToTheRightByteAndBit) {
    for (size_t index = 0; index < MAX_CELLS; ++index) {
        CellBalancingBits bits{};
        bits.bits[index / 8] = static_cast<uint8_t>(1U << (index % 8));

        for (size_t probe = 0; probe < MAX_CELLS; ++probe) {
            EXPECT_EQ(bits.cell_balancing(probe), probe == index)
                << "index " << index << " leaked into " << probe;
        }
    }
}

TEST(CellBalancingBitsTest, AllClearAndAllSetFields) {
    CellBalancingBits none{};
    for (size_t i = 0; i < MAX_CELLS; ++i) {
        EXPECT_FALSE(none.cell_balancing(i));
    }

    CellBalancingBits all{};
    all.bits.fill(0xFF);
    for (size_t i = 0; i < MAX_CELLS; ++i) {
        EXPECT_TRUE(all.cell_balancing(i));
    }
}

// -----------------------------------------------------------------------------
// FaultStatus (0x98)
// -----------------------------------------------------------------------------

TEST(FaultStatusTest, FaultTestsOneBitOfOneByte) {
    FaultStatus status{};
    status.bits = bytes8({0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00});

    EXPECT_TRUE(status.fault(0, 0));
    EXPECT_TRUE(status.fault(1, 1));
    EXPECT_TRUE(status.fault(2, 2));
    EXPECT_TRUE(status.fault(3, 3));
    EXPECT_TRUE(status.fault(4, 4));
    EXPECT_TRUE(status.fault(5, 5));
    EXPECT_TRUE(status.fault(6, 6));

    EXPECT_FALSE(status.fault(0, 1));
    EXPECT_FALSE(status.fault(6, 7));
}

TEST(FaultStatusTest, FaultCodeIsByteSevenVerbatim) {
    FaultStatus status{};
    status.bits = bytes8({0, 0, 0, 0, 0, 0, 0, 0x2A});

    EXPECT_EQ(status.fault_code(), 0x2A);
}

TEST(FaultStatusTest, AnyFaultIgnoresTheNumericCodeInByteSeven) {
    // Byte 7 is a numeric code, not a bit field, so a non-zero code on its own
    // is not "a fault bit is set".
    FaultStatus code_only{};
    code_only.bits = bytes8({0, 0, 0, 0, 0, 0, 0, 0xFF});
    EXPECT_FALSE(code_only.any_fault());
    EXPECT_EQ(code_only.fault_code(), 0xFF);

    FaultStatus clean{};
    EXPECT_FALSE(clean.any_fault());
    EXPECT_EQ(clean.fault_code(), 0);
}

TEST(FaultStatusTest, AnyFaultSeesEachOfBytesZeroThroughSix) {
    for (size_t byte_index = 0; byte_index < 7; ++byte_index) {
        FaultStatus status{};
        status.bits[byte_index] = 0x01;
        EXPECT_TRUE(status.any_fault()) << "byte " << byte_index << " was not considered";
    }
}

} // namespace
} // namespace haidi_test
