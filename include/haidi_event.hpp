// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file haidi_event.hpp
 * @brief The Event type delivered to callers, and its checked payload accessors.
 */

#include <haidi_payloads.hpp>
#include <haidi_protocol.hpp>

#include <cstdint>

namespace haidi
{

/**
 * @brief What an Event reports.
 *
 * The protocol defines no error or NAK reply, so the only failure signals
 * available are a timeout, a malformed or misaddressed frame, a Data ID that
 * does not match the outstanding request, a fault-record stream that stops
 * short, and -- for the two MOSFET control commands -- an echoed state
 * differing from the one requested.
 */
enum class EventKind : uint8_t {
    RESPONSE,      ///< A decoded reply; Event::payload is valid.
    TIMEOUT,       ///< No reply arrived before the deadline and retries were exhausted.
    PROTOCOL_ERROR ///< A frame was rejected or the exchange failed; see Event::error.
};

/**
 * @brief Why an Event failed.
 *
 * Set when Event::kind is EventKind::PROTOCOL_ERROR, and ErrorCode::NONE
 * otherwise, a timeout included.
 *
 * Not every error ends the request. Some are reported while it stays in
 * flight, to be answered, retried or timed out as usual, so one request can
 * produce several events and a handler must not take every error as its
 * outcome. Each code below says which kind it is.
 */
enum class ErrorCode : uint8_t {
    NONE = 0, ///< No error: a successful reply, or an EventKind::TIMEOUT.
    /** @brief The trailing checksum did not match. The request stays in flight. */
    CHECKSUM,
    /**
     * @brief Byte 1 was not the BMS address, so the frame is not ours.
     *
     * The request stays in flight: the real reply may still be on its way.
     */
    BAD_SOURCE_ADDRESS,
    /** @brief Byte 3 was not ::LENGTH_FLAG. The request stays in flight. */
    BAD_LENGTH,
    /**
     * @brief A frame did not echo the outstanding request's Data ID.
     *
     * Also reported for a frame that arrives with no request outstanding, which
     * can only be a late reply to one that already timed out. Either way, any
     * request in flight stays in flight.
     */
    UNEXPECTED_DATA_ID,
    /**
     * @brief The Data ID is not one this library decodes.
     *
     * Ends the request. Defensive only: no command reaches a decoder without
     * having passed is_known_data_id(), and every command that passes it has
     * one. This fires if a Data ID is ever added to that list without a
     * matching decoder.
     */
    UNKNOWN_DATA_ID,
    /**
     * @brief A paginated reply's frame counter was above 16.
     *
     * The request stays in flight. 16 is the highest counter any command can
     * use: 0x95 carries 48 cells in 16 frames, numbered 1..16 by a pack that
     * counts from 1. A stray 0xFF fault-stream sentinel lands here too. Frames
     * that arrive out of order, twice, or below the documented counter base
     * are accepted, not reported.
     */
    FRAME_SEQUENCE,
    /**
     * @brief The 0x64 fault-record stream went quiet before its 0xFF sentinel.
     *
     * Ends the request. Raised only once at least one frame of the stream has
     * arrived; if none did, the request times out instead. No other command
     * reports this: any other paginated reply that stops early is an
     * EventKind::RESPONSE with a smaller count or length, since those payloads
     * describe themselves.
     */
    TRUNCATED,
    /**
     * @brief 0xD9/0xDA echoed a state other than the one requested.
     *
     * Ends the request. The event still carries both states; see
     * as_discharge_mos_control(), as_charge_mos_control() and
     * as_mos_control_ack().
     */
    WRITE_REJECTED
};

/**
 * @brief The decoded payload of a successful reply.
 *
 * A tagged union rather than @c std::variant: @c std::variant::get throws, and
 * @c valueless_by_exception is dead weight in a build with exceptions disabled.
 * Event::id together with Event::kind is the tag. Prefer the checked @c as_*()
 * accessors below over reading a member directly -- they return @c nullptr on a
 * mismatch instead of handing back another command's bytes.
 */
union EventPayload {
    CapacityVoltage capacity_voltage;                 ///< Data ID 0x50.
    BmuCellTempCount bmu_cell_temp_count;             ///< Data ID 0x51.
    TotalChargeDischargeAh total_charge_discharge_ah; ///< Data ID 0x52.
    BatteryOperationMode battery_operation_mode;      ///< Data ID 0x53.
    FirmwareIndex firmware_index;                     ///< Data ID 0x54.
    TextPayload text;                                 ///< Data ID 0x55, 0x56, 0x57, 0x6A.
    CellVoltageAlarm cell_voltage_alarm;              ///< Data ID 0x59.
    TotalVoltageAlarm total_voltage_alarm;            ///< Data ID 0x5A.
    CurrentAlarm current_alarm;                       ///< Data ID 0x5B.
    TemperatureAlarm temperature_alarm;               ///< Data ID 0x5C.
    SocAlarm soc_alarm;                               ///< Data ID 0x5D.
    DifferenceAlarm difference_alarm;                 ///< Data ID 0x5E.
    BalancingParams balancing_params;                 ///< Data ID 0x5F.
    CurrentParams current_params;                     ///< Data ID 0x60.
    Rtc rtc;                                          ///< Data ID 0x58, 0x61.
    VersionPayload version;                           ///< Data ID 0x62, 0x63.
    FaultRecord fault_record;                         ///< Data ID 0x64.
    BoardNumber board_number;                         ///< Data ID 0x65.
    HeatingTemperature heating_temperature;           ///< Data ID 0x66.
    ActiveBalancingSwitch active_balancing_switch;    ///< Data ID 0x67.
    ActiveBalancingParams active_balancing_params;    ///< Data ID 0x68.
    InverterParams inverter_params;                   ///< Data ID 0x69.

    AlarmThreshold alarm_threshold;                ///< Data ID 0x70..0x8A, most classes.
    CurrentAlarmThreshold current_alarm_threshold; ///< Data ID 0x72, 0x7B, 0x84.
    TempAlarmThreshold temp_alarm_threshold;       ///< Data ID 0x73/0x74, 0x7C/0x7D, 0x85/0x86.

    TotalVoltageCurrentSoc total_voltage_current_soc;  ///< Data ID 0x90.
    CellVoltageExtremes cell_voltage_extremes;         ///< Data ID 0x91.
    CellTemperatureExtremes cell_temperature_extremes; ///< Data ID 0x92.
    ChargeDischargeMosStatus charge_discharge_mos;     ///< Data ID 0x93.
    StatusInfo status_info;                            ///< Data ID 0x94.
    CellVoltages cell_voltages;                        ///< Data ID 0x95.
    CellTemperatures cell_temperatures;                ///< Data ID 0x96.
    CellBalancingBits cell_balancing_bits;             ///< Data ID 0x97.
    FaultStatus fault_status;                          ///< Data ID 0x98.
    BalancingState balancing_state;                    ///< Data ID 0x99.
    BatteryStatus battery_status;                      ///< Data ID 0x9A.

    WakeupSource wakeup_source;    ///< Data ID 0xD8.
    MosControlAck mos_control_ack; ///< Data ID 0xD9, 0xDA.
};

/**
 * @brief One outcome of one request: a decoded reply, a timeout, or an error.
 *
 * Delivered to the handler registered with HaidiBMS::set_event_handler, and to
 * any per-request handler passed to the @c poll_*() or @c set_*_mos() call that
 * produced it.
 */
struct Event {
    EventKind kind = EventKind::RESPONSE; ///< Whether this is a reply, a timeout or an error.

    /**
     * @brief The command this event concerns.
     *
     * Normally the Data ID of the request in flight. For
     * ErrorCode::UNEXPECTED_DATA_ID and ErrorCode::BAD_SOURCE_ADDRESS it is
     * instead the Data ID the offending frame carried. For a checksum or length
     * error on a frame that arrived with no request in flight there is no
     * command to name, and the value carries no meaning.
     */
    DataId id = DataId::CAPACITY_VOLTAGE;

    /** @brief Why it failed; ErrorCode::NONE unless #kind is EventKind::PROTOCOL_ERROR. */
    ErrorCode error = ErrorCode::NONE;

    /**
     * @brief Retransmissions spent before giving up.
     *
     * Set on EventKind::TIMEOUT only. Every other event reports zero, including
     * a reply that arrived after one or more retransmissions.
     */
    uint8_t retries = 0;

    /**
     * @brief Distinct frames received for this event.
     *
     * 1 for a single-frame reply, ErrorCode::WRITE_REJECTED included; 4 for
     * each fault record; 0 for a timeout and for an error that rejected a frame
     * outright. For a paginated reply it counts every distinct frame that
     * arrived, including any beyond a missing one, which are not decoded, so it
     * can exceed what the payload holds. For ErrorCode::TRUNCATED it counts the
     * fault-stream frames received so far.
     *
     * A paginated reply that stopped early is still an EventKind::RESPONSE; its
     * payload's own count or length says how much was decoded.
     */
    uint8_t frames = 0;

    /**
     * @brief The decoded reply.
     *
     * Valid when #kind is EventKind::RESPONSE, plus the one deliberate
     * exception of ErrorCode::WRITE_REJECTED, which still carries the
     * MosControlAck holding the requested and applied states. Always read it
     * through the @c as_*() accessors, which enforce exactly that.
     *
     * Value-initialising the union activates its first member, which keeps
     * Event trivially copyable and default-constructible.
     */
    EventPayload payload{};
};

/**
 * @name Per-command payload accessors
 *
 * One accessor per command. Each returns @c nullptr unless the event is a
 * successful reply to that exact command, so the call names the command being
 * read and a caller never has to trust Event::id by hand. The alarm-threshold
 * accessors also take an AlarmLevel, since a class and a level together name
 * one Data ID. The one exception to "successful" is the MOSFET pair,
 * as_discharge_mos_control() and as_charge_mos_control(), which also answer an
 * ErrorCode::WRITE_REJECTED event.
 *
 * @code{.cpp}
 * if (const auto* v = as_total_voltage_current_soc(event)) {
 *     use(v->soc_pm);
 * }
 * @endcode
 */
///@{

/**
 * @brief The CapacityVoltage of a 0x50 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x50 reply.
 */
const CapacityVoltage* as_capacity_voltage(const Event& event);

/**
 * @brief The BmuCellTempCount of a 0x51 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x51 reply.
 */
const BmuCellTempCount* as_bmu_cell_temp_count(const Event& event);

/**
 * @brief The TotalChargeDischargeAh of a 0x52 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x52 reply.
 */
const TotalChargeDischargeAh* as_total_charge_discharge_ah(const Event& event);

/**
 * @brief The BatteryOperationMode of a 0x53 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x53 reply.
 */
const BatteryOperationMode* as_battery_operation_mode(const Event& event);

/**
 * @brief The FirmwareIndex of a 0x54 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x54 reply.
 */
const FirmwareIndex* as_firmware_index(const Event& event);

/**
 * @brief The manufacturer name from a 0x55 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x55 reply.
 */
const TextPayload* as_manufacturer_name(const Event& event);

/**
 * @brief The battery name from a 0x56 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x56 reply.
 */
const TextPayload* as_battery_name(const Event& event);

/**
 * @brief The battery serial number from a 0x57 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x57 reply.
 */
const TextPayload* as_battery_serial_number(const Event& event);

/**
 * @brief The production date from a 0x58 reply.
 *
 * @note The document marks every byte of this reply reserved. It is decoded
 * with the 0x61 layout -- year, month, day, hour, minute, second -- on the
 * assumption that the two share one; see Rtc.
 *
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x58 reply.
 */
const Rtc* as_battery_production_date(const Event& event);

/**
 * @brief The CellVoltageAlarm of a 0x59 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x59 reply.
 */
const CellVoltageAlarm* as_cell_voltage_alarm(const Event& event);

/**
 * @brief The TotalVoltageAlarm of a 0x5A reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x5A reply.
 */
const TotalVoltageAlarm* as_total_voltage_alarm(const Event& event);

/**
 * @brief The CurrentAlarm of a 0x5B reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x5B reply.
 */
const CurrentAlarm* as_current_alarm(const Event& event);

/**
 * @brief The TemperatureAlarm of a 0x5C reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x5C reply.
 */
const TemperatureAlarm* as_temperature_alarm(const Event& event);

/**
 * @brief The SocAlarm of a 0x5D reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x5D reply.
 */
const SocAlarm* as_soc_alarm(const Event& event);

/**
 * @brief The DifferenceAlarm of a 0x5E reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x5E reply.
 */
const DifferenceAlarm* as_difference_alarm(const Event& event);

/**
 * @brief The BalancingParams of a 0x5F reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x5F reply.
 */
const BalancingParams* as_balancing_params(const Event& event);

/**
 * @brief The CurrentParams of a 0x60 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x60 reply.
 */
const CurrentParams* as_current_params(const Event& event);

/**
 * @brief The Rtc of a 0x61 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x61 reply.
 */
const Rtc* as_rtc(const Event& event);

/**
 * @brief The software version from a 0x62 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x62 reply.
 */
const VersionPayload* as_software_version(const Event& event);

/**
 * @brief The hardware version from a 0x63 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x63 reply.
 */
const VersionPayload* as_hardware_version(const Event& event);

/**
 * @brief One FaultRecord from a 0x64 stream.
 * @param event The event to inspect.
 * @return The record, or @c nullptr unless @p event is a successful 0x64 reply.
 */
const FaultRecord* as_fault_record(const Event& event);

/**
 * @brief The BoardNumber of a 0x65 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x65 reply.
 */
const BoardNumber* as_board_number(const Event& event);

/**
 * @brief The HeatingTemperature of a 0x66 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x66 reply.
 */
const HeatingTemperature* as_heating_temperature(const Event& event);

/**
 * @brief The ActiveBalancingSwitch of a 0x67 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x67 reply.
 */
const ActiveBalancingSwitch* as_active_balancing_switch(const Event& event);

/**
 * @brief The ActiveBalancingParams of a 0x68 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x68 reply.
 */
const ActiveBalancingParams* as_active_balancing_params(const Event& event);

/**
 * @brief The InverterParams of a 0x69 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x69 reply.
 */
const InverterParams* as_inverter_params(const Event& event);

/**
 * @brief The SN serial number from a 0x6A reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x6A reply.
 */
const TextPayload* as_sn_serial_number(const Event& event);

/**
 * @brief A cell overvoltage threshold: 0x70, 0x79 or 0x82 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::CELL_OVERVOLTAGE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const AlarmThreshold* as_cell_overvoltage_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A cell undervoltage threshold: 0x71, 0x7A or 0x83 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::CELL_UNDERVOLTAGE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const AlarmThreshold* as_cell_undervoltage_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A charge and discharge overcurrent threshold: 0x72, 0x7B or 0x84 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::CURRENT, @p level). An out-of-range
 *         @p level always gives @c nullptr.
 */
const CurrentAlarmThreshold* as_overcurrent_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A high-temperature threshold: 0x73, 0x7C or 0x85 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::HIGH_TEMPERATURE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const TempAlarmThreshold* as_high_temperature_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A low-temperature threshold: 0x74, 0x7D or 0x86 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::LOW_TEMPERATURE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const TempAlarmThreshold* as_low_temperature_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A pack overvoltage threshold: 0x75, 0x7E or 0x87 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::TOTAL_OVERVOLTAGE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const AlarmThreshold* as_total_overvoltage_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A pack undervoltage threshold: 0x76, 0x7F or 0x88 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::TOTAL_UNDERVOLTAGE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const AlarmThreshold* as_total_undervoltage_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A cell voltage spread threshold: 0x77, 0x80 or 0x89 by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::VOLTAGE_DIFFERENCE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const AlarmThreshold* as_voltage_difference_threshold(const Event& event, AlarmLevel level);

/**
 * @brief A cell temperature spread threshold: 0x78, 0x81 or 0x8A by level.
 * @param event The event to inspect.
 * @param level The severity tier to expect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to
 *         alarm_threshold_id(AlarmClass::TEMPERATURE_DIFFERENCE, @p level). An
 *         out-of-range @p level always gives @c nullptr.
 */
const AlarmThreshold* as_temperature_difference_threshold(const Event& event, AlarmLevel level);

/**
 * @brief The TotalVoltageCurrentSoc of a 0x90 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x90 reply.
 */
const TotalVoltageCurrentSoc* as_total_voltage_current_soc(const Event& event);

/**
 * @brief The CellVoltageExtremes of a 0x91 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x91 reply.
 */
const CellVoltageExtremes* as_cell_voltage_extremes(const Event& event);

/**
 * @brief The CellTemperatureExtremes of a 0x92 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x92 reply.
 */
const CellTemperatureExtremes* as_cell_temperature_extremes(const Event& event);

/**
 * @brief The ChargeDischargeMosStatus of a 0x93 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x93 reply.
 */
const ChargeDischargeMosStatus* as_charge_discharge_mos(const Event& event);

/**
 * @brief The StatusInfo of a 0x94 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x94 reply.
 */
const StatusInfo* as_status_info(const Event& event);

/**
 * @brief The CellVoltages of a 0x95 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x95 reply.
 */
const CellVoltages* as_cell_voltages(const Event& event);

/**
 * @brief The CellTemperatures of a 0x96 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x96 reply.
 */
const CellTemperatures* as_cell_temperatures(const Event& event);

/**
 * @brief The CellBalancingBits of a 0x97 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x97 reply.
 */
const CellBalancingBits* as_cell_balancing_bits(const Event& event);

/**
 * @brief The FaultStatus of a 0x98 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x98 reply.
 */
const FaultStatus* as_fault_status(const Event& event);

/**
 * @brief The BalancingState of a 0x99 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x99 reply.
 */
const BalancingState* as_balancing_state(const Event& event);

/**
 * @brief The BatteryStatus of a 0x9A reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0x9A reply.
 */
const BatteryStatus* as_battery_status(const Event& event);

/**
 * @brief The WakeupSource of a 0xD8 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful 0xD8 reply.
 */
const WakeupSource* as_wakeup_source(const Event& event);

/**
 * @brief The MosControlAck of a 0xD9 discharge MOSFET write.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a 0xD9 reply that either
 *         succeeded or failed with ErrorCode::WRITE_REJECTED.
 */
const MosControlAck* as_discharge_mos_control(const Event& event);

/**
 * @brief The MosControlAck of a 0xDA charge MOSFET write.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a 0xDA reply that either
 *         succeeded or failed with ErrorCode::WRITE_REJECTED.
 */
const MosControlAck* as_charge_mos_control(const Event& event);

///@}

/**
 * @name Payload-shape accessors
 *
 * Generic helpers, each answering every command that shares one payload type,
 * for code that dispatches on the shape of a reply rather than on the command --
 * a logger, say. They apply the same checks as the per-command accessors, but a
 * non-null result does not say which of those commands replied: read Event::id
 * for that, or use the per-command accessor when the command is known.
 */
///@{

/**
 * @brief The TextPayload of a 0x55, 0x56, 0x57 or 0x6A reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to one
 *         of those four.
 * @see as_manufacturer_name, as_battery_name, as_battery_serial_number,
 *      as_sn_serial_number
 */
const TextPayload* as_text(const Event& event);

/**
 * @brief The VersionPayload of a 0x62 or 0x63 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful reply to one
 *         of those two.
 * @see as_software_version, as_hardware_version
 */
const VersionPayload* as_version(const Event& event);

/**
 * @brief The AlarmThreshold of a 0x70..0x8A reply, for the classes that use it.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful
 *         alarm-threshold reply of a class other than AlarmClass::CURRENT,
 *         AlarmClass::HIGH_TEMPERATURE and AlarmClass::LOW_TEMPERATURE.
 */
const AlarmThreshold* as_alarm_threshold(const Event& event);

/**
 * @brief The CurrentAlarmThreshold of a 0x72, 0x7B or 0x84 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful
 *         AlarmClass::CURRENT threshold reply.
 * @see as_overcurrent_threshold
 */
const CurrentAlarmThreshold* as_current_alarm_threshold(const Event& event);

/**
 * @brief The TempAlarmThreshold of a 0x73/0x74, 0x7C/0x7D or 0x85/0x86 reply.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a successful
 *         AlarmClass::HIGH_TEMPERATURE or AlarmClass::LOW_TEMPERATURE threshold
 *         reply.
 * @see as_high_temperature_threshold, as_low_temperature_threshold
 */
const TempAlarmThreshold* as_temp_alarm_threshold(const Event& event);

/**
 * @brief The MosControlAck of a 0xD9 or 0xDA MOSFET write.
 * @param event The event to inspect.
 * @return The payload, or @c nullptr unless @p event is a 0xD9 or 0xDA reply
 *         that either succeeded or failed with ErrorCode::WRITE_REJECTED.
 * @see as_discharge_mos_control, as_charge_mos_control
 */
const MosControlAck* as_mos_control_ack(const Event& event);

///@}

/** @name Human-readable names, for logging and diagnostics */
///@{

/**
 * @brief Name of an EventKind, e.g. @c "RESPONSE".
 * @param kind The value to name.
 * @return A static string; @c "?" for a value outside the enumeration.
 */
const char* to_string(EventKind kind);

/**
 * @brief Name of an ErrorCode, e.g. @c "WRITE_REJECTED".
 * @param error The value to name.
 * @return A static string; @c "?" for a value outside the enumeration.
 */
const char* to_string(ErrorCode error);

/**
 * @brief Name of a DataId, e.g. @c "capacity/voltage".
 * @param id The value to name.
 * @return A static string. Every alarm-threshold ID shares the name
 *         @c "alarm threshold"; a value that is no DataId gives @c "?".
 */
const char* to_string(DataId id);

///@}

} // namespace haidi
