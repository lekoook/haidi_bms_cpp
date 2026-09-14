// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#include <haidi_event.hpp>

namespace haidi
{

namespace
{

bool is_response(const Event& event) { return event.kind == EventKind::RESPONSE; }

bool responds_to(const Event& event, DataId id) { return is_response(event) && event.id == id; }

// The MOSFET writes are the one payload also valid on an error event: a
// rejected write still reports the state that was asked for and the state the
// device applied.
bool carries_mos_ack(const Event& event, DataId id) {
    return event.id == id && (is_response(event) || event.error == ErrorCode::WRITE_REJECTED);
}

// The level is range-checked first because an out-of-range one computes another
// command's Data ID: TEMPERATURE_DIFFERENCE at level index 3 lands on 0x93.
bool responds_to_threshold(const Event& event, AlarmClass cls, AlarmLevel level) {
    return static_cast<uint8_t>(level) < ALARM_LEVEL_COUNT &&
           responds_to(event, alarm_threshold_id(cls, level));
}

} // namespace

// -----------------------------------------------------------------------------
// Per-command payload accessors
// -----------------------------------------------------------------------------
const CapacityVoltage* as_capacity_voltage(const Event& event) {
    return responds_to(event, DataId::CAPACITY_VOLTAGE) ? &event.payload.capacity_voltage : nullptr;
}
const BmuCellTempCount* as_bmu_cell_temp_count(const Event& event) {
    return responds_to(event, DataId::BMU_CELL_TEMP_COUNT) ? &event.payload.bmu_cell_temp_count
                                                           : nullptr;
}
const TotalChargeDischargeAh* as_total_charge_discharge_ah(const Event& event) {
    return responds_to(event, DataId::TOTAL_CHARGE_DISCHARGE_AH)
               ? &event.payload.total_charge_discharge_ah
               : nullptr;
}
const BatteryOperationMode* as_battery_operation_mode(const Event& event) {
    return responds_to(event, DataId::BATTERY_OPERATION_MODE)
               ? &event.payload.battery_operation_mode
               : nullptr;
}
const FirmwareIndex* as_firmware_index(const Event& event) {
    return responds_to(event, DataId::FIRMWARE_INDEX) ? &event.payload.firmware_index : nullptr;
}
const TextPayload* as_manufacturer_name(const Event& event) {
    return responds_to(event, DataId::MANUFACTURER_NAME) ? &event.payload.text : nullptr;
}
const TextPayload* as_battery_name(const Event& event) {
    return responds_to(event, DataId::BATTERY_NAME) ? &event.payload.text : nullptr;
}
const TextPayload* as_battery_serial_number(const Event& event) {
    return responds_to(event, DataId::BATTERY_SERIAL_NUMBER) ? &event.payload.text : nullptr;
}
const Rtc* as_battery_production_date(const Event& event) {
    return responds_to(event, DataId::BATTERY_PRODUCTION_DATE) ? &event.payload.rtc : nullptr;
}
const CellVoltageAlarm* as_cell_voltage_alarm(const Event& event) {
    return responds_to(event, DataId::CELL_VOLTAGE_ALARM) ? &event.payload.cell_voltage_alarm
                                                          : nullptr;
}
const TotalVoltageAlarm* as_total_voltage_alarm(const Event& event) {
    return responds_to(event, DataId::TOTAL_VOLTAGE_ALARM) ? &event.payload.total_voltage_alarm
                                                           : nullptr;
}
const CurrentAlarm* as_current_alarm(const Event& event) {
    return responds_to(event, DataId::CURRENT_ALARM) ? &event.payload.current_alarm : nullptr;
}
const TemperatureAlarm* as_temperature_alarm(const Event& event) {
    return responds_to(event, DataId::TEMPERATURE_ALARM) ? &event.payload.temperature_alarm
                                                         : nullptr;
}
const SocAlarm* as_soc_alarm(const Event& event) {
    return responds_to(event, DataId::SOC_ALARM) ? &event.payload.soc_alarm : nullptr;
}
const DifferenceAlarm* as_difference_alarm(const Event& event) {
    return responds_to(event, DataId::DIFFERENCE_ALARM) ? &event.payload.difference_alarm : nullptr;
}
const BalancingParams* as_balancing_params(const Event& event) {
    return responds_to(event, DataId::BALANCING_PARAMS) ? &event.payload.balancing_params : nullptr;
}
const CurrentParams* as_current_params(const Event& event) {
    return responds_to(event, DataId::CURRENT_PARAMS) ? &event.payload.current_params : nullptr;
}
const Rtc* as_rtc(const Event& event) {
    return responds_to(event, DataId::RTC) ? &event.payload.rtc : nullptr;
}
const VersionPayload* as_software_version(const Event& event) {
    return responds_to(event, DataId::SOFTWARE_VERSION) ? &event.payload.version : nullptr;
}
const VersionPayload* as_hardware_version(const Event& event) {
    return responds_to(event, DataId::HARDWARE_VERSION) ? &event.payload.version : nullptr;
}
const FaultRecord* as_fault_record(const Event& event) {
    return responds_to(event, DataId::FAULT_RECORDS) ? &event.payload.fault_record : nullptr;
}
const BoardNumber* as_board_number(const Event& event) {
    return responds_to(event, DataId::BOARD_NUMBER) ? &event.payload.board_number : nullptr;
}
const HeatingTemperature* as_heating_temperature(const Event& event) {
    return responds_to(event, DataId::HEATING_TEMPERATURE) ? &event.payload.heating_temperature
                                                           : nullptr;
}
const ActiveBalancingSwitch* as_active_balancing_switch(const Event& event) {
    return responds_to(event, DataId::ACTIVE_BALANCING_SWITCH)
               ? &event.payload.active_balancing_switch
               : nullptr;
}
const ActiveBalancingParams* as_active_balancing_params(const Event& event) {
    return responds_to(event, DataId::ACTIVE_BALANCING_PARAMS)
               ? &event.payload.active_balancing_params
               : nullptr;
}
const InverterParams* as_inverter_params(const Event& event) {
    return responds_to(event, DataId::INVERTER_PARAMS) ? &event.payload.inverter_params : nullptr;
}
const TextPayload* as_sn_serial_number(const Event& event) {
    return responds_to(event, DataId::SN_SERIAL_NUMBER) ? &event.payload.text : nullptr;
}
const AlarmThreshold* as_cell_overvoltage_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::CELL_OVERVOLTAGE, level)
               ? &event.payload.alarm_threshold
               : nullptr;
}
const AlarmThreshold* as_cell_undervoltage_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::CELL_UNDERVOLTAGE, level)
               ? &event.payload.alarm_threshold
               : nullptr;
}
const CurrentAlarmThreshold* as_overcurrent_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::CURRENT, level)
               ? &event.payload.current_alarm_threshold
               : nullptr;
}
const TempAlarmThreshold* as_high_temperature_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::HIGH_TEMPERATURE, level)
               ? &event.payload.temp_alarm_threshold
               : nullptr;
}
const TempAlarmThreshold* as_low_temperature_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::LOW_TEMPERATURE, level)
               ? &event.payload.temp_alarm_threshold
               : nullptr;
}
const AlarmThreshold* as_total_overvoltage_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::TOTAL_OVERVOLTAGE, level)
               ? &event.payload.alarm_threshold
               : nullptr;
}
const AlarmThreshold* as_total_undervoltage_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::TOTAL_UNDERVOLTAGE, level)
               ? &event.payload.alarm_threshold
               : nullptr;
}
const AlarmThreshold* as_voltage_difference_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::VOLTAGE_DIFFERENCE, level)
               ? &event.payload.alarm_threshold
               : nullptr;
}
const AlarmThreshold* as_temperature_difference_threshold(const Event& event, AlarmLevel level) {
    return responds_to_threshold(event, AlarmClass::TEMPERATURE_DIFFERENCE, level)
               ? &event.payload.alarm_threshold
               : nullptr;
}
const TotalVoltageCurrentSoc* as_total_voltage_current_soc(const Event& event) {
    return responds_to(event, DataId::TOTAL_VOLTAGE_CURRENT_SOC)
               ? &event.payload.total_voltage_current_soc
               : nullptr;
}
const CellVoltageExtremes* as_cell_voltage_extremes(const Event& event) {
    return responds_to(event, DataId::CELL_VOLTAGE_EXTREMES) ? &event.payload.cell_voltage_extremes
                                                             : nullptr;
}
const CellTemperatureExtremes* as_cell_temperature_extremes(const Event& event) {
    return responds_to(event, DataId::CELL_TEMPERATURE_EXTREMES)
               ? &event.payload.cell_temperature_extremes
               : nullptr;
}
const ChargeDischargeMosStatus* as_charge_discharge_mos(const Event& event) {
    return responds_to(event, DataId::CHARGE_DISCHARGE_MOS_STATUS)
               ? &event.payload.charge_discharge_mos
               : nullptr;
}
const StatusInfo* as_status_info(const Event& event) {
    return responds_to(event, DataId::STATUS_INFO) ? &event.payload.status_info : nullptr;
}
const CellVoltages* as_cell_voltages(const Event& event) {
    return responds_to(event, DataId::CELL_VOLTAGES) ? &event.payload.cell_voltages : nullptr;
}
const CellTemperatures* as_cell_temperatures(const Event& event) {
    return responds_to(event, DataId::CELL_TEMPERATURES) ? &event.payload.cell_temperatures
                                                         : nullptr;
}
const CellBalancingBits* as_cell_balancing_bits(const Event& event) {
    return responds_to(event, DataId::CELL_BALANCING_BITS) ? &event.payload.cell_balancing_bits
                                                           : nullptr;
}
const FaultStatus* as_fault_status(const Event& event) {
    return responds_to(event, DataId::FAULT_STATUS) ? &event.payload.fault_status : nullptr;
}
const BalancingState* as_balancing_state(const Event& event) {
    return responds_to(event, DataId::BALANCING_STATE) ? &event.payload.balancing_state : nullptr;
}
const BatteryStatus* as_battery_status(const Event& event) {
    return responds_to(event, DataId::BATTERY_STATUS) ? &event.payload.battery_status : nullptr;
}
const WakeupSource* as_wakeup_source(const Event& event) {
    return responds_to(event, DataId::WAKEUP_SOURCE) ? &event.payload.wakeup_source : nullptr;
}
const MosControlAck* as_discharge_mos_control(const Event& event) {
    return carries_mos_ack(event, DataId::DISCHARGE_MOS_CONTROL) ? &event.payload.mos_control_ack
                                                                 : nullptr;
}
const MosControlAck* as_charge_mos_control(const Event& event) {
    return carries_mos_ack(event, DataId::CHARGE_MOS_CONTROL) ? &event.payload.mos_control_ack
                                                              : nullptr;
}

// -----------------------------------------------------------------------------
// Payload-shape accessors
// -----------------------------------------------------------------------------

// 0x55 Manufacturer Name, 0x56 Battery Name, 0x57 Battery Serial Number and
// 0x6A SN Serial Number all decode to the same reassembled text payload.
const TextPayload* as_text(const Event& event) {
    if (!is_response(event)) {
        return nullptr;
    }
    switch (event.id) {
        case DataId::MANUFACTURER_NAME:
        case DataId::BATTERY_NAME:
        case DataId::BATTERY_SERIAL_NUMBER:
        case DataId::SN_SERIAL_NUMBER:
            return &event.payload.text;
        default:
            return nullptr;
    }
}

const VersionPayload* as_version(const Event& event) {
    if (!is_response(event)) {
        return nullptr;
    }
    switch (event.id) {
        case DataId::SOFTWARE_VERSION:
        case DataId::HARDWARE_VERSION:
            return &event.payload.version;
        default:
            return nullptr;
    }
}

// Within the 0x70..0x8A block the payload type follows the alarm class.
const AlarmThreshold* as_alarm_threshold(const Event& event) {
    if (!is_response(event) || !is_alarm_threshold_id(event.id)) {
        return nullptr;
    }
    switch (alarm_class_of(event.id)) {
        case AlarmClass::CURRENT:
        case AlarmClass::HIGH_TEMPERATURE:
        case AlarmClass::LOW_TEMPERATURE:
            return nullptr;
        default:
            return &event.payload.alarm_threshold;
    }
}

const CurrentAlarmThreshold* as_current_alarm_threshold(const Event& event) {
    if (!is_response(event) || !is_alarm_threshold_id(event.id)) {
        return nullptr;
    }
    return alarm_class_of(event.id) == AlarmClass::CURRENT ? &event.payload.current_alarm_threshold
                                                           : nullptr;
}

const TempAlarmThreshold* as_temp_alarm_threshold(const Event& event) {
    if (!is_response(event) || !is_alarm_threshold_id(event.id)) {
        return nullptr;
    }
    switch (alarm_class_of(event.id)) {
        case AlarmClass::HIGH_TEMPERATURE:
        case AlarmClass::LOW_TEMPERATURE:
            return &event.payload.temp_alarm_threshold;
        default:
            return nullptr;
    }
}

const MosControlAck* as_mos_control_ack(const Event& event) {
    const bool carries_payload = carries_mos_ack(event, DataId::DISCHARGE_MOS_CONTROL) ||
                                 carries_mos_ack(event, DataId::CHARGE_MOS_CONTROL);
    return carries_payload ? &event.payload.mos_control_ack : nullptr;
}

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------

const char* to_string(EventKind kind) {
    switch (kind) {
        case EventKind::RESPONSE:
            return "RESPONSE";
        case EventKind::TIMEOUT:
            return "TIMEOUT";
        case EventKind::PROTOCOL_ERROR:
            return "PROTOCOL_ERROR";
    }
    return "?";
}

const char* to_string(ErrorCode error) {
    switch (error) {
        case ErrorCode::NONE:
            return "NONE";
        case ErrorCode::CHECKSUM:
            return "CHECKSUM";
        case ErrorCode::BAD_SOURCE_ADDRESS:
            return "BAD_SOURCE_ADDRESS";
        case ErrorCode::BAD_LENGTH:
            return "BAD_LENGTH";
        case ErrorCode::UNEXPECTED_DATA_ID:
            return "UNEXPECTED_DATA_ID";
        case ErrorCode::UNKNOWN_DATA_ID:
            return "UNKNOWN_DATA_ID";
        case ErrorCode::FRAME_SEQUENCE:
            return "FRAME_SEQUENCE";
        case ErrorCode::TRUNCATED:
            return "TRUNCATED";
        case ErrorCode::WRITE_REJECTED:
            return "WRITE_REJECTED";
    }
    return "?";
}
const char* to_string(DataId id) {
    if (is_alarm_threshold_id(id)) {
        return "alarm threshold";
    }
    switch (id) {
        case DataId::NONE:
            return "none";
        case DataId::CAPACITY_VOLTAGE:
            return "capacity/voltage";
        case DataId::BMU_CELL_TEMP_COUNT:
            return "bmu/cell/temp count";
        case DataId::TOTAL_CHARGE_DISCHARGE_AH:
            return "total charge/discharge Ah";
        case DataId::BATTERY_OPERATION_MODE:
            return "operation mode";
        case DataId::FIRMWARE_INDEX:
            return "firmware index";
        case DataId::MANUFACTURER_NAME:
            return "manufacturer name";
        case DataId::BATTERY_NAME:
            return "battery name";
        case DataId::BATTERY_SERIAL_NUMBER:
            return "battery serial number";
        case DataId::BATTERY_PRODUCTION_DATE:
            return "production date";
        case DataId::CELL_VOLTAGE_ALARM:
            return "cell voltage alarm";
        case DataId::TOTAL_VOLTAGE_ALARM:
            return "total voltage alarm";
        case DataId::CURRENT_ALARM:
            return "current alarm";
        case DataId::TEMPERATURE_ALARM:
            return "temperature alarm";
        case DataId::SOC_ALARM:
            return "soc alarm";
        case DataId::DIFFERENCE_ALARM:
            return "difference alarm";
        case DataId::BALANCING_PARAMS:
            return "balancing params";
        case DataId::CURRENT_PARAMS:
            return "current params";
        case DataId::RTC:
            return "rtc";
        case DataId::SOFTWARE_VERSION:
            return "software version";
        case DataId::HARDWARE_VERSION:
            return "hardware version";
        case DataId::FAULT_RECORDS:
            return "fault records";
        case DataId::BOARD_NUMBER:
            return "board number";
        case DataId::HEATING_TEMPERATURE:
            return "heating temperature";
        case DataId::ACTIVE_BALANCING_SWITCH:
            return "active balancing switch";
        case DataId::ACTIVE_BALANCING_PARAMS:
            return "active balancing params";
        case DataId::INVERTER_PARAMS:
            return "inverter params";
        case DataId::SN_SERIAL_NUMBER:
            return "sn serial number";
        case DataId::TOTAL_VOLTAGE_CURRENT_SOC:
            return "total voltage/current/soc";
        case DataId::CELL_VOLTAGE_EXTREMES:
            return "cell voltage extremes";
        case DataId::CELL_TEMPERATURE_EXTREMES:
            return "cell temperature extremes";
        case DataId::CHARGE_DISCHARGE_MOS_STATUS:
            return "charge/discharge mos status";
        case DataId::STATUS_INFO:
            return "status info";
        case DataId::CELL_VOLTAGES:
            return "cell voltages";
        case DataId::CELL_TEMPERATURES:
            return "cell temperatures";
        case DataId::CELL_BALANCING_BITS:
            return "cell balancing bits";
        case DataId::FAULT_STATUS:
            return "fault status";
        case DataId::BALANCING_STATE:
            return "balancing state";
        case DataId::BATTERY_STATUS:
            return "battery status";
        case DataId::WAKEUP_SOURCE:
            return "wakeup source";
        case DataId::DISCHARGE_MOS_CONTROL:
            return "discharge mos control";
        case DataId::CHARGE_MOS_CONTROL:
            return "charge mos control";
        default:
            return "?";
    }
}

} // namespace haidi
