// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#include "haidi_decode.hpp"

namespace haidi
{

namespace
{

uint16_t raw_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

// Rounds `count` items up to whole frames of `per_frame` items each.
uint8_t frames_for(uint8_t count, uint8_t per_frame, uint8_t max_frames) {
    if (count == 0) {
        return 0;
    }
    const auto frames = static_cast<uint8_t>((count + per_frame - 1) / per_frame);
    return frames > max_frames ? max_frames : frames;
}

// --- alarm-threshold block, 0x70..0x8A ---------------------------------------

void decode_alarm_threshold(DataId id, const DataBytes& data, EventPayload& out) {
    switch (alarm_class_of(id)) {
        case AlarmClass::CURRENT:
            // Discharge first here; the 0x5B Current Alarm frame is the other
            // way round.
            out.current_alarm_threshold = CurrentAlarmThreshold{
                to_current_da(be16(data, 0)),
                be16(data, 2),
                to_current_da(be16(data, 4)),
                be16(data, 6),
            };
            break;

        case AlarmClass::HIGH_TEMPERATURE:
        case AlarmClass::LOW_TEMPERATURE:
            out.temp_alarm_threshold = TempAlarmThreshold{
                to_temperature_c(data[0]), data[1], to_temperature_c(data[2]), data[3],
                to_temperature_c(data[4]), data[5], to_temperature_c(data[6]), data[7],
            };
            break;

        default:
            // Cell over/under voltage (mV), total over/under voltage (0.1 V),
            // voltage difference (mV) and temperature difference (degrees C)
            // all share this layout; only the unit of the value differs.
            out.alarm_threshold = AlarmThreshold{
                be16(data, 0),
                be16(data, 2),
                be16(data, 4),
                be16(data, 6),
            };
            break;
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Pagination metadata
// -----------------------------------------------------------------------------

uint8_t cell_voltage_frames(uint8_t cell_count) { return frames_for(cell_count, 3, 16); }

uint8_t cell_temperature_frames(uint8_t temp_count) { return frames_for(temp_count, 7, 3); }

MultiFrameInfo multiframe_info(DataId id) {
    MultiFrameInfo info;
    switch (id) {
        case DataId::MANUFACTURER_NAME: // 16 bytes over 3 frames
            info = MultiFrameInfo{true, false, 1, 7, 3, 16};
            break;
        case DataId::BATTERY_NAME:
        case DataId::BATTERY_SERIAL_NUMBER:
        case DataId::SN_SERIAL_NUMBER: // 32 bytes over 5 frames
            info = MultiFrameInfo{true, false, 1, 7, 5, 32};
            break;
        case DataId::SOFTWARE_VERSION:
        case DataId::HARDWARE_VERSION: // 14 bytes over 2 frames
            info = MultiFrameInfo{true, false, 0, 7, 2, 14};
            break;
        case DataId::CELL_VOLTAGES:
            // Three cells per frame; the frame count depends on how many cells
            // the pack actually has, so it is filled in by the session.
            info = MultiFrameInfo{true, false, 0, 6, 0, 0};
            break;
        case DataId::CELL_TEMPERATURES:
            // Seven sensors per frame, likewise pack-dependent.
            info = MultiFrameInfo{true, false, 0, 7, 0, 0};
            break;
        case DataId::FAULT_RECORDS:
            // Unbounded: one request yields records until the 0xFF sentinel.
            info = MultiFrameInfo{true, true, 0, 7, 0, 0};
            break;
        default:
            break;
    }
    return info;
}

// -----------------------------------------------------------------------------
// Single-frame decoders
// -----------------------------------------------------------------------------

bool decode_single(DataId id, const DataBytes& data, EventPayload& out) {
    if (is_alarm_threshold_id(id)) {
        decode_alarm_threshold(id, data, out);
        return true;
    }

    switch (id) {
        case DataId::CAPACITY_VOLTAGE: // 0x50
            out.capacity_voltage = CapacityVoltage{be32(data, 0), be32(data, 4)};
            return true;

        case DataId::BMU_CELL_TEMP_COUNT: // 0x51
            out.bmu_cell_temp_count = BmuCellTempCount{
                data[0], {data[1], data[2], data[3]}, {data[4], data[5], data[6]}, data[7]};
            return true;

        case DataId::TOTAL_CHARGE_DISCHARGE_AH: // 0x52
            out.total_charge_discharge_ah = TotalChargeDischargeAh{be32(data, 0), be32(data, 4)};
            return true;

        case DataId::BATTERY_OPERATION_MODE: // 0x53
            out.battery_operation_mode = BatteryOperationMode{
                data[0], data[1], to_year(data[2]), data[3], data[4], be16(data, 5), data[7]};
            return true;

        case DataId::FIRMWARE_INDEX: // 0x54, 8 ASCII bytes, single frame
            out.firmware_index = FirmwareIndex{};
            for (size_t i = 0; i < FIRMWARE_INDEX_LEN; ++i) {
                out.firmware_index.text[i] = static_cast<char>(data[i]);
            }
            return true;

        case DataId::CELL_VOLTAGE_ALARM: // 0x59
            out.cell_voltage_alarm =
                CellVoltageAlarm{be16(data, 0), be16(data, 2), be16(data, 4), be16(data, 6)};
            return true;

        case DataId::TOTAL_VOLTAGE_ALARM: // 0x5A
            out.total_voltage_alarm =
                TotalVoltageAlarm{be16(data, 0), be16(data, 2), be16(data, 4), be16(data, 6)};
            return true;

        case DataId::CURRENT_ALARM: // 0x5B, charge first
            out.current_alarm =
                CurrentAlarm{to_current_da(be16(data, 0)), to_current_da(be16(data, 2)),
                             to_current_da(be16(data, 4)), to_current_da(be16(data, 6))};
            return true;

        case DataId::TEMPERATURE_ALARM: // 0x5C
            out.temperature_alarm = TemperatureAlarm{
                to_temperature_c(data[0]), to_temperature_c(data[1]), to_temperature_c(data[2]),
                to_temperature_c(data[3]), to_temperature_c(data[4]), to_temperature_c(data[5]),
                to_temperature_c(data[6]), to_temperature_c(data[7])};
            return true;

        case DataId::SOC_ALARM: // 0x5D
            out.soc_alarm = SocAlarm{be16(data, 0), be16(data, 2), be16(data, 4), be16(data, 6)};
            return true;

        case DataId::DIFFERENCE_ALARM: // 0x5E
            // Byte4/Byte5 are differences, so they carry no 40 offset.
            out.difference_alarm =
                DifferenceAlarm{be16(data, 0), be16(data, 2), data[4], data[5], {data[6], data[7]}};
            return true;

        case DataId::BALANCING_PARAMS: // 0x5F
            out.balancing_params =
                BalancingParams{be16(data, 0), be16(data, 2), {data[4], data[5], data[6], data[7]}};
            return true;

        case DataId::CURRENT_PARAMS: // 0x60
            out.current_params =
                CurrentParams{be16(data, 0), be16(data, 2), {data[4], data[5], data[6], data[7]}};
            return true;

        // The document marks every byte of the 0x58 reply reserved; its layout
        // is assumed to be the same as 0x61.
        case DataId::BATTERY_PRODUCTION_DATE: // 0x58
        case DataId::RTC:                     // 0x61
            out.rtc = Rtc{to_year(data[0]), data[1], data[2],           data[3],
                          data[4],          data[5], {data[6], data[7]}};
            return true;

        case DataId::BOARD_NUMBER: // 0x65
            out.board_number = BoardNumber{data[0], data[1]};
            return true;

        case DataId::HEATING_TEMPERATURE: // 0x66, the document skips Byte1 and Byte3
            out.heating_temperature =
                HeatingTemperature{to_temperature_c(data[0]), to_temperature_c(data[2]), data[4]};
            return true;

        case DataId::ACTIVE_BALANCING_SWITCH: // 0x67
            out.active_balancing_switch = ActiveBalancingSwitch{data[0] != 0};
            return true;

        case DataId::ACTIVE_BALANCING_PARAMS: // 0x68
            out.active_balancing_params = ActiveBalancingParams{data[0], be16(data, 1)};
            return true;

        case DataId::INVERTER_PARAMS: // 0x69
            out.inverter_params = InverterParams{data[0], data[1]};
            return true;

        case DataId::TOTAL_VOLTAGE_CURRENT_SOC: // 0x90
            out.total_voltage_current_soc = TotalVoltageCurrentSoc{
                be16(data, 0), be16(data, 2), to_current_da(be16(data, 4)), be16(data, 6)};
            return true;

        case DataId::CELL_VOLTAGE_EXTREMES: // 0x91, note the unaligned layout
            out.cell_voltage_extremes = CellVoltageExtremes{
                be16(data, 0), data[2], be16(data, 3), data[5], {data[6], data[7]}};
            return true;

        case DataId::CELL_TEMPERATURE_EXTREMES: // 0x92
            out.cell_temperature_extremes =
                CellTemperatureExtremes{to_temperature_c(data[0]),
                                        data[1],
                                        to_temperature_c(data[2]),
                                        data[3],
                                        {data[4], data[5], data[6], data[7]}};
            return true;

        case DataId::CHARGE_DISCHARGE_MOS_STATUS: // 0x93
            out.charge_discharge_mos =
                ChargeDischargeMosStatus{data[0], data[1], data[2], data[3], be32(data, 4)};
            return true;

        case DataId::STATUS_INFO: // 0x94
            out.status_info = StatusInfo{data[0],
                                         data[1],
                                         data[2] != 0,
                                         data[3] != 0,
                                         data[4],
                                         be16(data, 5),
                                         to_temperature_c(data[7])};
            return true;

        case DataId::CELL_BALANCING_BITS: // 0x97
            out.cell_balancing_bits = CellBalancingBits{data};
            return true;

        case DataId::FAULT_STATUS: // 0x98
            out.fault_status = FaultStatus{data};
            return true;

        case DataId::BALANCING_STATE: // 0x99
            out.balancing_state =
                BalancingState{data[0] != 0, to_current_da(be16(data, 1)), data[3]};
            return true;

        case DataId::BATTERY_STATUS: // 0x9A
            out.battery_status = BatteryStatus{be16(data, 0)};
            return true;

        case DataId::WAKEUP_SOURCE: // 0xD8, one flag per byte
            out.wakeup_source =
                WakeupSource{data[0] != 0, data[1] != 0, data[2] != 0, data[3] != 0, data[4] != 0};
            return true;

        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// Paginated decoders
// -----------------------------------------------------------------------------

bool decode_multi(DataId id, const uint8_t* buf, size_t len, EventPayload& out) {
    if (buf == nullptr) {
        return false;
    }

    switch (id) {
        case DataId::MANUFACTURER_NAME:
        case DataId::BATTERY_NAME:
        case DataId::BATTERY_SERIAL_NUMBER:
        case DataId::SN_SERIAL_NUMBER: {
            const size_t n = len > MAX_TEXT_LEN ? MAX_TEXT_LEN : len;
            out.text = TextPayload{};
            out.text.len = static_cast<uint8_t>(n);
            for (size_t i = 0; i < n; ++i) {
                out.text.text[i] = static_cast<char>(buf[i]);
            }
            return true;
        }

        case DataId::SOFTWARE_VERSION:
        case DataId::HARDWARE_VERSION: {
            const size_t n = len > MAX_VERSION_LEN ? MAX_VERSION_LEN : len;
            out.version = VersionPayload{};
            out.version.len = static_cast<uint8_t>(n);
            for (size_t i = 0; i < n; ++i) {
                out.version.text[i] = static_cast<char>(buf[i]);
            }
            return true;
        }

        case DataId::CELL_VOLTAGES: {
            const size_t cells = (len / 2) > MAX_CELLS ? MAX_CELLS : (len / 2);
            out.cell_voltages = CellVoltages{};
            out.cell_voltages.count = static_cast<uint8_t>(cells);
            for (size_t i = 0; i < cells; ++i) {
                out.cell_voltages.mv[i] = raw_be16(buf + (i * 2));
            }
            return true;
        }

        case DataId::CELL_TEMPERATURES: {
            const size_t temps = len > MAX_TEMPERATURES ? MAX_TEMPERATURES : len;
            out.cell_temperatures = CellTemperatures{};
            out.cell_temperatures.count = static_cast<uint8_t>(temps);
            for (size_t i = 0; i < temps; ++i) {
                out.cell_temperatures.celsius[i] = to_temperature_c(buf[i]);
            }
            return true;
        }

        default:
            return false;
    }
}

bool decode_fault_record(const uint8_t* record, size_t len, FaultRecord& out) {
    if (record == nullptr || len < FAULT_RECORD_LEN) {
        return false;
    }

    out = FaultRecord{};
    out.year = to_year(record[0x00]);
    out.month = record[0x01];
    out.day = record[0x02];
    out.hour = record[0x03];
    out.minute = record[0x04];
    out.second = record[0x05];
    out.record_id = record[0x06];
    out.occurred = record[0x07] != 0;
    out.total_voltage_dv = raw_be16(record + 0x08);
    out.current_da = to_current_da(raw_be16(record + 0x0A));
    out.soc_pm = raw_be16(record + 0x0C);
    out.charge_mos_on = (record[0x0E] & 0x01U) != 0;
    out.discharge_mos_on = (record[0x0E] & 0x02U) != 0;
    out.highest_cell_mv = raw_be16(record + 0x0F);
    out.highest_cell_number = record[0x11];
    out.lowest_cell_mv = raw_be16(record + 0x12);
    out.lowest_cell_number = record[0x14];
    out.highest_temp_c = to_temperature_c(record[0x15]);
    out.highest_temp_number = record[0x16];
    out.lowest_temp_c = to_temperature_c(record[0x17]);
    out.lowest_temp_number = record[0x18];
    out.fault_code = record[0x19];

    // Table 1 calls offset 0x1A "sum of bytes 0-17" without making the range
    // clear. Accept either plausible reading -- bytes 0..17, or every byte
    // before the sum, 0x00..0x19 -- rather than discard a record over an
    // ambiguity in the document.
    uint8_t sum_all = 0;
    for (size_t i = 0; i < 0x1A; ++i) {
        sum_all = static_cast<uint8_t>(sum_all + record[i]);
    }
    uint8_t sum_dec = 0;
    for (size_t i = 0; i <= 17; ++i) {
        sum_dec = static_cast<uint8_t>(sum_dec + record[i]);
    }
    out.checksum_ok = (record[0x1A] == sum_all) || (record[0x1A] == sum_dec);

    return true;
}

} // namespace haidi
