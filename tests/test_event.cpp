// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Tests for include/haidi_event.hpp: the checked payload accessors and the
// diagnostic to_string() overloads.
//
// Event is a public aggregate with public members, so events are built here
// directly rather than driven through a session. That is what makes the
// negative paths reachable at all -- a live session will never hand you a
// CapacityVoltage event carrying an RTC id, but a caller can still ask, and the
// answer must be nullptr rather than another command's bytes.

#include "haidi_test_util.hpp"

#include <haidi_event.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace haidi_test
{
namespace
{

Event response(DataId id) {
    Event event{};
    event.kind = EventKind::RESPONSE;
    event.id = id;
    return event;
}

Event failure(EventKind kind, DataId id, ErrorCode error) {
    Event event{};
    event.kind = kind;
    event.id = id;
    event.error = error;
    return event;
}

/// Every Data ID worth probing an accessor with: the whole documented surface.
std::vector<DataId> all_data_ids() {
    std::vector<DataId> ids;
    for (unsigned raw = 0x50; raw <= 0x6A; ++raw) {
        ids.push_back(static_cast<DataId>(raw));
    }
    for (unsigned raw = 0x70; raw <= 0x8A; ++raw) {
        ids.push_back(static_cast<DataId>(raw));
    }
    for (unsigned raw = 0x90; raw <= 0x9A; ++raw) {
        ids.push_back(static_cast<DataId>(raw));
    }
    for (unsigned raw = 0xD8; raw <= 0xDA; ++raw) {
        ids.push_back(static_cast<DataId>(raw));
    }
    return ids;
}

/// One accessor under test: how to call it, and which Data IDs it must accept.
struct AccessorCase {
    const char* name;
    std::function<bool(const Event&)> non_null;
    std::function<bool(DataId)> accepts;
};

bool only(DataId id, DataId wanted) { return id == wanted; }

std::vector<AccessorCase> accessor_cases() {
    // Wraps `fn` so the case stores a uniform bool-returning callable.
    auto probe = [](auto fn) { return [fn](const Event& event) { return fn(event) != nullptr; }; };
    auto just = [](DataId wanted) { return [wanted](DataId id) { return only(id, wanted); }; };

    return {
        {"as_raw", probe(as_raw), just(DataId::BATTERY_PRODUCTION_DATE)},
        {"as_capacity_voltage", probe(as_capacity_voltage), just(DataId::CAPACITY_VOLTAGE)},
        {"as_bmu_cell_temp_count", probe(as_bmu_cell_temp_count),
         just(DataId::BMU_CELL_TEMP_COUNT)},
        {"as_total_charge_discharge_ah", probe(as_total_charge_discharge_ah),
         just(DataId::TOTAL_CHARGE_DISCHARGE_AH)},
        {"as_battery_operation_mode", probe(as_battery_operation_mode),
         just(DataId::BATTERY_OPERATION_MODE)},
        {"as_firmware_index", probe(as_firmware_index), just(DataId::FIRMWARE_INDEX)},
        {"as_cell_voltage_alarm", probe(as_cell_voltage_alarm), just(DataId::CELL_VOLTAGE_ALARM)},
        {"as_total_voltage_alarm", probe(as_total_voltage_alarm),
         just(DataId::TOTAL_VOLTAGE_ALARM)},
        {"as_current_alarm", probe(as_current_alarm), just(DataId::CURRENT_ALARM)},
        {"as_temperature_alarm", probe(as_temperature_alarm), just(DataId::TEMPERATURE_ALARM)},
        {"as_soc_alarm", probe(as_soc_alarm), just(DataId::SOC_ALARM)},
        {"as_difference_alarm", probe(as_difference_alarm), just(DataId::DIFFERENCE_ALARM)},
        {"as_balancing_params", probe(as_balancing_params), just(DataId::BALANCING_PARAMS)},
        {"as_current_params", probe(as_current_params), just(DataId::CURRENT_PARAMS)},
        {"as_rtc", probe(as_rtc), just(DataId::RTC)},
        {"as_fault_record", probe(as_fault_record), just(DataId::FAULT_RECORDS)},
        {"as_board_number", probe(as_board_number), just(DataId::BOARD_NUMBER)},
        {"as_heating_temperature", probe(as_heating_temperature),
         just(DataId::HEATING_TEMPERATURE)},
        {"as_active_balancing_switch", probe(as_active_balancing_switch),
         just(DataId::ACTIVE_BALANCING_SWITCH)},
        {"as_active_balancing_params", probe(as_active_balancing_params),
         just(DataId::ACTIVE_BALANCING_PARAMS)},
        {"as_inverter_params", probe(as_inverter_params), just(DataId::INVERTER_PARAMS)},
        {"as_total_voltage_current_soc", probe(as_total_voltage_current_soc),
         just(DataId::TOTAL_VOLTAGE_CURRENT_SOC)},
        {"as_cell_voltage_extremes", probe(as_cell_voltage_extremes),
         just(DataId::CELL_VOLTAGE_EXTREMES)},
        {"as_cell_temperature_extremes", probe(as_cell_temperature_extremes),
         just(DataId::CELL_TEMPERATURE_EXTREMES)},
        {"as_charge_discharge_mos", probe(as_charge_discharge_mos),
         just(DataId::CHARGE_DISCHARGE_MOS_STATUS)},
        {"as_status_info", probe(as_status_info), just(DataId::STATUS_INFO)},
        {"as_cell_voltages", probe(as_cell_voltages), just(DataId::CELL_VOLTAGES)},
        {"as_cell_temperatures", probe(as_cell_temperatures), just(DataId::CELL_TEMPERATURES)},
        {"as_cell_balancing_bits", probe(as_cell_balancing_bits),
         just(DataId::CELL_BALANCING_BITS)},
        {"as_fault_status", probe(as_fault_status), just(DataId::FAULT_STATUS)},
        {"as_balancing_state", probe(as_balancing_state), just(DataId::BALANCING_STATE)},
        {"as_battery_status", probe(as_battery_status), just(DataId::BATTERY_STATUS)},
        {"as_wakeup_source", probe(as_wakeup_source), just(DataId::WAKEUP_SOURCE)},

        // The accessors covering more than one Data ID.
        {"as_text", probe(as_text),
         [](DataId id) {
             return id == DataId::MANUFACTURER_NAME || id == DataId::BATTERY_NAME ||
                    id == DataId::BATTERY_SERIAL_NUMBER || id == DataId::SN_SERIAL_NUMBER;
         }},
        {"as_version", probe(as_version),
         [](DataId id) {
             return id == DataId::SOFTWARE_VERSION || id == DataId::HARDWARE_VERSION;
         }},
        {"as_mos_control_ack", probe(as_mos_control_ack),
         [](DataId id) {
             return id == DataId::DISCHARGE_MOS_CONTROL || id == DataId::CHARGE_MOS_CONTROL;
         }},

        // Within 0x70..0x8A the payload type follows the alarm class.
        {"as_alarm_threshold", probe(as_alarm_threshold),
         [](DataId id) {
             if (!is_alarm_threshold_id(id)) {
                 return false;
             }
             const AlarmClass cls = alarm_class_of(id);
             return cls != AlarmClass::CURRENT && cls != AlarmClass::HIGH_TEMPERATURE &&
                    cls != AlarmClass::LOW_TEMPERATURE;
         }},
        {"as_current_alarm_threshold", probe(as_current_alarm_threshold),
         [](DataId id) {
             return is_alarm_threshold_id(id) && alarm_class_of(id) == AlarmClass::CURRENT;
         }},
        {"as_temp_alarm_threshold", probe(as_temp_alarm_threshold),
         [](DataId id) {
             if (!is_alarm_threshold_id(id)) {
                 return false;
             }
             const AlarmClass cls = alarm_class_of(id);
             return cls == AlarmClass::HIGH_TEMPERATURE || cls == AlarmClass::LOW_TEMPERATURE;
         }},
    };
}

// -----------------------------------------------------------------------------
// The accessors, positive and negative
// -----------------------------------------------------------------------------

TEST(Accessors, EveryAccessorIsCovered) {
    // A guard against this file silently falling behind the header: if an
    // accessor is added and not listed above, the table stops matching.
    EXPECT_EQ(accessor_cases().size(), 39U)
        << "haidi_event.hpp declares 39 as_*() accessors; update accessor_cases()";
}

TEST(Accessors, EachAcceptsExactlyItsOwnDataIds) {
    for (const AccessorCase& c : accessor_cases()) {
        for (DataId id : all_data_ids()) {
            const bool expected = c.accepts(id);
            EXPECT_EQ(c.non_null(response(id)), expected)
                << c.name << " on 0x" << std::hex << +static_cast<uint8_t>(id);
        }
    }
}

TEST(Accessors, NoneAcceptATimeoutEvent) {
    for (const AccessorCase& c : accessor_cases()) {
        for (DataId id : all_data_ids()) {
            EXPECT_FALSE(c.non_null(failure(EventKind::TIMEOUT, id, ErrorCode::NONE)))
                << c.name << " accepted a TIMEOUT";
        }
    }
}

TEST(Accessors, NoneAcceptAProtocolErrorExceptTheDocumentedMosCase) {
    for (const AccessorCase& c : accessor_cases()) {
        for (DataId id : all_data_ids()) {
            const Event event = failure(EventKind::PROTOCOL_ERROR, id, ErrorCode::CHECKSUM);
            EXPECT_FALSE(c.non_null(event)) << c.name << " accepted a PROTOCOL_ERROR on 0x"
                                            << std::hex << +static_cast<uint8_t>(id);
        }
    }
}

// -----------------------------------------------------------------------------
// The alarm-threshold partition
// -----------------------------------------------------------------------------

TEST(AlarmThresholdAccessors, ExactlyOneMatchesEachIdInTheBlock) {
    // The three alarm-threshold accessors must partition 0x70..0x8A: every ID
    // decodes to exactly one payload shape, never zero and never two.
    for (unsigned raw = 0x70; raw <= 0x8A; ++raw) {
        const Event event = response(static_cast<DataId>(raw));

        const int matches = (as_alarm_threshold(event) != nullptr ? 1 : 0) +
                            (as_current_alarm_threshold(event) != nullptr ? 1 : 0) +
                            (as_temp_alarm_threshold(event) != nullptr ? 1 : 0);

        EXPECT_EQ(matches, 1) << "0x" << std::hex << raw << " matched " << std::dec << matches
                              << " accessors";
    }
}

TEST(AlarmThresholdAccessors, CurrentAndTemperatureClassesTakeTheirOwnLayouts) {
    // 0x72, 0x7B and 0x84 are the current class at each level.
    for (DataId id :
         {static_cast<DataId>(0x72), static_cast<DataId>(0x7B), static_cast<DataId>(0x84)}) {
        const Event event = response(id);
        EXPECT_NE(as_current_alarm_threshold(event), nullptr);
        EXPECT_EQ(as_alarm_threshold(event), nullptr);
        EXPECT_EQ(as_temp_alarm_threshold(event), nullptr);
    }

    // 0x73/0x74, 0x7C/0x7D and 0x85/0x86 are the two temperature classes.
    for (DataId id :
         {static_cast<DataId>(0x73), static_cast<DataId>(0x74), static_cast<DataId>(0x7C),
          static_cast<DataId>(0x7D), static_cast<DataId>(0x85), static_cast<DataId>(0x86)}) {
        const Event event = response(id);
        EXPECT_NE(as_temp_alarm_threshold(event), nullptr);
        EXPECT_EQ(as_alarm_threshold(event), nullptr);
        EXPECT_EQ(as_current_alarm_threshold(event), nullptr);
    }
}

// -----------------------------------------------------------------------------
// The documented exception: a rejected MOSFET write still carries its payload
// -----------------------------------------------------------------------------

TEST(MosControlAckAccessor, AcceptsAWriteRejectedError) {
    // The header calls this out as the one deliberate exception: a rejected
    // write is an error event that still reports what was asked for against
    // what the device did.
    for (DataId id : {DataId::DISCHARGE_MOS_CONTROL, DataId::CHARGE_MOS_CONTROL}) {
        Event event = failure(EventKind::PROTOCOL_ERROR, id, ErrorCode::WRITE_REJECTED);
        event.payload.mos_control_ack = MosControlAck{true, false};

        const MosControlAck* ack = as_mos_control_ack(event);
        ASSERT_NE(ack, nullptr);
        EXPECT_TRUE(ack->requested_on);
        EXPECT_FALSE(ack->reported_on);
    }
}

TEST(MosControlAckAccessor, RejectsOtherErrorCodesAndOtherDataIds) {
    // Only WRITE_REJECTED is excused, and only for the two control commands.
    for (ErrorCode error : {ErrorCode::CHECKSUM, ErrorCode::TRUNCATED, ErrorCode::BAD_LENGTH,
                            ErrorCode::UNEXPECTED_DATA_ID}) {
        const Event event = failure(EventKind::PROTOCOL_ERROR, DataId::CHARGE_MOS_CONTROL, error);
        EXPECT_EQ(as_mos_control_ack(event), nullptr) << "excused " << to_string(error);
    }

    const Event other = failure(EventKind::PROTOCOL_ERROR, DataId::RTC, ErrorCode::WRITE_REJECTED);
    EXPECT_EQ(as_mos_control_ack(other), nullptr);
}

// -----------------------------------------------------------------------------
// Event defaults
// -----------------------------------------------------------------------------

TEST(EventDefaults, AreASuccessfulEmptyResponse) {
    const Event event{};
    EXPECT_EQ(event.kind, EventKind::RESPONSE);
    EXPECT_EQ(event.error, ErrorCode::NONE);
    EXPECT_EQ(event.retries, 0);
    EXPECT_EQ(event.frames, 0);
}

// -----------------------------------------------------------------------------
// to_string
// -----------------------------------------------------------------------------

TEST(ToString, NamesEveryEventKind) {
    EXPECT_STREQ(to_string(EventKind::RESPONSE), "RESPONSE");
    EXPECT_STREQ(to_string(EventKind::TIMEOUT), "TIMEOUT");
    EXPECT_STREQ(to_string(EventKind::PROTOCOL_ERROR), "PROTOCOL_ERROR");
}

TEST(ToString, NamesEveryErrorCode) {
    EXPECT_STREQ(to_string(ErrorCode::NONE), "NONE");
    EXPECT_STREQ(to_string(ErrorCode::CHECKSUM), "CHECKSUM");
    EXPECT_STREQ(to_string(ErrorCode::BAD_SOURCE_ADDRESS), "BAD_SOURCE_ADDRESS");
    EXPECT_STREQ(to_string(ErrorCode::BAD_LENGTH), "BAD_LENGTH");
    EXPECT_STREQ(to_string(ErrorCode::UNEXPECTED_DATA_ID), "UNEXPECTED_DATA_ID");
    EXPECT_STREQ(to_string(ErrorCode::UNKNOWN_DATA_ID), "UNKNOWN_DATA_ID");
    EXPECT_STREQ(to_string(ErrorCode::FRAME_SEQUENCE), "FRAME_SEQUENCE");
    EXPECT_STREQ(to_string(ErrorCode::TRUNCATED), "TRUNCATED");
    EXPECT_STREQ(to_string(ErrorCode::WRITE_REJECTED), "WRITE_REJECTED");
}

TEST(ToString, NamesEveryDataIdAndNeverReturnsNull) {
    EXPECT_STREQ(to_string(DataId::NONE), "none");
    EXPECT_STREQ(to_string(DataId::CAPACITY_VOLTAGE), "capacity/voltage");
    EXPECT_STREQ(to_string(DataId::TOTAL_VOLTAGE_CURRENT_SOC), "total voltage/current/soc");
    EXPECT_STREQ(to_string(DataId::CHARGE_MOS_CONTROL), "charge mos control");

    for (DataId id : all_data_ids()) {
        const char* name = to_string(id);
        ASSERT_NE(name, nullptr) << "0x" << std::hex << +static_cast<uint8_t>(id);
        EXPECT_STRNE(name, "") << "0x" << std::hex << +static_cast<uint8_t>(id);
        EXPECT_STRNE(name, "?") << "0x" << std::hex << +static_cast<uint8_t>(id)
                                << " has no name of its own";
    }
}

TEST(ToString, TheWholeAlarmBlockSharesOneName) {
    // Documented behaviour: 27 Data IDs, one name.
    for (unsigned raw = 0x70; raw <= 0x8A; ++raw) {
        EXPECT_STREQ(to_string(static_cast<DataId>(raw)), "alarm threshold");
    }
}

TEST(ToString, UnmappedValuesStillReturnAUsableString) {
    // A logging call site must never have to null-check these.
    for (uint8_t raw : {0x01, 0x4F, 0x6B, 0x8B, 0x9B, 0xFF}) {
        const char* name = to_string(static_cast<DataId>(raw));
        ASSERT_NE(name, nullptr);
        EXPECT_STREQ(name, "?");
    }

    EXPECT_STREQ(to_string(static_cast<EventKind>(99)), "?");
    EXPECT_STREQ(to_string(static_cast<ErrorCode>(99)), "?");
}

} // namespace
} // namespace haidi_test
