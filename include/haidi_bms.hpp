// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file haidi_bms.hpp
 * @brief The session: request queue, timeouts, reassembly and event dispatch.
 */

#include <haidi_event.hpp>
#include <haidi_frame_parser.hpp>
#include <haidi_payloads.hpp>
#include <haidi_protocol.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

/**
 * @brief Depth of the pending-request queue.
 *
 * Override at compile time for targets that poll more commands per sweep than
 * the default allows. Must be at least 1.
 */
#ifndef HAIDI_REQUEST_QUEUE_CAPACITY
#define HAIDI_REQUEST_QUEUE_CAPACITY 24 // NOLINT(cppcoreguidelines-macro-usage)
#endif

namespace haidi
{

/**
 * @brief Outcome of enqueuing a request.
 *
 * Reports only whether the request was *accepted*. The reply itself always
 * arrives later through an event handler, never through this value.
 *
 * When more than one refusal applies, they are checked in the order
 * QueueStatus::NO_TX_HANDLER, QueueStatus::BAD_REQUEST, QueueStatus::QUEUE_FULL,
 * and the first that applies is returned.
 */
enum class QueueStatus : uint8_t {
    QUEUED,        ///< Accepted; transmitted now, or as soon as the bus is free.
    QUEUE_FULL,    ///< Too many requests already pending; nothing was sent.
    NO_TX_HANDLER, ///< No TX handler is registered; see HaidiBMS::set_tx_handler.
    /**
     * @brief The request is not one this library supports.
     *
     * Every named request method passes a valid Data ID, so in practice this
     * means an out-of-range argument to HaidiBMS::poll_alarm_threshold.
     */
    BAD_REQUEST
};

/**
 * @brief Asynchronous, transport-agnostic driver for one HAIDI BMS.
 *
 * The library never touches a serial port, a clock or a thread. The caller
 * wires up four things: set_tx_handler(), set_event_handler(), feed() and
 * tick(). A @c poll_*() call encodes a request, hands it to the TX handler if
 * the bus is free, and returns immediately; the reply arrives later as an Event.
 *
 * Protocol section 5.3 states that "two data items cannot be read at once", so
 * at most one request is ever outstanding. Requests issued while a transaction
 * is in flight are queued and sent in order as earlier ones complete;
 * @c poll_*() never blocks and never transmits out of turn.
 *
 * Every @c poll_*() and @c set_*_mos() call also accepts its own handler, so a
 * caller need not demultiplex results out of the session-wide one:
 *
 * @code{.cpp}
 * bms.poll_capacity_voltage(&on_capacity, &app);
 * @endcode
 *
 * That handler runs first, then the one from set_event_handler(); both always
 * run. It sees **every** event for its request, including EventKind::TIMEOUT and
 * EventKind::PROTOCOL_ERROR, and poll_fault_records() calls it once per record.
 * The @c ctx pointer is copied into the queue slot, so it must outlive the
 * transaction. A handler may issue further polls: they queue and go out once
 * dispatch finishes, never from inside the callback. Events belonging to no
 * request -- a frame arriving after its transaction already timed out -- reach
 * the session-wide handler only.
 *
 * One request can produce more than one event. A corrupted frame, or one from
 * the wrong address or for the wrong command, is reported as an
 * EventKind::PROTOCOL_ERROR while the request stays in flight, and its real
 * outcome follows later; ErrorCode says which errors end a request. A handler
 * that treats every error as final will act on a request that is still
 * running.
 *
 * Replies longer than one frame carry a frame counter whose starting value the
 * protocol document fixes per command, and which devices do not reliably
 * honour. The session reads it off the frames that arrive instead, so either
 * convention decodes on every paginated command with no configuration; see
 * @ref mainpage_pagination.
 *
 * The class allocates nothing, throws nothing, and is not thread-safe: call
 * every method from one context, or serialise access yourself.
 */
class HaidiBMS {
    public:
    /** @brief Called when an encoded frame must be put on the wire. */
    using TxFn = void (*)(void* ctx, const uint8_t* data, size_t len);

    /** @brief Called when a reply, timeout or protocol error occurs. */
    using EventFn = void (*)(void* ctx, const Event& event);

    /** @brief Pending requests that can be queued before QueueStatus::QUEUE_FULL. */
    static constexpr size_t QUEUE_CAPACITY = HAIDI_REQUEST_QUEUE_CAPACITY;

    /**
     * @brief Default wait for the first frame of a reply, milliseconds.
     *
     * A 13-byte frame takes 13.5 ms on the wire at 9600 8N1, so a turnaround
     * cannot be quicker than about 27 ms. The protocol document specifies no
     * timeout at all, so this and the two below are this library's policy.
     */
    static constexpr uint32_t DEFAULT_RESPONSE_TIMEOUT_MS = 250;

    /** @brief Default wait between frames of a paginated reply, milliseconds. */
    static constexpr uint32_t DEFAULT_INTERFRAME_TIMEOUT_MS = 250;

    /** @brief Default retransmissions before a request is abandoned. */
    static constexpr uint8_t DEFAULT_MAX_RETRIES = 1;

    /**
     * @brief Constructs a session.
     * @param host_address Address to send requests from.
     * @param bms_address  Address replies must come from; frames from any other
     *                     source are rejected with ErrorCode::BAD_SOURCE_ADDRESS.
     */
    explicit HaidiBMS(HostAddress host_address = HostAddress::COMP,
                      uint8_t bms_address = BMS_ADDRESS);

    // A session owns a bus on which only one request may be outstanding
    // (section 5.3). Two copies sharing a TX handler would interleave frames
    // while each believed it held the transaction, so neither copying nor
    // moving one is meaningful.
    ~HaidiBMS() = default;
    HaidiBMS(const HaidiBMS&) = delete;
    HaidiBMS& operator=(const HaidiBMS&) = delete;
    HaidiBMS(HaidiBMS&&) = delete;
    HaidiBMS& operator=(HaidiBMS&&) = delete;

    // --- wiring --------------------------------------------------------------

    /**
     * @brief Registers the callback that puts bytes on the wire.
     *
     * Required. Without it every request is refused with
     * QueueStatus::NO_TX_HANDLER.
     *
     * @param fn  Invoked with a complete encoded frame.
     * @param ctx Opaque pointer passed back to @p fn; must outlive the session.
     */
    void set_tx_handler(TxFn fn, void* ctx);

    /**
     * @brief Registers the session-wide event handler.
     *
     * Optional, but with neither this nor per-request handlers the session polls
     * the BMS and discards every decoded result.
     *
     * @param fn  Invoked for every event, after any per-request handler.
     * @param ctx Opaque pointer passed back to @p fn; must outlive the session.
     */
    void set_event_handler(EventFn fn, void* ctx);

    /**
     * @brief Sets how long to wait for the first frame of a reply.
     *
     * Applies from the next transmission, retransmissions included; a deadline
     * already running is left alone.
     *
     * @param ms Timeout in milliseconds. Defaults to #DEFAULT_RESPONSE_TIMEOUT_MS.
     */
    void set_response_timeout_ms(uint32_t ms) { response_timeout_ms_ = ms; }

    /**
     * @brief Sets how long to wait between frames of a multi-frame reply.
     *
     * Covers paginated reads and the 0x64 fault-record stream. For a paginated
     * read of unknown length this is not an error path but the normal
     * end-of-reply signal. Too long makes such reads slow; too short truncates
     * them, and a truncated read is reported as a *successful* reply carrying a
     * smaller count. Applies from the next frame received.
     *
     * @param ms Timeout in milliseconds. Defaults to #DEFAULT_INTERFRAME_TIMEOUT_MS.
     */
    void set_interframe_timeout_ms(uint32_t ms) { interframe_timeout_ms_ = ms; }

    /**
     * @brief Sets how many times an unanswered request is retransmitted.
     *
     * A request is resent only when its deadline passes with no frame of its
     * reply accepted. Once any frame of a multi-frame reply is in, the deadline
     * ends the transfer instead of resending it. Takes effect at the next
     * deadline, including for the request already in flight.
     *
     * @param retries Retransmissions after the first attempt; 0 disables them.
     *                Defaults to #DEFAULT_MAX_RETRIES.
     */
    void set_max_retries(uint8_t retries) { max_retries_ = retries; }

    // --- driving the session -------------------------------------------------

    /**
     * @brief Hands received bytes to the framer.
     *
     * Frames may span calls. Before this returns, any number of events may be
     * dispatched, and the TX handler may be called to send the next queued
     * request.
     *
     * @param data Bytes as they came off the transport, in any chunking.
     *             Ignored when @c nullptr.
     * @param len  Number of bytes at @p data.
     */
    void feed(const uint8_t* data, size_t len);

    /**
     * @brief Advances the clock, expiring deadlines.
     *
     * @param now_ms Monotonic millisecond count. Wraparound at 2^32 ms is handled.
     *
     * @warning This is not optional bookkeeping. Deadlines are examined nowhere
     * else, so a session that is never ticked has no way to give up on anything:
     * - a single lost reply stalls it **permanently**. The transaction stays
     *   outstanding and every later @c poll_*() returns QueueStatus::QUEUED
     *   while transmitting nothing. The first outward symptom is
     *   QueueStatus::QUEUE_FULL once the queue fills, which says nothing about
     *   the cause.
     * - retries never happen, whatever set_max_retries() was given.
     * - poll_cell_voltages() and poll_cell_temperatures() with no count, passed
     *   or cached, never complete at all, because for them the inter-frame
     *   timeout is the normal end-of-reply signal rather than an error path. The
     *   same goes for a fault-record stream whose 0xFF sentinel never arrives.
     *
     * Sending and receiving are themselves clock-free, so a link on which every
     * reply does arrive appears to work perfectly without this call. That is
     * what makes omitting it dangerous: it passes on the bench and stalls in the
     * field, on the first dropped byte.
     */
    void tick(uint32_t now_ms);

    // --- static configuration reads -----------------------------------------

    /**
     * @brief Design capacity and design voltage (0x50).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_capacity_voltage(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CAPACITY_VOLTAGE, fn, ctx);
    }
    /**
     * @brief Acquisition board, cell and temperature-sensor counts (0x51).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_bmu_cell_temp_count(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BMU_CELL_TEMP_COUNT, fn, ctx);
    }
    /**
     * @brief Lifetime charge and discharge totals, Ah (0x52).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_total_charge_discharge_ah(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::TOTAL_CHARGE_DISCHARGE_AH, fn, ctx);
    }
    /**
     * @brief Battery chemistry, power mode, build date, sleep time, zero-drift current (0x53).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_battery_operation_mode(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BATTERY_OPERATION_MODE, fn, ctx);
    }
    /**
     * @brief Firmware index, 8 ASCII bytes in one frame (0x54).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_firmware_index(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::FIRMWARE_INDEX, fn, ctx);
    }
    /**
     * @brief Manufacturer name, 16 bytes over 3 frames (0x55).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_manufacturer_name(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::MANUFACTURER_NAME, fn, ctx);
    }
    /**
     * @brief Battery name, 32 bytes over 5 frames (0x56).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_battery_name(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BATTERY_NAME, fn, ctx);
    }
    /**
     * @brief Battery serial number, 32 bytes over 5 frames (0x57).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_battery_serial_number(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BATTERY_SERIAL_NUMBER, fn, ctx);
    }
    /**
     * @brief Production date. Reply layout undefined; raw bytes returned (0x58).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_battery_production_date(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BATTERY_PRODUCTION_DATE, fn, ctx);
    }
    /**
     * @brief Cell over- and under-voltage alarm thresholds (0x59).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_cell_voltage_alarm(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CELL_VOLTAGE_ALARM, fn, ctx);
    }
    /**
     * @brief Pack over- and under-voltage alarm thresholds (0x5A).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_total_voltage_alarm(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::TOTAL_VOLTAGE_ALARM, fn, ctx);
    }
    /**
     * @brief Charge and discharge overcurrent alarms. Charge first (0x5B).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_current_alarm(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CURRENT_ALARM, fn, ctx);
    }
    /**
     * @brief Charge and discharge over/under temperature alarms (0x5C).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_temperature_alarm(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::TEMPERATURE_ALARM, fn, ctx);
    }
    /**
     * @brief State-of-charge high and low alarm thresholds (0x5D).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_soc_alarm(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::SOC_ALARM, fn, ctx);
    }
    /**
     * @brief Cell voltage and temperature spread alarm thresholds (0x5E).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_difference_alarm(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::DIFFERENCE_ALARM, fn, ctx);
    }
    /**
     * @brief Passive balancing start voltage and start difference (0x5F).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_balancing_params(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BALANCING_PARAMS, fn, ctx);
    }
    /**
     * @brief Current alarm value and shunt resistance (0x60).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_current_params(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CURRENT_PARAMS, fn, ctx);
    }
    /**
     * @brief Real-time clock reading (0x61).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_rtc(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::RTC, fn, ctx);
    }
    /**
     * @brief Software version, 14 bytes over 2 frames (0x62).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_software_version(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::SOFTWARE_VERSION, fn, ctx);
    }
    /**
     * @brief Hardware version, 14 bytes over 2 frames (0x63).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_hardware_version(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::HARDWARE_VERSION, fn, ctx);
    }
    /**
     * @brief Board number and total BMU count (0x65).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_board_number(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BOARD_NUMBER, fn, ctx);
    }
    /**
     * @brief Heater and fan start temperatures, key MOS control mode (0x66).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_heating_temperature(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::HEATING_TEMPERATURE, fn, ctx);
    }
    /**
     * @brief Active balancing on/off state (0x67).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_active_balancing_switch(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::ACTIVE_BALANCING_SWITCH, fn, ctx);
    }
    /**
     * @brief Series cell count and active balancing current (0x68).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_active_balancing_params(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::ACTIVE_BALANCING_PARAMS, fn, ctx);
    }
    /**
     * @brief Paired inverter make and its communication bus (0x69).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_inverter_params(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::INVERTER_PARAMS, fn, ctx);
    }
    /**
     * @brief SN serial number, 32 bytes over 5 frames (0x6A).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_sn_serial_number(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::SN_SERIAL_NUMBER, fn, ctx);
    }

    /**
     * @brief Requests every stored fault record (0x64).
     *
     * Unlike every other command, one request triggers a *stream*: the handler
     * is called once per FaultRecord, ending at the 0xFF sentinel frame. The
     * sentinel itself produces no event.
     *
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_fault_records(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::FAULT_RECORDS, fn, ctx);
    }

    // --- alarm thresholds, 0x70..0x8A ---------------------------------------

    /**
     * @brief Requests one alarm threshold from the 0x70..0x8A block.
     *
     * Collapses 27 commands into one call. The payload type follows the class:
     * CurrentAlarmThreshold for AlarmClass::CURRENT, TempAlarmThreshold for the
     * two temperature classes, AlarmThreshold otherwise.
     *
     * @param cls   Which alarm class to read.
     * @param level Which severity tier.
     * @param fn    Optional per-request handler, invoked before the session-wide one.
     * @param ctx   Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused --
     *         QueueStatus::BAD_REQUEST if @p cls or @p level is out of range.
     * @see alarm_threshold_id
     */
    QueueStatus poll_alarm_threshold(AlarmClass cls, AlarmLevel level, EventFn fn = nullptr,
                                     void* ctx = nullptr) {
        // An out-of-range pair can alias onto a *valid* Data ID -- class 9 at
        // level 1 computes 0x79, a real command -- so it has to be caught here
        // rather than left to is_known_data_id() on the result. DataId::NONE is
        // rejected by that check, which keeps the missing-transmit-handler case
        // reported ahead of the bad-argument case, as for every other request.
        const bool in_range = static_cast<uint8_t>(cls) < ALARM_CLASS_COUNT &&
                              static_cast<uint8_t>(level) < ALARM_LEVEL_COUNT;
        return request(in_range ? alarm_threshold_id(cls, level) : DataId::NONE, fn, ctx);
    }

    // --- live telemetry ------------------------------------------------------

    /**
     * @brief Pack voltage, current and state of charge. The core telemetry read (0x90).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_total_voltage_current_soc(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::TOTAL_VOLTAGE_CURRENT_SOC, fn, ctx);
    }
    /**
     * @brief Highest and lowest cell voltage, with cell numbers (0x91).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_cell_voltage_extremes(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CELL_VOLTAGE_EXTREMES, fn, ctx);
    }
    /**
     * @brief Highest and lowest cell temperature, with sensor numbers (0x92).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_cell_temperature_extremes(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CELL_TEMPERATURE_EXTREMES, fn, ctx);
    }
    /**
     * @brief Charge/discharge state, MOS states, life counter, remaining capacity (0x93).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_charge_discharge_mos_status(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CHARGE_DISCHARGE_MOS_STATUS, fn, ctx);
    }
    /**
     * @brief Cell and sensor counts, charger/load presence, IO bits, cycle count (0x94).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_status_info(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::STATUS_INFO, fn, ctx);
    }
    /**
     * @brief Per-cell balancing state as a 48-bit field (0x97).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_cell_balancing_bits(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::CELL_BALANCING_BITS, fn, ctx);
    }
    /**
     * @brief Active fault and alarm bits, plus a numeric fault code (0x98).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_fault_status(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::FAULT_STATUS, fn, ctx);
    }
    /**
     * @brief Balancing state, current and position, as scalars (0x99).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_balancing_state(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BALANCING_STATE, fn, ctx);
    }
    /**
     * @brief State of health (0x9A).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_battery_status(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::BATTERY_STATUS, fn, ctx);
    }

    /**
     * @brief Requests every cell voltage (0x95).
     *
     * The reply is paginated and the protocol never states how many cells the
     * pack has, so the frame count must come from somewhere. Pass it explicitly,
     * or leave it at 0 to use the count cached from the most recent 0x94 or 0x51
     * reply. The cache is read when the request is transmitted, not when it is
     * queued, so a 0x94 queued just ahead of this one is enough.
     *
     * With neither, the reply ends on the inter-frame timeout and whatever
     * arrived is reported -- CellVoltages carries its own CellVoltages::count,
     * so a short reply is self-describing. With no count to trim to, that count
     * is a whole number of frames' worth, three cells each, so any padding in
     * the last frame is reported as cells.
     *
     * @param cell_count Cells to expect, or 0 to use the cached count.
     * @param fn         Optional per-request handler, invoked before the session-wide one.
     * @param ctx        Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     *
     * @warning The inter-frame timeout fires only inside tick(). An uncounted
     * read on a session that is never ticked therefore never completes, even
     * over a link that delivers every frame.
     *
     * @note The protocol document specifies that 0x95 numbers its frames from 0,
     * and packs have been observed numbering them from 1 regardless. The session
     * reads the base off the frames that arrive rather than asserting the
     * documented one, so either behaviour decodes correctly and no configuration
     * is needed. Were it not handled, the first frame's worth of cells would read
     * 0 mV and every real reading would be shifted three places along, with the
     * last three cells lost at the trim.
     *
     * @see cached_cell_count
     */
    QueueStatus poll_cell_voltages(uint8_t cell_count = 0, EventFn fn = nullptr,
                                   void* ctx = nullptr) {
        return request_paginated(DataId::CELL_VOLTAGES, cell_count, fn, ctx);
    }
    /**
     * @brief Requests every cell temperature (0x96).
     *
     * Sized exactly like poll_cell_voltages(), from @p temp_count or the cached
     * sensor count. With neither, the reported count is a whole number of
     * frames' worth, seven sensors each.
     *
     * @param temp_count Sensors to expect, or 0 to use the cached count.
     * @param fn         Optional per-request handler, invoked before the session-wide one.
     * @param ctx        Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     *
     * @warning Carries the same dependency on tick() as poll_cell_voltages().
     * @note Frame numbering is handled the same way too: see the @c \@note on
     * poll_cell_voltages().
     * @see cached_temp_count
     */
    QueueStatus poll_cell_temperatures(uint8_t temp_count = 0, EventFn fn = nullptr,
                                       void* ctx = nullptr) {
        return request_paginated(DataId::CELL_TEMPERATURES, temp_count, fn, ctx);
    }

    // --- control -------------------------------------------------------------

    /**
     * @brief Which signal last woke the BMS (0xD8).
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus poll_wakeup_source(EventFn fn = nullptr, void* ctx = nullptr) {
        return request(DataId::WAKEUP_SOURCE, fn, ctx);
    }

    /**
     * @name MOSFET control
     *
     * The protocol has no ACK or NAK: the reply simply echoes the state that was
     * applied. An echo differing from the request surfaces as
     * ErrorCode::WRITE_REJECTED, and that event still carries a MosControlAck
     * holding both values, so a caller can see what was asked for against what
     * the device did.
     */
    ///@{

    /**
     * @brief Turns the discharge MOSFET on or off (0xD9).
     * @param on  Desired state, true for conducting.
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus set_discharge_mos(bool on, EventFn fn = nullptr, void* ctx = nullptr) {
        return request_mos(DataId::DISCHARGE_MOS_CONTROL, on, fn, ctx);
    }

    /**
     * @brief Turns the charge MOSFET on or off (0xDA).
     * @param on  Desired state, true for conducting.
     * @param fn  Optional per-request handler, invoked before the session-wide one.
     * @param ctx Opaque pointer passed to @p fn; must outlive the transaction.
     * @return QueueStatus::QUEUED, or why the request was refused.
     */
    QueueStatus set_charge_mos(bool on, EventFn fn = nullptr, void* ctx = nullptr) {
        return request_mos(DataId::CHARGE_MOS_CONTROL, on, fn, ctx);
    }

    ///@}

    // --- introspection -------------------------------------------------------

    /**
     * @brief Whether a transaction is on the wire awaiting its reply.
     * @return True from transmission until the request completes or is abandoned.
     */
    [[nodiscard]] bool busy() const { return state_ != State::IDLE; }

    /**
     * @brief Requests queued, including the one in flight.
     * @return A count from 0 to #QUEUE_CAPACITY.
     */
    [[nodiscard]] size_t pending() const { return queue_count_; }

    /**
     * @brief The command currently on the wire.
     * @return Its Data ID, or DataId::NONE when idle.
     */
    [[nodiscard]] DataId in_flight() const {
        return state_ == State::AWAITING && queue_count_ > 0 ? head().id : DataId::NONE;
    }

    /**
     * @brief Cells reported by the most recent 0x94 or 0x51 reply.
     *
     * Used to size a poll_cell_voltages() call given no explicit count. From
     * 0x51 it is the sum over the three acquisition boards. Survives reset().
     *
     * @return The cell count, or 0 until one of those commands has been answered.
     */
    [[nodiscard]] uint8_t cached_cell_count() const { return cached_cell_count_; }

    /**
     * @brief Temperature sensors reported by the most recent 0x94 or 0x51 reply.
     *
     * Used to size a poll_cell_temperatures() call given no explicit count, and
     * kept exactly like cached_cell_count().
     *
     * @return The sensor count, or 0 until one of those commands has been answered.
     */
    [[nodiscard]] uint8_t cached_temp_count() const { return cached_temp_count_; }

    /**
     * @brief Bytes the framer threw away as junk.
     *
     * A steadily climbing count means a noisy link or a baud-rate mismatch.
     *
     * @return Bytes discarded since construction or the last reset().
     */
    [[nodiscard]] uint32_t discarded_bytes() const { return parser_.discarded(); }

    /**
     * @brief Abandons the in-flight transaction and clears the queue.
     *
     * Also drops any partially received frame and zeroes discarded_bytes(). No
     * events are emitted, so a caller waiting on a reply will simply never hear
     * about it. The handlers, timeouts, retry limit and cached cell and sensor
     * counts are all kept.
     */
    void reset();

    private:
    enum class State : uint8_t { IDLE, AWAITING };

    struct Request {
        DataId id = DataId::CAPACITY_VOLTAGE;
        DataBytes data{};
        uint8_t hint = 0;           // caller-supplied item count for paginated reads
        EventFn on_event = nullptr; // per-request handler, run before the global one
        void* on_event_ctx = nullptr;
    };

    QueueStatus enqueue(const Request& req);

    // Not public: every Data ID the protocol defines already has a named
    // poll_*()/set_*() method, so there is nothing an escape hatch could reach
    // that a typed call does not.
    QueueStatus request(DataId id, EventFn fn = nullptr, void* ctx = nullptr);
    QueueStatus request_paginated(DataId id, uint8_t hint, EventFn fn, void* ctx);
    QueueStatus request_mos(DataId id, bool on, EventFn fn, void* ctx);

    [[nodiscard]] const Request& head() const { return queue_[queue_head_]; }

    void pump();           // start the next transaction if the bus is free
    void transmit();       // encode and hand the head request to the TX handler
    void begin_awaiting(); // reset per-transaction reassembly state
    void complete();       // pop the head request and pump the next one
    void emit(const Event& event);

    void on_frame(const MessageBytes& frame);
    void on_frame_error(FrameError error);
    void handle_single(DataId id, const DataBytes& data);
    void handle_multi(DataId id, const DataBytes& data);
    void handle_stream(DataId id, const DataBytes& data);
    void finish_multi(DataId id);
    void note_counts(DataId id, const DataBytes& data);

    static void frame_trampoline(void* ctx, const MessageBytes& frame);
    static void error_trampoline(void* ctx, FrameError error);

    HostAddress host_address_;
    uint8_t bms_address_;

    TxFn tx_fn_ = nullptr;
    void* tx_ctx_ = nullptr;
    EventFn event_fn_ = nullptr;
    void* event_ctx_ = nullptr;

    uint32_t response_timeout_ms_ = DEFAULT_RESPONSE_TIMEOUT_MS;
    uint32_t interframe_timeout_ms_ = DEFAULT_INTERFRAME_TIMEOUT_MS;
    uint8_t max_retries_ = DEFAULT_MAX_RETRIES;

    FrameParser parser_;

    std::array<Request, QUEUE_CAPACITY> queue_{};
    size_t queue_head_ = 0;
    size_t queue_count_ = 0;

    State state_ = State::IDLE;
    uint32_t now_ms_ = 0;
    uint32_t deadline_ms_ = 0;
    uint8_t retries_ = 0;

    // Guards against a poll_*() issued from inside the event handler
    // transmitting before the current transaction has been retired.
    bool dispatching_ = false;

    // Reassembly of a paginated reply.
    //
    // Frames are filed under the counter the device actually put in Byte0,
    // never under a base-corrected index: which base a command counts from is
    // decided at the end of the transfer, from the frames that turned up. That
    // makes 0x95's highest legal counter 16 rather than 15 -- 48 cells over 16
    // frames numbered from 1 -- so the buffer is sized for a full seven-byte
    // frame at that counter. MAX_REASSEMBLY_LEN stays the cap on the *decoded*
    // payload, which is unaffected by where the counter starts.
    static constexpr uint8_t MAX_FRAME_INDEX = 16;
    static constexpr size_t ASSEMBLY_LEN = (static_cast<size_t>(MAX_FRAME_INDEX) + 1) * 7;
    std::array<uint8_t, ASSEMBLY_LEN> assembly_{};
    uint32_t seen_mask_ = 0;  // one bit per frame counter, so duplicates and
    uint8_t frames_seen_ = 0; // out-of-order frames are handled correctly
    uint8_t expected_frames_ = 0;
    // Items (cells or sensors) the reply should carry, for 0x95/0x96 where the
    // last frame is padded past the real count. Zero when unknown.
    uint8_t expected_items_ = 0;

    // Accumulation of one 27-byte fault record. Frames carry 7 bytes each, so
    // four frames deliver 28 bytes and the last one is slack.
    static constexpr size_t FAULT_FRAMES_PER_RECORD = 4;
    static constexpr size_t FAULT_BUFFER_LEN = FAULT_FRAMES_PER_RECORD * 7;
    std::array<uint8_t, FAULT_BUFFER_LEN> record_{};
    size_t record_len_ = 0;

    uint8_t cached_cell_count_ = 0;
    uint8_t cached_temp_count_ = 0;
};

} // namespace haidi
