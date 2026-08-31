// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// One test per single-frame command on HaidiBMS: call the named poll_*()/set_*()
// method, check what it put on the wire, feed a reply with known bytes, and read
// the result back through the matching as_*() accessor.
//
// Each case therefore covers four things at once -- the public method, the
// request encoding, the payload decoder and the checked accessor -- which is how
// decoder coverage is retained without reaching into the library's internals.
//
// Paginated and streaming commands live in test_multiframe.cpp.

#include "haidi_test_util.hpp"

#include <gtest/gtest.h>

namespace haidi_test
{
namespace
{

class CommandTest : public ::testing::Test {
    protected:
    Loopback bus;

    /// Asserts the last request carried an all-zero payload, as reads must.
    void expect_reserved_payload() const {
        ASSERT_FALSE(bus.sent.empty());
        EXPECT_EQ(data_of(bus.last_sent()), DataBytes{})
            << "every documented read specifies Byte0..Byte7 as reserved";
    }
};

// -----------------------------------------------------------------------------
// Static configuration reads, 0x50..0x69
// -----------------------------------------------------------------------------

TEST_F(CommandTest, CapacityVoltage_0x50) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_capacity_voltage(); }, DataId::CAPACITY_VOLTAGE,
                       {0x00, 0x01, 0x86, 0xA0, 0x00, 0x00, 0xC3, 0x50});
    expect_reserved_payload();

    const CapacityVoltage* payload = as_capacity_voltage(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->capacity_mah, 100000U); // 0x000186A0
    EXPECT_EQ(payload->voltage_mv, 50000U);    // 0x0000C350
    EXPECT_EQ(event.frames, 1);
}

TEST_F(CommandTest, BmuCellTempCount_0x51) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_bmu_cell_temp_count(); },
                                       DataId::BMU_CELL_TEMP_COUNT, {3, 8, 8, 8, 2, 2, 2, 0x77});

    const BmuCellTempCount* payload = as_bmu_cell_temp_count(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->board_count, 3);
    EXPECT_EQ(payload->cell_count, (std::array<uint8_t, 3>{8, 8, 8}));
    EXPECT_EQ(payload->temp_count, (std::array<uint8_t, 3>{2, 2, 2}));
    EXPECT_EQ(payload->reserved, 0x77) << "Byte7 is preserved verbatim, not interpreted";
}

TEST_F(CommandTest, TotalChargeDischargeAh_0x52) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_total_charge_discharge_ah(); },
                                       DataId::TOTAL_CHARGE_DISCHARGE_AH,
                                       {0x00, 0x00, 0x04, 0xD2, 0x00, 0x00, 0x03, 0x15});

    const TotalChargeDischargeAh* payload = as_total_charge_discharge_ah(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->charge_ah, 1234U);
    EXPECT_EQ(payload->discharge_ah, 789U);
}

TEST_F(CommandTest, BatteryOperationMode_0x53) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_battery_operation_mode(); },
                       DataId::BATTERY_OPERATION_MODE, {0x00, 0x01, 26, 9, 2, 0x0E, 0x10, 5});

    const BatteryOperationMode* payload = as_battery_operation_mode(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->battery_type, static_cast<uint8_t>(BatteryType::LFP));
    EXPECT_EQ(payload->operation_mode, 0x01);
    EXPECT_EQ(payload->year, 2026) << "the raw year byte carries a 2000 offset";
    EXPECT_EQ(payload->month, 9);
    EXPECT_EQ(payload->day, 2);
    EXPECT_EQ(payload->auto_sleep_s, 0x0E10); // 3600 s, from Byte5-6
    EXPECT_EQ(payload->zero_drift_current_da, 5);
}

TEST_F(CommandTest, FirmwareIndex_0x54) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_firmware_index(); }, DataId::FIRMWARE_INDEX,
                       {'H', 'A', 'I', 'D', 'I', '0', '0', '1'});

    const FirmwareIndex* payload = as_firmware_index(event);
    ASSERT_NE(payload, nullptr);
    const std::array<char, FIRMWARE_INDEX_LEN> expected{'H', 'A', 'I', 'D', 'I', '0', '0', '1'};
    EXPECT_EQ(payload->text, expected) << "8 ASCII bytes, not null-terminated";
}

TEST_F(CommandTest, BatteryProductionDate_0x58_IsHandedBackUntouched) {
    // The document never specifies this reply's layout, so the bytes are
    // returned verbatim rather than being guessed at.
    const DataBytes raw = bytes8({0x14, 0x09, 0x02, 0xDE, 0xAD, 0xBE, 0xEF, 0x01});
    const Event event = bus.round_trip([&] { return bus.bms.poll_battery_production_date(); },
                                       DataId::BATTERY_PRODUCTION_DATE, raw);

    const RawPayload* payload = as_raw(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->data, raw);
}

TEST_F(CommandTest, CellVoltageAlarm_0x59) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_cell_voltage_alarm(); },
                                       DataId::CELL_VOLTAGE_ALARM,
                                       {0x0E, 0x74, 0x0E, 0xD8, 0x0B, 0xB8, 0x0A, 0xF0});

    const CellVoltageAlarm* payload = as_cell_voltage_alarm(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->overvoltage_l1_mv, 3700);
    EXPECT_EQ(payload->overvoltage_l2_mv, 3800);
    EXPECT_EQ(payload->undervoltage_l1_mv, 3000);
    EXPECT_EQ(payload->undervoltage_l2_mv, 2800);
}

TEST_F(CommandTest, TotalVoltageAlarm_0x5A) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_total_voltage_alarm(); },
                                       DataId::TOTAL_VOLTAGE_ALARM,
                                       {0x02, 0x30, 0x02, 0x3A, 0x01, 0xC2, 0x01, 0xB8});

    const TotalVoltageAlarm* payload = as_total_voltage_alarm(event);
    ASSERT_NE(payload, nullptr);
    // All four fields are decivolts; the document's "0.01V" note on Byte6-7 is
    // treated as a typo, and this pins that decision.
    EXPECT_EQ(payload->overvoltage_l1_dv, 560);
    EXPECT_EQ(payload->overvoltage_l2_dv, 570);
    EXPECT_EQ(payload->undervoltage_l1_dv, 450);
    EXPECT_EQ(payload->undervoltage_l2_dv, 440);
}

TEST_F(CommandTest, CurrentAlarm_0x5B_ListsChargeBeforeDischarge) {
    // The opposite order to the 0x72/0x7B/0x84 threshold frames, which is
    // exactly why it is easy to transpose and worth asserting explicitly.
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_current_alarm(); }, DataId::CURRENT_ALARM,
                       {0x76, 0x5C, 0x77, 0x24, 0x71, 0x48, 0x70, 0x80});

    const CurrentAlarm* payload = as_current_alarm(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->charge_overcurrent_l1_da, 300);      // 30300 - 30000
    EXPECT_EQ(payload->charge_overcurrent_l2_da, 500);      // 30500 - 30000
    EXPECT_EQ(payload->discharge_overcurrent_l1_da, -1000); // 29000 - 30000
    EXPECT_EQ(payload->discharge_overcurrent_l2_da, -1200); // 28800 - 30000
}

TEST_F(CommandTest, TemperatureAlarm_0x5C) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_temperature_alarm(); }, DataId::TEMPERATURE_ALARM,
                       {95, 100, 35, 30, 100, 105, 20, 15});

    const TemperatureAlarm* payload = as_temperature_alarm(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->charge_over_l1_c, 55); // every byte carries the 40 offset
    EXPECT_EQ(payload->charge_over_l2_c, 60);
    EXPECT_EQ(payload->charge_under_l1_c, -5);
    EXPECT_EQ(payload->charge_under_l2_c, -10);
    EXPECT_EQ(payload->discharge_over_l1_c, 60);
    EXPECT_EQ(payload->discharge_over_l2_c, 65);
    EXPECT_EQ(payload->discharge_under_l1_c, -20);
    EXPECT_EQ(payload->discharge_under_l2_c, -25);
}

TEST_F(CommandTest, SocAlarm_0x5D) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_soc_alarm(); }, DataId::SOC_ALARM,
                                       {0x03, 0xB6, 0x03, 0xE8, 0x00, 0xC8, 0x00, 0x64});

    const SocAlarm* payload = as_soc_alarm(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->over_l1_pm, 950); // 0.1 % units, so 95.0 %
    EXPECT_EQ(payload->over_l2_pm, 1000);
    EXPECT_EQ(payload->under_l1_pm, 200);
    EXPECT_EQ(payload->under_l2_pm, 100);
}

TEST_F(CommandTest, DifferenceAlarm_0x5E) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_difference_alarm(); }, DataId::DIFFERENCE_ALARM,
                       {0x00, 0x64, 0x00, 0xC8, 10, 15, 0xAA, 0xBB});

    const DifferenceAlarm* payload = as_difference_alarm(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->voltage_diff_l1_mv, 100);
    EXPECT_EQ(payload->voltage_diff_l2_mv, 200);
    // These are differences, so they carry no 40 offset.
    EXPECT_EQ(payload->temp_diff_l1_c, 10);
    EXPECT_EQ(payload->temp_diff_l2_c, 15);
    EXPECT_EQ(payload->reserved, (std::array<uint8_t, 2>{0xAA, 0xBB}));
}

TEST_F(CommandTest, BalancingParams_0x5F) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_balancing_params(); }, DataId::BALANCING_PARAMS,
                       {0x0D, 0x48, 0x00, 0x1E, 0x01, 0x02, 0x03, 0x04});

    const BalancingParams* payload = as_balancing_params(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->start_voltage_mv, 3400);
    EXPECT_EQ(payload->start_difference_mv, 30);
    EXPECT_EQ(payload->reserved, (std::array<uint8_t, 4>{1, 2, 3, 4}));
}

TEST_F(CommandTest, CurrentParams_0x60) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_current_params(); }, DataId::CURRENT_PARAMS,
                       {0x00, 0x64, 0x00, 0xC8, 0xDE, 0xAD, 0xBE, 0xEF});

    const CurrentParams* payload = as_current_params(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->alarm_current_a, 100);
    EXPECT_EQ(payload->sense_resistor_uohm, 200);
    EXPECT_EQ(payload->reserved, (std::array<uint8_t, 4>{0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST_F(CommandTest, Rtc_0x61) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_rtc(); }, DataId::RTC,
                                       {26, 9, 2, 14, 35, 59, 0x11, 0x22});

    const Rtc* payload = as_rtc(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->year, 2026);
    EXPECT_EQ(payload->month, 9);
    EXPECT_EQ(payload->day, 2);
    EXPECT_EQ(payload->hour, 14);
    EXPECT_EQ(payload->minute, 35);
    EXPECT_EQ(payload->second, 59);
    EXPECT_EQ(payload->reserved, (std::array<uint8_t, 2>{0x11, 0x22}));
}

TEST_F(CommandTest, BoardNumber_0x65) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_board_number(); }, DataId::BOARD_NUMBER, {2, 4});

    const BoardNumber* payload = as_board_number(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->board_number, 2);
    EXPECT_EQ(payload->bmu_count, 4);
}

TEST_F(CommandTest, HeatingTemperature_0x66_SkipsBytesOneAndThree) {
    // The document numbers only Byte0, Byte2 and Byte4 for this command; the
    // filler bytes below must not leak into the result.
    const Event event = bus.round_trip(
        [&] { return bus.bms.poll_heating_temperature(); }, DataId::HEATING_TEMPERATURE,
        {35, 0xFF, 85, 0xFF, static_cast<uint8_t>(KeyControlMos::DISCHARGE_AND_SLEEP), 0, 0, 0});

    const HeatingTemperature* payload = as_heating_temperature(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->heating_start_c, -5); // 35 - 40
    EXPECT_EQ(payload->fan_start_c, 45);     // 85 - 40
    EXPECT_EQ(payload->key_control_mos, static_cast<uint8_t>(KeyControlMos::DISCHARGE_AND_SLEEP));
}

TEST_F(CommandTest, ActiveBalancingSwitch_0x67) {
    Loopback on_bus;
    const Event on = on_bus.round_trip([&] { return on_bus.bms.poll_active_balancing_switch(); },
                                       DataId::ACTIVE_BALANCING_SWITCH, {1});
    ASSERT_NE(as_active_balancing_switch(on), nullptr);
    EXPECT_TRUE(as_active_balancing_switch(on)->enabled);

    const Event off = bus.round_trip([&] { return bus.bms.poll_active_balancing_switch(); },
                                     DataId::ACTIVE_BALANCING_SWITCH, {0});
    ASSERT_NE(as_active_balancing_switch(off), nullptr);
    EXPECT_FALSE(as_active_balancing_switch(off)->enabled);
}

TEST_F(CommandTest, ActiveBalancingParams_0x68) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_active_balancing_params(); },
                                       DataId::ACTIVE_BALANCING_PARAMS, {16, 0x00, 0x0A});

    const ActiveBalancingParams* payload = as_active_balancing_params(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->series_count, 16);
    // Byte1-2, so this field is deliberately unaligned against the others.
    EXPECT_EQ(payload->balancing_current_da, 10);
}

TEST_F(CommandTest, InverterParams_0x69) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_inverter_params(); }, DataId::INVERTER_PARAMS,
                       {static_cast<uint8_t>(InverterType::VICTRONENERGY),
                        static_cast<uint8_t>(InverterCommType::CAN)});

    const InverterParams* payload = as_inverter_params(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->inverter_type, static_cast<uint8_t>(InverterType::VICTRONENERGY));
    EXPECT_EQ(payload->comm_type, static_cast<uint8_t>(InverterCommType::CAN));
}

// -----------------------------------------------------------------------------
// Alarm thresholds, 0x70..0x8A -- three payload shapes
// -----------------------------------------------------------------------------

TEST_F(CommandTest, AlarmThreshold_PlainClassUsesTheSharedLayout) {
    const DataId id = alarm_threshold_id(AlarmClass::CELL_OVERVOLTAGE, AlarmLevel::LEVEL_1);
    const Event event = bus.round_trip(
        [&] {
            return bus.bms.poll_alarm_threshold(AlarmClass::CELL_OVERVOLTAGE, AlarmLevel::LEVEL_1);
        },
        id, {0x0E, 0x74, 0x00, 0x0A, 0x0E, 0x10, 0x00, 0x14});

    const AlarmThreshold* payload = as_alarm_threshold(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->value, 3700);
    EXPECT_EQ(payload->delay_cs, 10);
    EXPECT_EQ(payload->recovery_value, 3600);
    // The recovery value shares the alarm value's unit, whatever the document's
    // stray "(0.1V)" annotations say.
    EXPECT_EQ(payload->recovery_delay_cs, 20);
}

TEST_F(CommandTest, AlarmThreshold_CurrentClassListsDischargeBeforeCharge) {
    // The reverse of the 0x5B CurrentAlarm frame.
    const DataId id = alarm_threshold_id(AlarmClass::CURRENT, AlarmLevel::LEVEL_2);
    ASSERT_EQ(id, static_cast<DataId>(0x7B));

    const Event event = bus.round_trip(
        [&] { return bus.bms.poll_alarm_threshold(AlarmClass::CURRENT, AlarmLevel::LEVEL_2); }, id,
        {0x71, 0x48, 0x00, 0x32, 0x76, 0x5C, 0x00, 0x64});

    const CurrentAlarmThreshold* payload = as_current_alarm_threshold(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->discharge_overcurrent_da, -1000); // 29000 - 30000, read first
    EXPECT_EQ(payload->discharge_delay_cs, 50);
    EXPECT_EQ(payload->charge_overcurrent_da, 300); // 30300 - 30000
    EXPECT_EQ(payload->charge_delay_cs, 100);

    EXPECT_EQ(as_alarm_threshold(event), nullptr) << "the current class has its own layout";
}

TEST_F(CommandTest, AlarmThreshold_TemperatureClassUsesDecisecondDelays) {
    const DataId id = alarm_threshold_id(AlarmClass::HIGH_TEMPERATURE, AlarmLevel::LEVEL_1);
    ASSERT_EQ(id, static_cast<DataId>(0x73));

    const Event event = bus.round_trip(
        [&] {
            return bus.bms.poll_alarm_threshold(AlarmClass::HIGH_TEMPERATURE, AlarmLevel::LEVEL_1);
        },
        id, {95, 5, 85, 10, 100, 15, 90, 20});

    const TempAlarmThreshold* payload = as_temp_alarm_threshold(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->charge_alarm_c, 55); // 95 - 40
    EXPECT_EQ(payload->charge_delay_ds, 5); // deciseconds here, not centiseconds
    EXPECT_EQ(payload->charge_recovery_c, 45);
    EXPECT_EQ(payload->charge_recovery_delay_ds, 10);
    EXPECT_EQ(payload->discharge_alarm_c, 60);
    EXPECT_EQ(payload->discharge_delay_ds, 15);
    EXPECT_EQ(payload->discharge_recovery_c, 50);
    EXPECT_EQ(payload->discharge_recovery_delay_ds, 20);
}

// -----------------------------------------------------------------------------
// Live telemetry, 0x90..0x9A
// -----------------------------------------------------------------------------

TEST_F(CommandTest, TotalVoltageCurrentSoc_0x90) {
    // The document's own worked example, this time end to end through a session.
    const Event event = bus.round_trip([&] { return bus.bms.poll_total_voltage_current_soc(); },
                                       DataId::TOTAL_VOLTAGE_CURRENT_SOC,
                                       {0x02, 0xFC, 0x00, 0x00, 0x75, 0xA1, 0x00, 0x00});

    const TotalVoltageCurrentSoc* payload = as_total_voltage_current_soc(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->cumulative_voltage_dv, 764); // 76.4 V
    EXPECT_EQ(payload->measured_voltage_dv, 0);
    EXPECT_EQ(payload->current_da, 113); // 11.3 A charge
    EXPECT_EQ(payload->soc_pm, 0);
}

TEST_F(CommandTest, TotalVoltageCurrentSoc_0x90_ReportsDischargeAsNegative) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_total_voltage_current_soc(); },
                                       DataId::TOTAL_VOLTAGE_CURRENT_SOC,
                                       {0x02, 0x30, 0x02, 0x2E, 0x71, 0x48, 0x03, 0x20});

    const TotalVoltageCurrentSoc* payload = as_total_voltage_current_soc(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->cumulative_voltage_dv, 560);
    EXPECT_EQ(payload->measured_voltage_dv, 558);
    EXPECT_EQ(payload->current_da, -1000) << "positive is charge, negative discharge";
    EXPECT_EQ(payload->soc_pm, 800); // 80.0 %
}

TEST_F(CommandTest, CellVoltageExtremes_0x91_HasADeliberatelyUnalignedLayout) {
    // Byte0-1 value, Byte2 number, Byte3-4 value, Byte5 number. It is NOT four
    // two-byte fields, which is the trap this case exists to catch.
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_cell_voltage_extremes(); },
                       DataId::CELL_VOLTAGE_EXTREMES, {0x0D, 0x48, 7, 0x0D, 0x05, 12, 0xAA, 0xBB});

    const CellVoltageExtremes* payload = as_cell_voltage_extremes(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->highest_mv, 3400);
    EXPECT_EQ(payload->highest_cell_number, 7);
    EXPECT_EQ(payload->lowest_mv, 3333); // 0x0D05, read from Byte3-4
    EXPECT_EQ(payload->lowest_cell_number, 12);
    EXPECT_EQ(payload->reserved, (std::array<uint8_t, 2>{0xAA, 0xBB}));
}

TEST_F(CommandTest, CellTemperatureExtremes_0x92) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_cell_temperature_extremes(); },
                       DataId::CELL_TEMPERATURE_EXTREMES, {70, 3, 35, 1, 0x01, 0x02, 0x03, 0x04});

    const CellTemperatureExtremes* payload = as_cell_temperature_extremes(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->highest_c, 30); // 70 - 40
    EXPECT_EQ(payload->highest_cell_number, 3);
    EXPECT_EQ(payload->lowest_c, -5); // 35 - 40
    EXPECT_EQ(payload->lowest_cell_number, 1);
    EXPECT_EQ(payload->reserved, (std::array<uint8_t, 4>{1, 2, 3, 4}));
}

TEST_F(CommandTest, ChargeDischargeMosStatus_0x93) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_charge_discharge_mos_status(); },
                                       DataId::CHARGE_DISCHARGE_MOS_STATUS,
                                       {static_cast<uint8_t>(ChargeDischargeState::DISCHARGING), 1,
                                        1, 0x2A, 0x00, 0x00, 0x9C, 0x40});

    const ChargeDischargeMosStatus* payload = as_charge_discharge_mos(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->state, static_cast<uint8_t>(ChargeDischargeState::DISCHARGING));
    EXPECT_EQ(payload->charge_mos_state, 1);
    EXPECT_EQ(payload->discharge_mos_state, 1);
    EXPECT_EQ(payload->bms_life, 0x2A) << "the rolling liveness heartbeat";
    EXPECT_EQ(payload->remaining_capacity_mah, 40000U);
}

TEST_F(CommandTest, StatusInfo_0x94) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_status_info(); },
                                       DataId::STATUS_INFO, {16, 4, 1, 0, 0x0F, 0x00, 0x2A, 65});

    const StatusInfo* payload = as_status_info(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->series_count, 16);
    EXPECT_EQ(payload->temp_sensor_count, 4);
    EXPECT_TRUE(payload->charger_connected);
    EXPECT_FALSE(payload->load_connected);
    EXPECT_EQ(payload->io_bits, 0x0F);
    EXPECT_EQ(payload->cycle_count, 42); // Byte5-6
    EXPECT_EQ(payload->onboard_temp_c, 25);
}

TEST_F(CommandTest, CellBalancingBits_0x97) {
    const DataBytes bits = bytes8({0x05, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00});
    const Event event = bus.round_trip([&] { return bus.bms.poll_cell_balancing_bits(); },
                                       DataId::CELL_BALANCING_BITS, bits);

    const CellBalancingBits* payload = as_cell_balancing_bits(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->bits, bits);
    EXPECT_TRUE(payload->cell_balancing(0));  // cell 1
    EXPECT_FALSE(payload->cell_balancing(1)); // cell 2
    EXPECT_TRUE(payload->cell_balancing(2));  // cell 3
    EXPECT_TRUE(payload->cell_balancing(47)); // cell 48
}

TEST_F(CommandTest, FaultStatus_0x98) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_fault_status(); }, DataId::FAULT_STATUS,
                       {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07});

    const FaultStatus* payload = as_fault_status(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_TRUE(payload->fault(0, 0));
    EXPECT_TRUE(payload->any_fault());
    EXPECT_EQ(payload->fault_code(), 7);
}

TEST_F(CommandTest, FaultStatus_0x98_ReportsACleanPack) {
    const Event event =
        bus.round_trip([&] { return bus.bms.poll_fault_status(); }, DataId::FAULT_STATUS, {});

    const FaultStatus* payload = as_fault_status(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_FALSE(payload->any_fault());
    EXPECT_EQ(payload->fault_code(), 0);
}

TEST_F(CommandTest, BalancingState_0x99) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_balancing_state(); },
                                       DataId::BALANCING_STATE, {1, 0x75, 0x35, 9});

    const BalancingState* payload = as_balancing_state(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_TRUE(payload->enabled);
    EXPECT_EQ(payload->current_da, 5); // 0x7535 = 30005, less the 30000 offset
    EXPECT_EQ(payload->position, 9);
}

TEST_F(CommandTest, BatteryStatus_0x9A) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_battery_status(); },
                                       DataId::BATTERY_STATUS, {0x03, 0xE0});

    const BatteryStatus* payload = as_battery_status(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(payload->soh_pm, 992); // 99.2 %
}

// -----------------------------------------------------------------------------
// Control, 0xD8..0xDA
// -----------------------------------------------------------------------------

TEST_F(CommandTest, WakeupSource_0xD8_IsOneFlagPerByte) {
    const Event event = bus.round_trip([&] { return bus.bms.poll_wakeup_source(); },
                                       DataId::WAKEUP_SOURCE, {0, 1, 0, 0, 1});

    const WakeupSource* payload = as_wakeup_source(event);
    ASSERT_NE(payload, nullptr);
    EXPECT_FALSE(payload->key_signal);
    EXPECT_TRUE(payload->button_signal);
    EXPECT_FALSE(payload->rs485_signal);
    EXPECT_FALSE(payload->can_signal);
    EXPECT_TRUE(payload->charge_discharge_current);
}

TEST_F(CommandTest, SetDischargeMos_0xD9_SendsTheRequestedStateAndAcceptsAMatchingEcho) {
    const Event event = bus.round_trip([&] { return bus.bms.set_discharge_mos(true); },
                                       DataId::DISCHARGE_MOS_CONTROL, {1});

    ASSERT_FALSE(bus.sent.empty());
    EXPECT_EQ(data_of(bus.last_sent())[0], 1) << "the request carries the desired state";

    EXPECT_EQ(event.kind, EventKind::RESPONSE);
    const MosControlAck* ack = as_mos_control_ack(event);
    ASSERT_NE(ack, nullptr);
    EXPECT_TRUE(ack->requested_on);
    EXPECT_TRUE(ack->reported_on);
}

TEST_F(CommandTest, SetChargeMos_0xDA_SendsZeroWhenTurningOff) {
    const Event event = bus.round_trip([&] { return bus.bms.set_charge_mos(false); },
                                       DataId::CHARGE_MOS_CONTROL, {0});

    EXPECT_EQ(data_of(bus.last_sent())[0], 0);
    EXPECT_EQ(event.kind, EventKind::RESPONSE);

    const MosControlAck* ack = as_mos_control_ack(event);
    ASSERT_NE(ack, nullptr);
    EXPECT_FALSE(ack->requested_on);
    EXPECT_FALSE(ack->reported_on);
}

TEST_F(CommandTest, ADifferingEchoIsAWriteRejectionThatStillCarriesBothStates) {
    // The protocol has no NAK, so an echo differing from the request is the only
    // way the device can refuse a write.
    const Event event = bus.round_trip([&] { return bus.bms.set_charge_mos(true); },
                                       DataId::CHARGE_MOS_CONTROL, {0});

    EXPECT_EQ(event.kind, EventKind::PROTOCOL_ERROR);
    EXPECT_EQ(event.error, ErrorCode::WRITE_REJECTED);

    const MosControlAck* ack = as_mos_control_ack(event);
    ASSERT_NE(ack, nullptr) << "a rejected write still reports what happened";
    EXPECT_TRUE(ack->requested_on);
    EXPECT_FALSE(ack->reported_on) << "the device did not do what was asked";

    EXPECT_FALSE(bus.bms.busy()) << "the transaction is still finished";
}

TEST_F(CommandTest, AnEchoOfAnyNonZeroValueCountsAsOn) {
    const Event event = bus.round_trip([&] { return bus.bms.set_discharge_mos(true); },
                                       DataId::DISCHARGE_MOS_CONTROL, {0xFF});

    EXPECT_EQ(event.kind, EventKind::RESPONSE);
    ASSERT_NE(as_mos_control_ack(event), nullptr);
    EXPECT_TRUE(as_mos_control_ack(event)->reported_on);
}

} // namespace
} // namespace haidi_test
