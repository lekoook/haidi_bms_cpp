// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Polls a HAIDI BMS over a serial port and prints decoded telemetry.
//
//     ./haidi_poll_loop [device] [sweep_ms]
//     ./haidi_poll_loop /dev/ttyUSB0 1000
//
// This is the smallest complete wiring of the four things HaidiBMS needs: a
// transmit callback, an event callback, received bytes pushed in through
// feed(), and a monotonic millisecond clock pushed in through tick().

#include <haidi_bms.hpp>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

using namespace haidi;

namespace
{

// Section 4.1: 9600 bps, 9600/N/8/1, no flow control.
int open_serial(const char* device) {
    const int fd = ::open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "open(%s): %s\n", device, std::strerror(errno));
        return -1;
    }

    termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        std::fprintf(stderr, "tcgetattr: %s\n", std::strerror(errno));
        ::close(fd);
        return -1;
    }

    ::cfmakeraw(&tty);
    ::cfsetispeed(&tty, B9600);
    ::cfsetospeed(&tty, B9600);
    tty.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= static_cast<tcflag_t>(CS8 | CREAD | CLOCAL);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::fprintf(stderr, "tcsetattr: %s\n", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

uint32_t monotonic_ms() {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>((static_cast<uint64_t>(ts.tv_sec) * 1000U) +
                                 (static_cast<uint64_t>(ts.tv_nsec) / 1000000U));
}

void on_tx(void* ctx, const uint8_t* data, size_t len) {
    const int fd = *static_cast<int*>(ctx);
    if (::write(fd, data, len) < 0) {
        std::fprintf(stderr, "write: %s\n", std::strerror(errno));
    }
}

void print_response(const Event& event) {
    if (const TotalVoltageCurrentSoc* p = as_total_voltage_current_soc(event)) {
        std::printf("  pack      %6.1f V  %7.1f A  SOC %5.1f %%\n", p->cumulative_voltage_dv / 10.0,
                    p->current_da / 10.0, p->soc_pm / 10.0);
    } else if (const CellVoltageExtremes* p = as_cell_voltage_extremes(event)) {
        std::printf("  cells     high %u mV (#%u)  low %u mV (#%u)  spread %d mV\n", p->highest_mv,
                    p->highest_cell_number, p->lowest_mv, p->lowest_cell_number,
                    static_cast<int>(p->highest_mv) - static_cast<int>(p->lowest_mv));
    } else if (const CellTemperatureExtremes* p = as_cell_temperature_extremes(event)) {
        std::printf("  temps     high %d C (#%u)  low %d C (#%u)\n", p->highest_c,
                    p->highest_cell_number, p->lowest_c, p->lowest_cell_number);
    } else if (const ChargeDischargeMosStatus* p = as_charge_discharge_mos(event)) {
        static const char* const STATE[] = {"idle", "charging", "discharging"};
        std::printf("  mos       %s  chg %u  dsg %u  remaining %u mAh  life %u\n",
                    p->state < 3 ? STATE[p->state] : "?", p->charge_mos_state,
                    p->discharge_mos_state, p->remaining_capacity_mah, p->bms_life);
    } else if (const StatusInfo* p = as_status_info(event)) {
        std::printf("  status    %u cells  %u sensors  charger %s  load %s  %u cycles  %d C\n",
                    p->series_count, p->temp_sensor_count, p->charger_connected ? "yes" : "no",
                    p->load_connected ? "yes" : "no", p->cycle_count, p->onboard_temp_c);
    } else if (const FaultStatus* p = as_fault_status(event)) {
        if (p->any_fault() || p->fault_code() != 0) {
            std::printf("  FAULT     %02X %02X %02X %02X %02X %02X %02X  code %u\n", p->bits[0],
                        p->bits[1], p->bits[2], p->bits[3], p->bits[4], p->bits[5], p->bits[6],
                        p->fault_code());
        } else {
            std::printf("  faults    none\n");
        }
    } else if (const BatteryStatus* p = as_battery_status(event)) {
        std::printf("  health    SOH %.1f %%\n", p->soh_pm / 10.0);
    } else if (const CapacityVoltage* p = as_capacity_voltage(event)) {
        std::printf("  design    %u mAh  %u mV\n", p->capacity_mah, p->voltage_mv);
    } else if (const CellVoltages* p = as_cell_voltages(event)) {
        std::printf("  %u cell voltages:", p->count);
        for (uint8_t i = 0; i < p->count; ++i) {
            std::printf(" %u", p->mv[i]);
        }
        std::printf("\n");
    } else {
        std::printf("  %s: decoded, no printer\n", to_string(event.id));
    }
}

// Filled in by the start-up reads below.
struct Identification {
    uint32_t capacity_mah = 0;
    uint8_t cells = 0;
    uint8_t sensors = 0;
};

// A per-request handler. It sees every event for the request it was passed to,
// so it has to cope with a timeout or a protocol error as well as a reply. Not
// every error is final: after a corrupted frame, say, the real outcome follows.
void on_identified(void* ctx, const Event& event) {
    auto& self = *static_cast<Identification*>(ctx);

    if (event.kind != EventKind::RESPONSE) {
        std::fprintf(stderr, "identification: %s failed (%s)\n", to_string(event.id),
                     event.kind == EventKind::TIMEOUT ? "timeout" : to_string(event.error));
        return;
    }

    if (const CapacityVoltage* value = as_capacity_voltage(event)) {
        self.capacity_mah = value->capacity_mah;
    } else if (const StatusInfo* value = as_status_info(event)) {
        self.cells = value->series_count;
        self.sensors = value->temp_sensor_count;
    }

    if (self.capacity_mah != 0 && self.cells != 0) {
        std::printf("identified: %u mAh, %u cells, %u sensors\n", self.capacity_mah, self.cells,
                    self.sensors);
    }
}

void on_event(void* /*ctx*/, const Event& event) {
    switch (event.kind) {
        case EventKind::RESPONSE:
            print_response(event);
            break;
        case EventKind::TIMEOUT:
            // The protocol has no "unsupported command" reply, so a command
            // the firmware does not implement also lands here.
            std::printf("  %-26s timeout after %u retries\n", to_string(event.id), event.retries);
            break;
        case EventKind::PROTOCOL_ERROR:
            std::printf("  %-26s error: %s\n", to_string(event.id), to_string(event.error));
            break;
    }
}

} // namespace

int main(int argc, char** argv) {
    const char* device = argc > 1 ? argv[1] : "/dev/ttyUSB0";
    const uint32_t sweep_ms = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 1000;

    int fd = open_serial(device);
    if (fd < 0) {
        return 1;
    }

    HaidiBMS bms{HostAddress::COMP};
    bms.set_tx_handler(&on_tx, &fd);
    bms.set_event_handler(&on_event, nullptr);
    bms.tick(monotonic_ms());

    // One-off reads at start-up. These carry their own handler, so the result
    // lands where it is needed without on_event() having to switch on the Data
    // ID -- contrast the repeating sweep below, which is generic enough that
    // the session-wide handler is the right home for it. Both handlers run for
    // these two replies: the per-request one first, then on_event().
    //
    // `startup` must outlive the transaction, since HaidiBMS keeps the pointer
    // in its queue slot until the reply arrives. A local in main() is fine; a
    // local in a function that returns first would not be.
    static Identification startup;
    bms.poll_capacity_voltage(&on_identified, &startup);

    // The 0x94 reply also caches the cell and sensor counts, which is what lets
    // a later poll_cell_voltages() size itself.
    bms.poll_status_info(&on_identified, &startup);

    uint32_t next_sweep = monotonic_ms();

    for (;;) {
        const uint32_t now = monotonic_ms();

        // Drives timeouts and retries. Skip this and a single lost reply leaves
        // the session waiting on it forever, with later polls queueing silently
        // behind it.
        bms.tick(now);

        uint8_t buf[64];
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n > 0) {
            bms.feed(buf, static_cast<size_t>(n));
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::fprintf(stderr, "read: %s\n", std::strerror(errno));
            break;
        }

        if (static_cast<int32_t>(now - next_sweep) >= 0 && !bms.busy() && bms.pending() == 0) {
            std::printf("--- sweep at %u ms (discarded %u junk bytes) ---\n", now,
                        bms.discarded_bytes());
            bms.poll_total_voltage_current_soc();
            bms.poll_cell_voltage_extremes();
            bms.poll_cell_temperature_extremes();
            bms.poll_charge_discharge_mos_status();
            bms.poll_status_info();
            bms.poll_fault_status();
            bms.poll_battery_status();
            next_sweep = now + sweep_ms;
        }

        // 13 bytes at 9600 8N1 take 13.5 ms, so there is nothing to gain from
        // spinning faster than a couple of milliseconds.
        timespec idle{0, 2 * 1000 * 1000};
        ::nanosleep(&idle, nullptr);
    }

    ::close(fd);
    return 0;
}
