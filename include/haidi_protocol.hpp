// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file haidi_protocol.hpp
 * @brief Frame constants, Data IDs and the stateless codec for protocol V4.0.
 *
 * Every frame is exactly 13 bytes in both directions:
 *
 * | Offset | Size | Field    | Value                                          |
 * |--------|------|----------|------------------------------------------------|
 * | 0      | 1    | START    | 0xA5, constant                                 |
 * | 1      | 1    | ADDRESS  | request: sender's host address; reply: 0x01    |
 * | 2      | 1    | DATA_ID  | command code, see DataId                       |
 * | 3      | 1    | LENGTH   | 0x08, constant                                 |
 * | 4..11  | 8    | DATA     | payload, big-endian, unused bytes 0x00         |
 * | 12     | 1    | CHECKSUM | (sum of bytes 0..11) & 0xFF                    |
 *
 * @note Byte 1 of a *request* carries the sender's own address, not the
 * target's. UART/RS485 has no slave-select field, so one UART segment addresses
 * exactly one BMS.
 *
 * @note Replies are correlated to requests solely by the echoed Data ID in
 * byte 2. The protocol has no sequence number.
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace haidi
{

/** @brief Total frame length in bytes; identical in both directions. */
static constexpr size_t MSG_MAX_LEN = 13;
/** @brief Index of the trailing checksum byte. */
static constexpr size_t MSG_LAST_INDEX = MSG_MAX_LEN - 1;

/** @brief Index of the start flag. */
static constexpr size_t MSG_START_INDEX = 0;
/** @brief Index of the sender's address. */
static constexpr size_t MSG_ADDRESS_INDEX = 1;
/** @brief Index of the Data ID, which is also the reply-correlation key. */
static constexpr size_t MSG_DATA_ID_INDEX = 2;
/** @brief Index of the length field, always LENGTH_FLAG. */
static constexpr size_t MSG_LEN_INDEX = 3;

/** @brief Offset of the first payload byte within a frame. */
static constexpr size_t DATA_OFFSET = 4;
/** @brief Payload length in bytes; fixed, so the length field carries no information. */
static constexpr size_t DATA_LEN = 8;

/** @brief Start-of-frame marker, byte 0 of every frame. */
static constexpr uint8_t START_FLAG = 0xA5;
/** @brief The only legal value of the length field. */
static constexpr uint8_t LENGTH_FLAG = 0x08;
/** @brief Address the BMS sends from. Replies carrying anything else are not ours. */
static constexpr uint8_t BMS_ADDRESS = 0x01;

/** @brief A complete 13-byte frame. */
using MessageBytes = std::array<uint8_t, MSG_MAX_LEN>;
/** @brief The 8-byte payload field, indexed as Byte0..Byte7 in the protocol document. */
using DataBytes = std::array<uint8_t, DATA_LEN>;

/**
 * @brief Address of the host end of the link (protocol section 5.2).
 *
 * These identify the *host*, not the target. The BMS's own address is not a
 * host address and lives in ::BMS_ADDRESS.
 */
enum class HostAddress : uint8_t {
    BLE = 0x80,      ///< Bluetooth mobile application.
    GPRS = 0x20,     ///< GPRS module.
    COMP = 0x40,     ///< Upper computer, i.e. a PC or embedded host.
    BROADCAST = 0xFF ///< Broadcast. No reply is defined; treat as fire-and-forget.
};

/**
 * @brief Every command the protocol document defines (section 6).
 *
 * The value is the byte placed at ::MSG_DATA_ID_INDEX, and a reply echoes it.
 */
enum class DataId : uint8_t {
    /**
     * @brief Not a wire value; means "no command".
     *
     * Reported by HaidiBMS::in_flight() when no transaction is outstanding.
     * 0x00 falls outside every range the protocol assigns, so it can never be
     * confused with a real command or accepted as one.
     */
    NONE = 0x00,

    // Static configuration reads.
    CAPACITY_VOLTAGE = 0x50, ///< Design capacity and design voltage. Data ID 0x50.
    BMU_CELL_TEMP_COUNT =
        0x51, ///< Acquisition board, cell and temperature-sensor counts. Data ID 0x51.
    TOTAL_CHARGE_DISCHARGE_AH = 0x52, ///< Lifetime charge and discharge totals, Ah. Data ID 0x52.
    BATTERY_OPERATION_MODE = 0x53,    ///< Battery chemistry, power mode, build date, sleep time,
                                      ///< zero-drift current. Data ID 0x53.
    FIRMWARE_INDEX = 0x54,            ///< Firmware index, 8 ASCII bytes in one frame. Data ID 0x54.
    MANUFACTURER_NAME = 0x55,         ///< Manufacturer name, 16 bytes over 3 frames. Data ID 0x55.
    BATTERY_NAME = 0x56,              ///< Battery name, 32 bytes over 5 frames. Data ID 0x56.
    BATTERY_SERIAL_NUMBER = 0x57, ///< Battery serial number, 32 bytes over 5 frames. Data ID 0x57.
    BATTERY_PRODUCTION_DATE =
        0x58, ///< Production date. Reply layout undefined; raw bytes returned. Data ID 0x58.
    CELL_VOLTAGE_ALARM = 0x59,  ///< Cell over- and under-voltage alarm thresholds. Data ID 0x59.
    TOTAL_VOLTAGE_ALARM = 0x5A, ///< Pack over- and under-voltage alarm thresholds. Data ID 0x5A.
    CURRENT_ALARM = 0x5B, ///< Charge and discharge overcurrent alarms. Charge first. Data ID 0x5B.
    TEMPERATURE_ALARM = 0x5C, ///< Charge and discharge over/under temperature alarms. Data ID 0x5C.
    SOC_ALARM = 0x5D,         ///< State-of-charge high and low alarm thresholds. Data ID 0x5D.
    DIFFERENCE_ALARM =
        0x5E, ///< Cell voltage and temperature spread alarm thresholds. Data ID 0x5E.
    BALANCING_PARAMS =
        0x5F,              ///< Passive balancing start voltage and start difference. Data ID 0x5F.
    CURRENT_PARAMS = 0x60, ///< Current alarm value and shunt resistance. Data ID 0x60.
    RTC = 0x61,            ///< Real-time clock reading. Data ID 0x61.
    SOFTWARE_VERSION = 0x62, ///< Software version, 14 bytes over 2 frames. Data ID 0x62.
    HARDWARE_VERSION = 0x63, ///< Hardware version, 14 bytes over 2 frames. Data ID 0x63.
    FAULT_RECORDS = 0x64, ///< Stored fault records. One request streams many replies until a 0xFF
                          ///< sentinel. Data ID 0x64.
    BOARD_NUMBER = 0x65,  ///< Board number and total BMU count. Data ID 0x65.
    HEATING_TEMPERATURE =
        0x66, ///< Heater and fan start temperatures, key MOS control mode. Data ID 0x66.
    ACTIVE_BALANCING_SWITCH = 0x67, ///< Active balancing on/off state. Data ID 0x67.
    ACTIVE_BALANCING_PARAMS =
        0x68,                ///< Series cell count and active balancing current. Data ID 0x68.
    INVERTER_PARAMS = 0x69,  ///< Paired inverter make and its communication bus. Data ID 0x69.
    SN_SERIAL_NUMBER = 0x6A, ///< SN serial number, 32 bytes over 5 frames. Data ID 0x6A.

    // Per-level alarm thresholds occupy 0x70..0x8A: three levels of nine alarm
    // classes in a regular grid. Address them through alarm_threshold_id()
    // rather than by naming 27 enumerators.
    ALARM_THRESHOLD_FIRST = 0x70, ///< First Data ID of the alarm-threshold block.
    ALARM_THRESHOLD_LAST = 0x8A,  ///< Last Data ID of the alarm-threshold block.

    // Live telemetry.
    TOTAL_VOLTAGE_CURRENT_SOC =
        0x90, ///< Pack voltage, current and state of charge. The core telemetry read. Data ID 0x90.
    CELL_VOLTAGE_EXTREMES =
        0x91, ///< Highest and lowest cell voltage, with cell numbers. Data ID 0x91.
    CELL_TEMPERATURE_EXTREMES =
        0x92, ///< Highest and lowest cell temperature, with sensor numbers. Data ID 0x92.
    CHARGE_DISCHARGE_MOS_STATUS = 0x93, ///< Charge/discharge state, MOS states, life counter,
                                        ///< remaining capacity. Data ID 0x93.
    STATUS_INFO = 0x94, ///< Cell and sensor counts, charger/load presence, IO bits, cycle count.
                        ///< Data ID 0x94.
    CELL_VOLTAGES =
        0x95, ///< Per-cell voltages, up to 48 over 16 frames, three cells per frame. Data ID 0x95.
    CELL_TEMPERATURES =
        0x96, ///< Per-sensor temperatures, up to 21 over 3 frames, seven per frame. Data ID 0x96.
    CELL_BALANCING_BITS = 0x97, ///< Per-cell balancing state as a 48-bit field. Data ID 0x97.
    FAULT_STATUS = 0x98, ///< Active fault and alarm bits, plus a numeric fault code. Data ID 0x98.
    BALANCING_STATE = 0x99, ///< Balancing state, current and position, as scalars. Data ID 0x99.
    BATTERY_STATUS = 0x9A,  ///< State of health. Data ID 0x9A.

    // Control.
    WAKEUP_SOURCE = 0xD8, ///< Which signal last woke the BMS. Data ID 0xD8.
    /**
     * @brief Turn the discharge MOSFET on or off. Data ID 0xD9.
     * @see HaidiBMS::set_discharge_mos
     */
    DISCHARGE_MOS_CONTROL = 0xD9,
    /**
     * @brief Turn the charge MOSFET on or off. Data ID 0xDA.
     * @see HaidiBMS::set_charge_mos
     */
    CHARGE_MOS_CONTROL = 0xDA,
};

/**
 * @brief The nine alarm classes of the 0x70..0x8A block, in Data ID order.
 *
 * The class selects which payload struct the reply decodes into: AlarmClass::CURRENT
 * yields a CurrentAlarmThreshold, the two temperature classes yield a
 * TempAlarmThreshold, and every other class yields an AlarmThreshold.
 *
 * @see alarm_threshold_id, HaidiBMS::poll_alarm_threshold
 */
enum class AlarmClass : uint8_t {
    CELL_OVERVOLTAGE = 0,       ///< Single-cell overvoltage, mV.
    CELL_UNDERVOLTAGE = 1,      ///< Single-cell undervoltage, mV.
    CURRENT = 2,                ///< Charge and discharge overcurrent, 0.1 A with a 30000 offset.
    HIGH_TEMPERATURE = 3,       ///< Over-temperature, degrees C with a 40 offset.
    LOW_TEMPERATURE = 4,        ///< Under-temperature, degrees C with a 40 offset.
    TOTAL_OVERVOLTAGE = 5,      ///< Pack overvoltage, 0.1 V.
    TOTAL_UNDERVOLTAGE = 6,     ///< Pack undervoltage, 0.1 V.
    VOLTAGE_DIFFERENCE = 7,     ///< Spread between highest and lowest cell, mV.
    TEMPERATURE_DIFFERENCE = 8, ///< Spread between highest and lowest sensor, degrees C.
};

/** @brief Severity tier of an alarm threshold. Each class defines all three. */
enum class AlarmLevel : uint8_t {
    LEVEL_1 = 0, ///< Warning tier, Data IDs 0x70..0x78.
    LEVEL_2 = 1, ///< Second tier, Data IDs 0x79..0x81.
    LEVEL_3 = 2  ///< Most severe tier, Data IDs 0x82..0x8A.
};

/** @brief Number of alarm classes, i.e. the stride between levels. */
static constexpr uint8_t ALARM_CLASS_COUNT = 9;
/** @brief Number of severity tiers. */
static constexpr uint8_t ALARM_LEVEL_COUNT = 3;

/**
 * @brief Maps an (alarm class, level) pair onto its Data ID.
 *
 * Level 1 occupies 0x70..0x78, level 2 0x79..0x81 and level 3 0x82..0x8A.
 *
 * @param cls Alarm class.
 * @param lvl Severity tier.
 * @return The Data ID for that combination.
 * @pre Both arguments are in range. Nothing here checks, and an out-of-range
 *      pair can land on another command's Data ID; HaidiBMS::poll_alarm_threshold
 *      does check.
 */
constexpr DataId alarm_threshold_id(AlarmClass cls, AlarmLevel lvl) {
    return static_cast<DataId>(static_cast<uint8_t>(DataId::ALARM_THRESHOLD_FIRST) +
                               (static_cast<uint8_t>(lvl) * ALARM_CLASS_COUNT) +
                               static_cast<uint8_t>(cls));
}

/**
 * @brief Whether a Data ID falls inside the alarm-threshold block.
 * @param id Data ID to test.
 * @return True for 0x70..0x8A inclusive.
 */
constexpr bool is_alarm_threshold_id(DataId id) {
    return static_cast<uint8_t>(id) >= static_cast<uint8_t>(DataId::ALARM_THRESHOLD_FIRST) &&
           static_cast<uint8_t>(id) <= static_cast<uint8_t>(DataId::ALARM_THRESHOLD_LAST);
}

/**
 * @brief Recovers the alarm class from an alarm-threshold Data ID.
 * @param id An alarm-threshold Data ID.
 * @return The class that alarm_threshold_id() was given to produce @p id.
 * @pre is_alarm_threshold_id(@p id) must hold; the result is meaningless otherwise.
 */
constexpr AlarmClass alarm_class_of(DataId id) {
    return static_cast<AlarmClass>(
        (static_cast<uint8_t>(id) - static_cast<uint8_t>(DataId::ALARM_THRESHOLD_FIRST)) %
        ALARM_CLASS_COUNT);
}

/**
 * @brief Recovers the severity tier from an alarm-threshold Data ID.
 * @param id An alarm-threshold Data ID.
 * @return The tier that alarm_threshold_id() was given to produce @p id.
 * @pre is_alarm_threshold_id(@p id) must hold; the result is meaningless otherwise.
 */
constexpr AlarmLevel alarm_level_of(DataId id) {
    return static_cast<AlarmLevel>(
        (static_cast<uint8_t>(id) - static_cast<uint8_t>(DataId::ALARM_THRESHOLD_FIRST)) /
        ALARM_CLASS_COUNT);
}

/**
 * @brief Computes a frame's checksum.
 *
 * Protocol section 5.3 note 2: "the checksum is the sum of all preceding data
 * (low byte only)". That is bytes 0..11 inclusive; truncation to @c uint8_t is
 * the "low byte" part. Not a CRC.
 *
 * @param bytes Frame whose first ::MSG_LAST_INDEX bytes are summed.
 * @return The value belonging at ::MSG_LAST_INDEX.
 */
uint8_t calc_chksum(const MessageBytes& bytes);

/**
 * @brief Builds a complete request frame, checksum included.
 * @param host Address to send from.
 * @param id   Command to request.
 * @param data Eight payload bytes.
 * @return The 13-byte frame, ready to transmit.
 */
MessageBytes encode(HostAddress host, DataId id, const DataBytes& data);

/**
 * @brief Builds a request frame with an all-zero payload.
 *
 * Every documented read command specifies Byte0..Byte7 as reserved, so this is
 * the right overload for all of them.
 *
 * @param host Address to send from.
 * @param id   Command to request.
 * @return The 13-byte frame, ready to transmit.
 */
MessageBytes encode(HostAddress host, DataId id);

/**
 * @brief Whether a Data ID is a command this library models.
 * @param id Data ID to test.
 * @return True for 0x50..0x6A, 0x70..0x8A, 0x90..0x9A and 0xD8..0xDA only, so
 *         DataId::NONE is correctly rejected.
 */
bool is_known_data_id(DataId id);

/**
 * @brief Reads a big-endian 16-bit field from a payload.
 * @param data The 8-byte payload.
 * @param off  Offset in the Byte0..Byte7 numbering used throughout section 6.
 * @return Bytes @p off and @p off + 1, most significant first.
 * @pre @p off <= 6. Not checked.
 */
constexpr uint16_t be16(const DataBytes& data, size_t off) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[off]) << 8) |
                                 static_cast<uint16_t>(data[off + 1]));
}

/**
 * @brief Reads a big-endian 32-bit field from a payload.
 * @param data The 8-byte payload.
 * @param off  Offset in the Byte0..Byte7 numbering used throughout section 6.
 * @return Bytes @p off to @p off + 3, most significant first.
 * @pre @p off <= 4. Not checked.
 */
constexpr uint32_t be32(const DataBytes& data, size_t off) {
    return (static_cast<uint32_t>(data[off]) << 24) | (static_cast<uint32_t>(data[off + 1]) << 16) |
           (static_cast<uint32_t>(data[off + 2]) << 8) | static_cast<uint32_t>(data[off + 3]);
}

/**
 * @brief Extracts the 8-byte payload from a received frame.
 * @param frame A complete frame.
 * @return Bytes 4..11 of @p frame.
 */
DataBytes data_of(const MessageBytes& frame);

/**
 * @brief Bias subtracted from a raw current reading.
 *
 * The document's three universal scalings are all integer-exact, so the codec
 * never needs floating point; convert to engineering units at the edge.
 */
static constexpr uint16_t CURRENT_OFFSET = 30000;
/** @brief Bias subtracted from a raw temperature reading. */
static constexpr uint8_t TEMPERATURE_OFFSET = 40;
/** @brief Bias added to a raw year byte. */
static constexpr uint16_t YEAR_OFFSET = 2000;

/**
 * @brief Converts a raw current field to deciamps.
 * @param raw Wire value, biased by ::CURRENT_OFFSET.
 * @return Current in 0.1 A; positive is charge, negative is discharge.
 */
constexpr int16_t to_current_da(uint16_t raw) {
    return static_cast<int16_t>(static_cast<int32_t>(raw) - CURRENT_OFFSET);
}

/**
 * @brief Converts a raw temperature byte to degrees Celsius.
 * @param raw Wire value, biased by ::TEMPERATURE_OFFSET.
 * @return Temperature in whole degrees Celsius, -40 to 215.
 */
constexpr int16_t to_temperature_c(uint8_t raw) {
    return static_cast<int16_t>(static_cast<int16_t>(raw) - TEMPERATURE_OFFSET);
}

/**
 * @brief Converts a raw year byte to a full year.
 * @param raw Wire value, biased by ::YEAR_OFFSET.
 * @return The full year, e.g. 2026 for a raw 26.
 */
constexpr uint16_t to_year(uint8_t raw) { return static_cast<uint16_t>(raw + YEAR_OFFSET); }

} // namespace haidi
