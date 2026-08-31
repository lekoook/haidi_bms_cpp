// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file haidi_payloads.hpp
 * @brief Decoded payload structs, one per command family (protocol section 6).
 *
 * Every struct is a trivially-copyable POD so it can live inside the EventPayload
 * union without allocation. Values are exact fixed-point integers with the unit
 * in the field name; the codec never uses floating point.
 *
 * @note Unit suffixes used throughout:
 * | Suffix | Unit                  | Suffix | Unit                   |
 * |--------|-----------------------|--------|------------------------|
 * | @c _mv | millivolts            | @c _dv | decivolts, 0.1 V       |
 * | @c _da | deciamps, 0.1 A       | @c _mah| milliamp-hours         |
 * | @c _pm | per mille, 0.1 %      | @c _c  | whole degrees Celsius  |
 * | @c _cs | centiseconds, 0.01 s  | @c _ds | deciseconds, 0.1 s     |
 * | @c _ah | amp-hours             | @c _a  | amps                   |
 * | @c _s  | seconds               | @c _uohm | microhms             |
 *
 * @note Payload regions the document leaves undefined are preserved verbatim in
 * a @c reserved member rather than being interpreted or asserted on.
 */

#include <haidi_protocol.hpp>

#include <array>
#include <cstdint>

namespace haidi
{

/** @brief Most cells the protocol can report, via Data ID 0x95. */
static constexpr size_t MAX_CELLS = 48;
/** @brief Most temperature sensors the protocol can report, via Data ID 0x96. */
static constexpr size_t MAX_TEMPERATURES = 21;
/** @brief Longest text payload: 0x56, 0x57 and 0x6A carry 32 bytes, 0x55 carries 16. */
static constexpr size_t MAX_TEXT_LEN = 32;
/** @brief Length of a version string, used by 0x62 and 0x63. */
static constexpr size_t MAX_VERSION_LEN = 14;
/** @brief Length of the firmware index string, Data ID 0x54. */
static constexpr size_t FIRMWARE_INDEX_LEN = 8;
/** @brief Length of one Table 1 fault record, Data ID 0x64. */
static constexpr size_t FAULT_RECORD_LEN = 27;

/**
 * @brief Longest payload any paginated reply decodes to.
 *
 * Dominated by Data ID 0x95, which carries ::MAX_CELLS voltages at two bytes
 * each. This caps the decoded payload only. The session's own reassembly buffer
 * is a little larger, because a pack that numbers its frames from 1 puts every
 * frame one slot further along.
 */
static constexpr size_t MAX_REASSEMBLY_LEN = MAX_CELLS * 2;

/** @name Static configuration reads, Data IDs 0x50..0x6A */
///@{

/** @brief Design capacity and design voltage. Data ID 0x50. */
struct CapacityVoltage {
    uint32_t capacity_mah; ///< Design capacity, mAh.
    uint32_t voltage_mv;   ///< Design voltage, mV.
};

/** @brief Acquisition board, cell and sensor counts. Data ID 0x51. */
struct BmuCellTempCount {
    uint8_t board_count;               ///< Number of acquisition boards.
    std::array<uint8_t, 3> cell_count; ///< Cells monitored by each board.
    std::array<uint8_t, 3> temp_count; ///< Temperature sensors on each board.
    uint8_t reserved;                  ///< Byte7, unspecified by the document.
};

/** @brief Lifetime charge and discharge totals. Data ID 0x52. */
struct TotalChargeDischargeAh {
    uint32_t charge_ah;    ///< Cumulative charge, Ah.
    uint32_t discharge_ah; ///< Cumulative discharge, Ah.
};

/** @brief Cell chemistry reported by Data ID 0x53. */
enum class BatteryType : uint8_t {
    LFP = 0,     ///< Lithium iron phosphate.
    TERNARY = 1, ///< Ternary lithium, NMC or similar.
    LTO = 2      ///< Lithium titanate.
};

/** @brief Chemistry, power mode, build date and sleep settings. Data ID 0x53. */
struct BatteryOperationMode {
    uint8_t battery_type;          ///< A ::BatteryType; kept raw, other values are reserved.
    uint8_t operation_mode;        ///< 0x01 long-press power on/off, 0x02 short-press.
    uint16_t year;                 ///< Build year, already offset to a full year.
    uint8_t month;                 ///< Build month, 1..12.
    uint8_t day;                   ///< Build day, 1..31.
    uint16_t auto_sleep_s;         ///< Idle time before automatic sleep, seconds.
    uint8_t zero_drift_current_da; ///< Current below which readings are treated as zero, 0.1 A.
};

/** @brief Firmware index string. Data ID 0x54, one frame, no counter. */
struct FirmwareIndex {
    std::array<char, FIRMWARE_INDEX_LEN> text; ///< 8 ASCII bytes, not null-terminated.
};

/**
 * @brief A reassembled text field.
 *
 * Shared by manufacturer name (0x55), battery name (0x56), battery serial
 * number (0x57) and SN serial number (0x6A), each arriving across several
 * frames. The text is trimmed to the command's nominal length, 16 or 32 bytes.
 * A reply that lost a frame stops at the gap, leaving #len short.
 */
struct TextPayload {
    uint8_t len;                         ///< Bytes of text, which may be short.
    std::array<char, MAX_TEXT_LEN> text; ///< Content; **not** null-terminated.
};

/**
 * @brief An uninterpreted payload.
 *
 * Used for production date (0x58), whose reply layout the document never
 * specifies, so the bytes are handed back untouched.
 */
struct RawPayload {
    DataBytes data; ///< The eight payload bytes exactly as received.
};

/** @brief Cell over- and under-voltage alarm thresholds. Data ID 0x59. */
struct CellVoltageAlarm {
    uint16_t overvoltage_l1_mv;  ///< Level 1 cell overvoltage, mV.
    uint16_t overvoltage_l2_mv;  ///< Level 2 cell overvoltage, mV.
    uint16_t undervoltage_l1_mv; ///< Level 1 cell undervoltage, mV.
    uint16_t undervoltage_l2_mv; ///< Level 2 cell undervoltage, mV.
};

/**
 * @brief Pack over- and under-voltage alarm thresholds. Data ID 0x5A.
 *
 * @note The document annotates Byte6-7 as "0.01V". Every sibling field in the
 * same frame is 0.1 V, so that annotation is treated as a typo and all four
 * fields are decivolts.
 */
struct TotalVoltageAlarm {
    uint16_t overvoltage_l1_dv;  ///< Level 1 pack overvoltage, 0.1 V.
    uint16_t overvoltage_l2_dv;  ///< Level 2 pack overvoltage, 0.1 V.
    uint16_t undervoltage_l1_dv; ///< Level 1 pack undervoltage, 0.1 V.
    uint16_t undervoltage_l2_dv; ///< Level 2 pack undervoltage, 0.1 V.
};

/**
 * @brief Charge and discharge overcurrent alarms. Data ID 0x5B.
 *
 * @warning This frame lists **charge before discharge**. The alarm-threshold
 * frames 0x72, 0x7B and 0x84 use the opposite order, so the two are easy to
 * transpose. @see CurrentAlarmThreshold
 */
struct CurrentAlarm {
    int16_t charge_overcurrent_l1_da;    ///< Level 1 charge overcurrent, 0.1 A.
    int16_t charge_overcurrent_l2_da;    ///< Level 2 charge overcurrent, 0.1 A.
    int16_t discharge_overcurrent_l1_da; ///< Level 1 discharge overcurrent, 0.1 A.
    int16_t discharge_overcurrent_l2_da; ///< Level 2 discharge overcurrent, 0.1 A.
};

/** @brief Charge and discharge temperature alarm thresholds. Data ID 0x5C. */
struct TemperatureAlarm {
    int16_t charge_over_l1_c;     ///< Level 1 charge over-temperature, degrees C.
    int16_t charge_over_l2_c;     ///< Level 2 charge over-temperature, degrees C.
    int16_t charge_under_l1_c;    ///< Level 1 charge under-temperature, degrees C.
    int16_t charge_under_l2_c;    ///< Level 2 charge under-temperature, degrees C.
    int16_t discharge_over_l1_c;  ///< Level 1 discharge over-temperature, degrees C.
    int16_t discharge_over_l2_c;  ///< Level 2 discharge over-temperature, degrees C.
    int16_t discharge_under_l1_c; ///< Level 1 discharge under-temperature, degrees C.
    int16_t discharge_under_l2_c; ///< Level 2 discharge under-temperature, degrees C.
};

/** @brief State-of-charge alarm thresholds. Data ID 0x5D. */
struct SocAlarm {
    uint16_t over_l1_pm;  ///< Level 1 SOC-high threshold, 0.1 %.
    uint16_t over_l2_pm;  ///< Level 2 SOC-high threshold, 0.1 %.
    uint16_t under_l1_pm; ///< Level 1 SOC-low threshold, 0.1 %.
    uint16_t under_l2_pm; ///< Level 2 SOC-low threshold, 0.1 %.
};

/** @brief Cell voltage and temperature spread alarms. Data ID 0x5E. */
struct DifferenceAlarm {
    uint16_t voltage_diff_l1_mv;     ///< Level 1 cell voltage spread, mV.
    uint16_t voltage_diff_l2_mv;     ///< Level 2 cell voltage spread, mV.
    uint8_t temp_diff_l1_c;          ///< Level 1 temperature spread, degrees C.
    uint8_t temp_diff_l2_c;          ///< Level 2 temperature spread, degrees C.
    std::array<uint8_t, 2> reserved; ///< Byte6-7, unspecified by the document.
};

/** @brief Passive balancing parameters. Data ID 0x5F. */
struct BalancingParams {
    uint16_t start_voltage_mv;       ///< Cell voltage above which balancing may start, mV.
    uint16_t start_difference_mv;    ///< Cell spread above which balancing may start, mV.
    std::array<uint8_t, 4> reserved; ///< Byte4-7, left blank by the document.
};

/** @brief Current measurement parameters. Data ID 0x60. */
struct CurrentParams {
    uint16_t alarm_current_a;        ///< Current alarm value, A.
    uint16_t sense_resistor_uohm;    ///< Shunt resistance, microhms.
    std::array<uint8_t, 4> reserved; ///< Byte4-7, unspecified by the document.
};

/** @brief Real-time clock reading. Data ID 0x61. */
struct Rtc {
    uint16_t year;                   ///< Full year, already offset.
    uint8_t month;                   ///< Month, 1..12.
    uint8_t day;                     ///< Day, 1..31.
    uint8_t hour;                    ///< Hour, 0..23.
    uint8_t minute;                  ///< Minute, 0..59.
    uint8_t second;                  ///< Second, 0..59.
    std::array<uint8_t, 2> reserved; ///< Byte6-7, unspecified by the document.
};

/**
 * @brief Software (0x62) or hardware (0x63) version, 14 bytes over two frames.
 *
 * Trimmed and truncated exactly like TextPayload.
 */
struct VersionPayload {
    uint8_t len;                            ///< Bytes of text, which may be short.
    std::array<char, MAX_VERSION_LEN> text; ///< Content; **not** null-terminated.
};

/** @brief What a stored fault record describes. FaultRecord::record_id. */
enum class FaultRecordId : uint8_t {
    START_CHARGING = 0x01,     ///< Charging began.
    END_CHARGING = 0x02,       ///< Charging ended.
    CELL_OVERVOLTAGE = 0x03,   ///< A cell exceeded its overvoltage threshold.
    CELL_UNDERVOLTAGE = 0x04,  ///< A cell fell below its undervoltage threshold.
    TOTAL_OVERVOLTAGE = 0x05,  ///< Pack overvoltage.
    TOTAL_UNDERVOLTAGE = 0x06, ///< Pack undervoltage.
    OVERTEMPERATURE = 0x07,    ///< Over-temperature.
    UNDERTEMPERATURE = 0x08,   ///< Under-temperature.
    OVERCURRENT = 0x09,        ///< Overcurrent.
};

/**
 * @brief One stored fault record (Table 1). Data ID 0x64.
 *
 * A single request streams many of these; one Event carries one record.
 * @see HaidiBMS::poll_fault_records
 */
struct FaultRecord {
    uint16_t year;               ///< Year the record was written, already offset.
    uint8_t month;               ///< Month, 1..12.
    uint8_t day;                 ///< Day, 1..31.
    uint8_t hour;                ///< Hour, 0..23.
    uint8_t minute;              ///< Minute, 0..59.
    uint8_t second;              ///< Second, 0..59.
    uint8_t record_id;           ///< A ::FaultRecordId; other values are reserved.
    bool occurred;               ///< True when the condition arose, false when it cleared.
    uint16_t total_voltage_dv;   ///< Pack voltage at the time, 0.1 V.
    int16_t current_da;          ///< Current at the time, 0.1 A.
    uint16_t soc_pm;             ///< State of charge at the time, 0.1 %.
    bool charge_mos_on;          ///< Charge MOSFET state at the time.
    bool discharge_mos_on;       ///< Discharge MOSFET state at the time.
    uint16_t highest_cell_mv;    ///< Highest cell voltage at the time, mV.
    uint8_t highest_cell_number; ///< Which cell held that voltage.
    uint16_t lowest_cell_mv;     ///< Lowest cell voltage at the time, mV.
    uint8_t lowest_cell_number;  ///< Which cell held that voltage.
    int16_t highest_temp_c;      ///< Highest sensor reading, degrees C.
    uint8_t highest_temp_number; ///< Which sensor reported it.
    int16_t lowest_temp_c;       ///< Lowest sensor reading, degrees C.
    uint8_t lowest_temp_number;  ///< Which sensor reported it.
    uint8_t fault_code;          ///< Numeric fault code; reserved by the document.
    /**
     * @brief Whether the record's own trailing sum matched.
     *
     * Table 1 offset 0x1A carries "sum of bytes 0-17", whose byte range is
     * ambiguous, so either of two readings is accepted: bytes 0..17, or every
     * byte before the sum, 0x00..0x19. The check is advisory only: a mismatch
     * is reported here but never causes the record to be dropped.
     */
    bool checksum_ok;
};

/** @brief Board identity within a multi-board pack. Data ID 0x65. */
struct BoardNumber {
    uint8_t board_number; ///< This board's number.
    uint8_t bmu_count;    ///< Total BMUs in the pack.
};

/** @brief What the physical key is allowed to switch. HeatingTemperature::key_control_mos. */
enum class KeyControlMos : uint8_t {
    NONE = 0x55,                ///< The key controls nothing.
    DISCHARGE_AND_SLEEP = 0xA5, ///< The key controls the discharge MOSFET and sleep.
    DISCHARGE_NOT_SLEEP = 0x5A, ///< The key controls the discharge MOSFET but not sleep.
};

/**
 * @brief Heater, fan and key-control settings. Data ID 0x66.
 *
 * @note The document numbers only Byte0, Byte2 and Byte4 for this command;
 * Byte1 and Byte3 are skipped and are not interpreted.
 */
struct HeatingTemperature {
    int16_t heating_start_c; ///< Temperature below which the heater runs, degrees C.
    int16_t fan_start_c;     ///< Temperature above which the fan runs, degrees C.
    uint8_t key_control_mos; ///< A ::KeyControlMos.
};

/** @brief Active balancing on/off state. Data ID 0x67. */
struct ActiveBalancingSwitch {
    bool enabled; ///< True when active balancing is switched on.
};

/** @brief Active balancing configuration. Data ID 0x68. */
struct ActiveBalancingParams {
    uint8_t series_count;          ///< Cells in series.
    uint16_t balancing_current_da; ///< Balancing current, 0.1 A. No offset is stated.
};

/** @brief Inverter makes the BMS can pair with. InverterParams::inverter_type. */
enum class InverterType : uint8_t {
    NONE = 0,           ///< No inverter paired.
    PYLON = 1,          ///< Pylontech protocol.
    GROWAT = 2,         ///< Growatt protocol.
    SOFAR = 3,          ///< Sofar protocol.
    VOLTRONICPOWER = 4, ///< Voltronic Power protocol.
    GOODWE = 5,         ///< GoodWe protocol.
    SRNE = 6,           ///< SRNE protocol.
    MUST = 7,           ///< MUST protocol.
    VICTRONENERGY = 8,  ///< Victron Energy protocol.
};

/** @brief Bus the BMS uses to talk to the inverter. InverterParams::comm_type. */
enum class InverterCommType : uint8_t {
    RS485 = 0, ///< RS485.
    CAN = 1    ///< CAN.
};

/** @brief Paired inverter make and bus. Data ID 0x69. */
struct InverterParams {
    uint8_t inverter_type; ///< An ::InverterType.
    uint8_t comm_type;     ///< An ::InverterCommType.
};

///@}

/** @name Per-level alarm thresholds, Data IDs 0x70..0x8A */
///@{

/**
 * @brief Threshold set for most alarm classes. Data IDs 0x70..0x8A.
 *
 * Used by every AlarmClass except AlarmClass::HIGH_TEMPERATURE,
 * AlarmClass::LOW_TEMPERATURE and AlarmClass::CURRENT, which have their own
 * layouts. The unit of #value and #recovery_value follows the class:
 *
 * | Alarm class                                     | Unit        |
 * |-------------------------------------------------|-------------|
 * | CELL_OVERVOLTAGE, CELL_UNDERVOLTAGE, VOLTAGE_DIFFERENCE | mV  |
 * | TOTAL_OVERVOLTAGE, TOTAL_UNDERVOLTAGE           | 0.1 V       |
 * | TEMPERATURE_DIFFERENCE                          | degrees C   |
 *
 * @note The document annotates the recovery value of 0x77, 0x78, 0x80, 0x81,
 * 0x89 and 0x8A as "(0.1V)" even where the alarm value is mV or degrees. That
 * is copy-paste noise; the recovery value always shares the unit of the alarm
 * value, and is decoded that way here.
 *
 * @see HaidiBMS::poll_alarm_threshold
 */
struct AlarmThreshold {
    uint16_t value;             ///< Level at which the alarm asserts; unit per the class.
    uint16_t delay_cs;          ///< How long the condition must hold before asserting, 0.01 s.
    uint16_t recovery_value;    ///< Level at which the alarm clears; same unit as #value.
    uint16_t recovery_delay_cs; ///< How long recovery must hold before clearing, 0.01 s.
};

/**
 * @brief Threshold set for AlarmClass::CURRENT. Data IDs 0x72, 0x7B and 0x84.
 *
 * @warning This frame lists **discharge before charge**, the opposite order to
 * the 0x5B CurrentAlarm frame. @see CurrentAlarm
 */
struct CurrentAlarmThreshold {
    int16_t discharge_overcurrent_da; ///< Discharge overcurrent trip level, 0.1 A.
    uint16_t discharge_delay_cs;      ///< Discharge trip delay, 0.01 s.
    int16_t charge_overcurrent_da;    ///< Charge overcurrent trip level, 0.1 A.
    uint16_t charge_delay_cs;         ///< Charge trip delay, 0.01 s.
};

/**
 * @brief Threshold set for the two temperature classes.
 *
 * Covers AlarmClass::HIGH_TEMPERATURE and AlarmClass::LOW_TEMPERATURE, i.e.
 * Data IDs 0x73/0x74, 0x7C/0x7D and 0x85/0x86. Note that the delays here are
 * deciseconds, unlike the centiseconds used by AlarmThreshold.
 */
struct TempAlarmThreshold {
    int16_t charge_alarm_c;              ///< Charge-side trip temperature, degrees C.
    uint8_t charge_delay_ds;             ///< Charge-side trip delay, 0.1 s.
    int16_t charge_recovery_c;           ///< Charge-side recovery temperature, degrees C.
    uint8_t charge_recovery_delay_ds;    ///< Charge-side recovery delay, 0.1 s.
    int16_t discharge_alarm_c;           ///< Discharge-side trip temperature, degrees C.
    uint8_t discharge_delay_ds;          ///< Discharge-side trip delay, 0.1 s.
    int16_t discharge_recovery_c;        ///< Discharge-side recovery temperature, degrees C.
    uint8_t discharge_recovery_delay_ds; ///< Discharge-side recovery delay, 0.1 s.
};

///@}

/** @name Live telemetry, Data IDs 0x90..0x9A */
///@{

/** @brief Pack voltage, current and state of charge. Data ID 0x90. */
struct TotalVoltageCurrentSoc {
    uint16_t cumulative_voltage_dv; ///< Sum of the cell voltages, 0.1 V.
    uint16_t measured_voltage_dv;   ///< Directly measured pack voltage, 0.1 V.
    int16_t current_da;             ///< Current, 0.1 A; positive charge, negative discharge.
    uint16_t soc_pm;                ///< State of charge, 0.1 %.
};

/**
 * @brief Highest and lowest cell voltage with their cell numbers. Data ID 0x91.
 *
 * @note The wire layout is deliberately unaligned: Byte0-1 value, Byte2 number,
 * Byte3-4 value, Byte5 number. It is not four 2-byte fields.
 */
struct CellVoltageExtremes {
    uint16_t highest_mv;             ///< Highest cell voltage, mV.
    uint8_t highest_cell_number;     ///< Which cell holds it.
    uint16_t lowest_mv;              ///< Lowest cell voltage, mV.
    uint8_t lowest_cell_number;      ///< Which cell holds it.
    std::array<uint8_t, 2> reserved; ///< Byte6-7, unspecified by the document.
};

/** @brief Highest and lowest sensor temperature with their numbers. Data ID 0x92. */
struct CellTemperatureExtremes {
    int16_t highest_c;               ///< Highest sensor reading, degrees C.
    uint8_t highest_cell_number;     ///< Which sensor reports it.
    int16_t lowest_c;                ///< Lowest sensor reading, degrees C.
    uint8_t lowest_cell_number;      ///< Which sensor reports it.
    std::array<uint8_t, 4> reserved; ///< Byte4-7, unspecified by the document.
};

/** @brief What the pack is currently doing. ChargeDischargeMosStatus::state. */
enum class ChargeDischargeState : uint8_t {
    IDLE = 0,       ///< Neither charging nor discharging.
    CHARGING = 1,   ///< Charging.
    DISCHARGING = 2 ///< Discharging.
};

/** @brief Charge/discharge state, MOSFET states and remaining capacity. Data ID 0x93. */
struct ChargeDischargeMosStatus {
    uint8_t state;               ///< A ::ChargeDischargeState.
    uint8_t charge_mos_state;    ///< Charge MOSFET state, non-zero when conducting.
    uint8_t discharge_mos_state; ///< Discharge MOSFET state, non-zero when conducting.
    /**
     * @brief Rolling 0..255 heartbeat.
     *
     * Increments every reply. A value that stops advancing across polls means
     * the BMS has hung, which is the nearest thing the protocol offers to a
     * liveness signal.
     */
    uint8_t bms_life;
    uint32_t remaining_capacity_mah; ///< Remaining capacity, mAh.
};

/** @brief Cell and sensor counts, connection state and cycle count. Data ID 0x94. */
struct StatusInfo {
    uint8_t series_count;      ///< Cells in series. Sizes a CellVoltages read.
    uint8_t temp_sensor_count; ///< Temperature sensors fitted. Sizes a CellTemperatures read.
    bool charger_connected;    ///< True when a charger is attached.
    bool load_connected;       ///< True when a load is attached.
    uint8_t io_bits;           ///< Bit0-3 inputs DI1..DI4, Bit4-7 outputs DO1..DO4.
    uint16_t cycle_count;      ///< Charge/discharge cycles completed.
    int16_t onboard_temp_c;    ///< Board temperature, degrees C.
};

/**
 * @brief Per-cell voltages. Data ID 0x95.
 *
 * Reassembled from up to 16 frames carrying three cells each. Each frame is
 * placed by its own counter, whichever base the pack numbers from. A frame lost
 * mid-reply ends the reply at the gap, so #count comes back short rather than
 * reporting cells that never arrived as 0 mV.
 *
 * @warning The protocol never states how many cells the pack has. Pass the
 * count to HaidiBMS::poll_cell_voltages, or let it use the count cached from a
 * previous 0x94 or 0x51 reply. With neither, the transfer ends only on the
 * inter-frame timeout, which requires HaidiBMS::tick to be called, and #count
 * is a whole number of frames' worth, so padding in the last frame is reported
 * as cells.
 */
struct CellVoltages {
    uint8_t count;                      ///< Cells actually reported.
    std::array<uint16_t, MAX_CELLS> mv; ///< Cell voltages, mV; only the first #count are valid.
};

/**
 * @brief Per-sensor temperatures. Data ID 0x96.
 *
 * Reassembled from up to 3 frames carrying seven sensors each, placed and
 * truncated exactly as for CellVoltages.
 *
 * @note The document titles this command "1~16" but its own frame budget allows
 * 21. The sensor count from 0x94 or 0x51 is authoritative; ::MAX_TEMPERATURES
 * is only the buffer bound.
 *
 * @warning Sized the same way as CellVoltages, with the same dependency on
 * HaidiBMS::tick and the same whole-frame #count when no count is known.
 */
struct CellTemperatures {
    uint8_t count;                                 ///< Sensors actually reported.
    std::array<int16_t, MAX_TEMPERATURES> celsius; ///< Readings, degrees C; first #count valid.
};

/**
 * @brief Per-cell balancing state as a bit field. Data ID 0x97.
 *
 * Bit0 is cell 1 and Bit47 is cell 48; Bit48-63 are reserved. Distinct from
 * BalancingState, which the document confusingly gives the same name.
 */
struct CellBalancingBits {
    DataBytes bits; ///< Raw bit field, one bit per cell.

    /**
     * @brief Whether one cell is currently balancing.
     * @param cell_index Zero-based cell index, so cell 1 is index 0.
     * @return True when that cell's bit is set.
     * @pre @p cell_index < ::MAX_CELLS. Not checked: 64 or more reads past the field.
     */
    [[nodiscard]] bool cell_balancing(size_t cell_index) const {
        return (bits[cell_index / 8] & (1U << (cell_index % 8))) != 0;
    }
};

/**
 * @brief Active faults and alarms. Data ID 0x98.
 *
 * Bytes 0..6 are bit fields where a set bit means a fault is active. Byte 7 is
 * a numeric code, not a bit field, and is read with fault_code().
 */
struct FaultStatus {
    DataBytes bits; ///< Raw fault bits; byte 7 is the numeric code.

    /**
     * @brief Tests one fault bit.
     * @param byte_index Byte 0..6 of the field.
     * @param bit_index  Bit 0..7 within that byte.
     * @return True when that fault is active.
     * @pre @p byte_index < 7 and @p bit_index < 8. Not checked.
     */
    [[nodiscard]] bool fault(size_t byte_index, size_t bit_index) const {
        return (bits[byte_index] & (1U << bit_index)) != 0;
    }
    /**
     * @brief The numeric fault code from byte 7.
     * @return The code; zero means none.
     */
    [[nodiscard]] uint8_t fault_code() const { return bits[7]; }
    /**
     * @brief Whether any fault bit is set.
     * @return True when any bit in bytes 0..6 is set. Ignores the numeric code.
     */
    [[nodiscard]] bool any_fault() const {
        return bits[0] != 0 || bits[1] != 0 || bits[2] != 0 || bits[3] != 0 || bits[4] != 0 ||
               bits[5] != 0 || bits[6] != 0;
    }
};

/**
 * @brief Balancing state in scalar form. Data ID 0x99.
 *
 * Distinct from CellBalancingBits despite sharing a name in the document.
 */
struct BalancingState {
    bool enabled;       ///< True when balancing is active.
    int16_t current_da; ///< Balancing current, 0.1 A.
    uint8_t position;   ///< Which cell position is being balanced.
};

/** @brief State of health. Data ID 0x9A. */
struct BatteryStatus {
    uint16_t soh_pm; ///< State of health, 0.1 %.
};

///@}

/** @name Control commands, Data IDs 0xD8..0xDA */
///@{

/** @brief Which signal last woke the BMS. Data ID 0xD8. */
struct WakeupSource {
    bool key_signal;               ///< Woken by the key signal.
    bool button_signal;            ///< Woken by the button signal.
    bool rs485_signal;             ///< Woken by RS485 traffic.
    bool can_signal;               ///< Woken by CAN traffic.
    bool charge_discharge_current; ///< Woken by charge or discharge current.
};

/**
 * @brief Result of a MOSFET control write. Data IDs 0xD9 and 0xDA.
 *
 * The protocol defines no ACK or NAK: the reply simply echoes the state that
 * was applied. A #reported_on differing from #requested_on is the only way the
 * device can refuse a write, and it surfaces as ErrorCode::WRITE_REJECTED.
 *
 * @see HaidiBMS::set_charge_mos, HaidiBMS::set_discharge_mos
 */
struct MosControlAck {
    bool requested_on; ///< The state that was asked for.
    bool reported_on;  ///< The state the device says it applied.
};

///@}

} // namespace haidi
