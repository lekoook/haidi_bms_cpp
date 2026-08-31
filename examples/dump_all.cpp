// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

// Reads every value the HAIDI protocol V4.0 defines out of a real BMS over a
// serial port and prints each one decoded.
//
//     ./haidi_dump_all -d /dev/ttyUSB0             one full sweep, then exit
//     ./haidi_dump_all -d /dev/ttyUSB0 -n 0        sweep until interrupted
//     ./haidi_dump_all -d /dev/ttyUSB0 -v          also trace every frame in hex
//     ./haidi_dump_all -h                          every option
//
// Where examples/poll_loop.cpp polls the handful of telemetry commands a
// monitoring application actually needs, this walks all 66 read commands:
// identification, live telemetry, every per-cell voltage and temperature,
// configuration, all 27 alarm thresholds, and the stored fault-record stream.
// That makes it a bring-up tool -- point it at an unfamiliar pack and see what
// the firmware really answers, and with what.
//
// It only ever reads. The two MOSFET control writes are deliberately absent, so
// running this against a live pack switches nothing.
//
// One command is on the wire at a time and the next is issued only once the
// session goes idle. That costs nothing -- protocol section 5.3 allows just one
// outstanding transaction anyway -- and it keeps every printed line in
// schedule order, which is what makes the output diffable between packs.

#include <haidi_bms.hpp>

#include <cctype>
#include <cerrno>
#include <csignal>
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

// -----------------------------------------------------------------------------
// Options
// -----------------------------------------------------------------------------

struct Options {
    const char* device = "/dev/ttyUSB0";
    unsigned baud = 9600;
    unsigned host_address = 0x40; // HostAddress::COMP.
    unsigned response_timeout_ms = HaidiBMS::DEFAULT_RESPONSE_TIMEOUT_MS;
    unsigned interframe_timeout_ms = HaidiBMS::DEFAULT_INTERFRAME_TIMEOUT_MS;
    unsigned retries = HaidiBMS::DEFAULT_MAX_RETRIES;
    unsigned cell_count = 0; // 0 = size from the 0x94 / 0x51 reply.
    unsigned temp_count = 0;
    unsigned sweeps = 1; // 0 = until interrupted.
    unsigned interval_ms = 1000;
    bool skip_alarm_thresholds = false;
    bool skip_fault_records = false;
    bool trace = false;
};

void usage(const char* argv0) {
    std::printf("Usage: %s [options]\n"
                "\n"
                "Reads every documented HAIDI BMS value over a serial port and prints it.\n"
                "Reads only -- no MOSFET or configuration write is ever sent.\n"
                "\n"
                "  -d <device>  serial device                     (default /dev/ttyUSB0)\n"
                "  -b <baud>    baud rate                         (default 9600)\n"
                "  -a <hex>     host address, 80 BLE / 20 GPRS / 40 COMP  (default 40)\n"
                "  -t <ms>      response timeout                  (default %u)\n"
                "  -f <ms>      inter-frame timeout               (default %u)\n"
                "  -r <n>       retransmissions per request       (default %u)\n"
                "  -c <n>       cell count for the 0x95 read      (default: from 0x94/0x51)\n"
                "  -T <n>       sensor count for the 0x96 read    (default: from 0x94/0x51)\n"
                "  -n <n>       sweeps to run, 0 = forever        (default 1)\n"
                "  -i <ms>      pause between sweeps              (default 1000)\n"
                "  -A           skip the 27 alarm thresholds\n"
                "  -F           skip the stored fault records\n"
                "  -v           trace every frame sent and received, in hex\n"
                "  -h           this help\n",
                argv0, HaidiBMS::DEFAULT_RESPONSE_TIMEOUT_MS,
                HaidiBMS::DEFAULT_INTERFRAME_TIMEOUT_MS, HaidiBMS::DEFAULT_MAX_RETRIES);
}

// Returns false when a flag was malformed, so main() can exit non-zero rather
// than silently dumping with a default the caller did not ask for.
bool parse_options(int argc, char** argv, Options& opt, bool& want_help) {
    int c = 0;
    while ((c = ::getopt(argc, argv, "d:b:a:t:f:r:c:T:n:i:AFvh")) != -1) {
        switch (c) {
            case 'd':
                opt.device = optarg;
                break;
            case 'b':
                opt.baud = static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'a':
                opt.host_address = static_cast<unsigned>(std::strtoul(optarg, nullptr, 16));
                break;
            case 't':
                opt.response_timeout_ms = static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'f':
                opt.interframe_timeout_ms =
                    static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'r':
                opt.retries = static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'c':
                opt.cell_count = static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'T':
                opt.temp_count = static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'n':
                opt.sweeps = static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'i':
                opt.interval_ms = static_cast<unsigned>(std::strtoul(optarg, nullptr, 10));
                break;
            case 'A':
                opt.skip_alarm_thresholds = true;
                break;
            case 'F':
                opt.skip_fault_records = true;
                break;
            case 'v':
                opt.trace = true;
                break;
            case 'h':
                want_help = true;
                return true;
            default:
                return false;
        }
    }

    if (opt.retries > 255 || opt.cell_count > MAX_CELLS || opt.temp_count > MAX_TEMPERATURES) {
        std::fprintf(stderr, "-r must be <= 255, -c <= %zu, -T <= %zu\n", MAX_CELLS,
                     MAX_TEMPERATURES);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Serial port and clock
// -----------------------------------------------------------------------------

// 0 is not a valid line speed, so it doubles as "unsupported".
speed_t baud_constant(unsigned baud) {
    switch (baud) {
        case 1200:
            return B1200;
        case 2400:
            return B2400;
        case 4800:
            return B4800;
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        default:
            return 0;
    }
}

// Section 4.1: 9600 bps, 9600/N/8/1, no flow control.
int open_serial(const char* device, unsigned baud) {
    const speed_t speed = baud_constant(baud);
    if (speed == 0) {
        std::fprintf(stderr, "unsupported baud rate %u\n", baud);
        return -1;
    }

    const int fd = ::open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "open(%s): %s\n", device, std::strerror(errno));
        return -1;
    }

    termios tty{};
    if (::tcgetattr(fd, &tty) != 0) {
        std::fprintf(stderr, "tcgetattr(%s): %s\n", device, std::strerror(errno));
        ::close(fd);
        return -1;
    }

    ::cfmakeraw(&tty);
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);
    tty.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= static_cast<tcflag_t>(CS8 | CREAD | CLOCAL);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::fprintf(stderr, "tcsetattr(%s): %s\n", device, std::strerror(errno));
        ::close(fd);
        return -1;
    }

    // Anything the port buffered before we opened it belongs to nobody's
    // transaction, and would only be counted as junk by the framer.
    ::tcflush(fd, TCIOFLUSH);
    return fd;
}

uint32_t monotonic_ms() {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>((static_cast<uint64_t>(ts.tv_sec) * 1000U) +
                                 (static_cast<uint64_t>(ts.tv_nsec) / 1000000U));
}

// Set by -v. Frames are traced where they cross the serial port, so what is
// printed is the actual byte stream, not a re-encoding of it.
bool g_trace = false;

// True while a probe's label has been printed but its line not yet ended. A
// trace line has to break that line before it can print, and whatever finishes
// the probe then has to re-open a column of its own.
bool g_line_open = false;

void end_line() {
    if (g_line_open) {
        std::putchar('\n');
        g_line_open = false;
    }
}

void trace_bytes(const char* direction, const uint8_t* data, size_t len);

void on_tx(void* ctx, const uint8_t* data, size_t len) {
    const int fd = *static_cast<int*>(ctx);
    trace_bytes("->", data, len);
    if (::write(fd, data, len) < 0) {
        std::fprintf(stderr, "write: %s\n", std::strerror(errno));
    }
}

volatile sig_atomic_t g_stop = 0;

void on_signal(int /*sig*/) { g_stop = 1; }

// -----------------------------------------------------------------------------
// Formatting helpers
// -----------------------------------------------------------------------------

// Every line opens with the command's Data ID, so a reply can be traced back to
// the byte actually put on the wire -- which the 27 alarm thresholds especially
// need, since they collapse onto one to_string() name. LABEL_W is the name
// column; CONT is the whole prefix as spaces, for lining a continuation line up
// underneath the first line's value.
constexpr int LABEL_W = 27;
constexpr const char* CONT = "                                     ";
constexpr const char* BANNER =
    "================================================================================";

void field(DataId id, const char* label) {
    std::printf("  0x%02X  %-*s  ", static_cast<unsigned>(id), LABEL_W, label);
    g_line_open = true;
}

// Re-opens the ID column for a line that is not the first of its probe.
void continuation(DataId id) {
    if (!g_line_open) {
        std::printf("  0x%02X  %-*s  ", static_cast<unsigned>(id), LABEL_W, "");
    }
    g_line_open = false;
}

void trace_bytes(const char* direction, const uint8_t* data, size_t len) {
    if (!g_trace) {
        return;
    }
    end_line();
    std::printf("%s%s", CONT, direction);
    for (size_t i = 0; i < len; ++i) {
        std::printf(" %02X", data[i]);
    }
    std::putchar('\n');
}

// A section heading padded out to a fixed width with dashes.
void rule(const char* title) {
    constexpr int RULE_W = 74;
    static const char* const DASHES =
        "--------------------------------------------------------------------------";
    const int used = static_cast<int>(std::strlen(title));
    std::printf("\n-- %s %.*s\n", title, used < RULE_W ? RULE_W - used : 0, DASHES);
}

// Firmware pads its fixed-width text fields with NUL, space or 0xFF, none of
// which belong in the printed value.
size_t trimmed_len(const char* text, size_t len) {
    while (len > 0) {
        const unsigned char c = static_cast<unsigned char>(text[len - 1]);
        if (c != 0 && c != 0xFF && c != ' ') {
            break;
        }
        --len;
    }
    return len;
}

void print_ascii(const char* text, size_t len) {
    const size_t n = trimmed_len(text, len);
    if (n == 0) {
        std::printf("(empty)");
        return;
    }
    std::putchar('"');
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::putchar(std::isprint(c) != 0 ? static_cast<int>(c) : '.');
    }
    std::putchar('"');
}

void print_hex(const DataBytes& data) {
    for (size_t i = 0; i < data.size(); ++i) {
        std::printf("%s%02X", i == 0 ? "" : " ", data[i]);
    }
}

const char* on_off(bool on) { return on ? "on" : "off"; }

const char* yes_no(bool yes) { return yes ? "yes" : "no"; }

const char* battery_type_name(uint8_t raw) {
    switch (static_cast<BatteryType>(raw)) {
        case BatteryType::LFP:
            return "LFP";
        case BatteryType::TERNARY:
            return "ternary";
        case BatteryType::LTO:
            return "LTO";
    }
    return "reserved";
}

const char* charge_state_name(uint8_t raw) {
    switch (static_cast<ChargeDischargeState>(raw)) {
        case ChargeDischargeState::IDLE:
            return "idle";
        case ChargeDischargeState::CHARGING:
            return "charging";
        case ChargeDischargeState::DISCHARGING:
            return "discharging";
    }
    return "reserved";
}

const char* key_control_name(uint8_t raw) {
    switch (static_cast<KeyControlMos>(raw)) {
        case KeyControlMos::NONE:
            return "controls nothing";
        case KeyControlMos::DISCHARGE_AND_SLEEP:
            return "discharge MOS and sleep";
        case KeyControlMos::DISCHARGE_NOT_SLEEP:
            return "discharge MOS, not sleep";
    }
    return "reserved";
}

const char* inverter_type_name(uint8_t raw) {
    switch (static_cast<InverterType>(raw)) {
        case InverterType::NONE:
            return "none";
        case InverterType::PYLON:
            return "Pylontech";
        case InverterType::GROWAT:
            return "Growatt";
        case InverterType::SOFAR:
            return "Sofar";
        case InverterType::VOLTRONICPOWER:
            return "Voltronic Power";
        case InverterType::GOODWE:
            return "GoodWe";
        case InverterType::SRNE:
            return "SRNE";
        case InverterType::MUST:
            return "MUST";
        case InverterType::VICTRONENERGY:
            return "Victron Energy";
    }
    return "reserved";
}

const char* inverter_bus_name(uint8_t raw) {
    switch (static_cast<InverterCommType>(raw)) {
        case InverterCommType::RS485:
            return "RS485";
        case InverterCommType::CAN:
            return "CAN";
    }
    return "reserved";
}

const char* fault_record_name(uint8_t raw) {
    switch (static_cast<FaultRecordId>(raw)) {
        case FaultRecordId::START_CHARGING:
            return "charging started";
        case FaultRecordId::END_CHARGING:
            return "charging ended";
        case FaultRecordId::CELL_OVERVOLTAGE:
            return "cell overvoltage";
        case FaultRecordId::CELL_UNDERVOLTAGE:
            return "cell undervoltage";
        case FaultRecordId::TOTAL_OVERVOLTAGE:
            return "pack overvoltage";
        case FaultRecordId::TOTAL_UNDERVOLTAGE:
            return "pack undervoltage";
        case FaultRecordId::OVERTEMPERATURE:
            return "overtemperature";
        case FaultRecordId::UNDERTEMPERATURE:
            return "undertemperature";
        case FaultRecordId::OVERCURRENT:
            return "overcurrent";
    }
    return "reserved";
}

const char* alarm_class_name(AlarmClass cls) {
    switch (cls) {
        case AlarmClass::CELL_OVERVOLTAGE:
            return "cell overvoltage";
        case AlarmClass::CELL_UNDERVOLTAGE:
            return "cell undervoltage";
        case AlarmClass::CURRENT:
            return "overcurrent";
        case AlarmClass::HIGH_TEMPERATURE:
            return "high temperature";
        case AlarmClass::LOW_TEMPERATURE:
            return "low temperature";
        case AlarmClass::TOTAL_OVERVOLTAGE:
            return "pack overvoltage";
        case AlarmClass::TOTAL_UNDERVOLTAGE:
            return "pack undervoltage";
        case AlarmClass::VOLTAGE_DIFFERENCE:
            return "voltage difference";
        case AlarmClass::TEMPERATURE_DIFFERENCE:
            return "temp difference";
    }
    return "?";
}

// Section 6, Data ID 0x98: seven bytes of alarm and fault bits. A null entry is
// a bit the document marks reserved, which is reported by position instead.
const char* const FAULT_BITS[7][8] = {
    {"cell overvoltage L1", "cell overvoltage L2", "cell undervoltage L1", "cell undervoltage L2",
     "pack overvoltage L1", "pack overvoltage L2", "pack undervoltage L1", "pack undervoltage L2"},
    {"charge overtemperature L1", "charge overtemperature L2", "charge undertemperature L1",
     "charge undertemperature L2", "discharge overtemperature L1", "discharge overtemperature L2",
     "discharge undertemperature L1", "discharge undertemperature L2"},
    {"charge overcurrent L1", "charge overcurrent L2", "discharge overcurrent L1",
     "discharge overcurrent L2", "SOC high L1", "SOC high L2", "SOC low L1", "SOC low L2"},
    {"voltage difference L1", "voltage difference L2", "temperature difference L1",
     "temperature difference L2", nullptr, nullptr, nullptr, nullptr},
    {"charge MOS overtemperature", "discharge MOS overtemperature", "charge MOS sensor fault",
     "discharge MOS sensor fault", "charge MOS adhesion fault", "discharge MOS adhesion fault",
     "charge MOS open circuit", "discharge MOS open circuit"},
    {"AFE acquisition chip fault", "cell acquisition dropout", "cell temperature sensor fault",
     "EEPROM storage fault", "RTC clock fault", "pre-charge failure", "vehicle comms fault",
     "internal network module fault"},
    {"current module fault", "internal total voltage detection fault", "short circuit protection",
     "low voltage disables charging", "GPS or soft switch disconnected MOS", nullptr, nullptr,
     nullptr},
};

// -----------------------------------------------------------------------------
// Payload printers
// -----------------------------------------------------------------------------

void print_cell_voltages(const CellVoltages& p) {
    if (p.count == 0) {
        std::printf("no cells reported\n");
        return;
    }

    uint32_t sum = 0;
    uint16_t lo = p.mv[0];
    uint16_t hi = p.mv[0];
    uint8_t lo_at = 1;
    uint8_t hi_at = 1;
    for (uint8_t i = 0; i < p.count; ++i) {
        sum += p.mv[i];
        if (p.mv[i] < lo) {
            lo = p.mv[i];
            lo_at = static_cast<uint8_t>(i + 1);
        }
        if (p.mv[i] > hi) {
            hi = p.mv[i];
            hi_at = static_cast<uint8_t>(i + 1);
        }
    }

    std::printf("%u cells, min %u mV (#%u), max %u mV (#%u), spread %u mV, mean %u mV\n", p.count,
                lo, lo_at, hi, hi_at, static_cast<unsigned>(hi - lo),
                static_cast<unsigned>(sum / p.count));
    for (uint8_t i = 0; i < p.count; ++i) {
        if (i % 6 == 0) {
            std::printf("%s", CONT);
        }
        std::printf("#%-2u %5u mV%s", i + 1, p.mv[i],
                    (i % 6 == 5 || i + 1 == p.count) ? "\n" : "  ");
    }
}

void print_cell_temperatures(const CellTemperatures& p) {
    if (p.count == 0) {
        std::printf("no sensors reported\n");
        return;
    }

    int16_t lo = p.celsius[0];
    int16_t hi = p.celsius[0];
    for (uint8_t i = 0; i < p.count; ++i) {
        lo = p.celsius[i] < lo ? p.celsius[i] : lo;
        hi = p.celsius[i] > hi ? p.celsius[i] : hi;
    }

    std::printf("%u sensors, min %d C, max %d C\n", p.count, lo, hi);
    for (uint8_t i = 0; i < p.count; ++i) {
        if (i % 6 == 0) {
            std::printf("%s", CONT);
        }
        std::printf("#%-2u %4d C%s", i + 1, p.celsius[i],
                    (i % 6 == 5 || i + 1 == p.count) ? "\n" : "   ");
    }
}

void print_fault_status(const FaultStatus& p) {
    unsigned active = 0;
    for (size_t byte = 0; byte < 7; ++byte) {
        for (size_t bit = 0; bit < 8; ++bit) {
            active += p.fault(byte, bit) ? 1 : 0;
        }
    }

    if (active == 0 && p.fault_code() == 0) {
        std::printf("none  (raw ");
        print_hex(p.bits);
        std::printf(")\n");
        return;
    }

    std::printf("%u active  (raw ", active);
    print_hex(p.bits);
    std::printf(")\n");
    for (size_t byte = 0; byte < 7; ++byte) {
        for (size_t bit = 0; bit < 8; ++bit) {
            if (!p.fault(byte, bit)) {
                continue;
            }
            const char* name = FAULT_BITS[byte][bit];
            if (name != nullptr) {
                std::printf("%s! %s\n", CONT, name);
            } else {
                std::printf("%s! byte%zu bit%zu (reserved by the document)\n", CONT, byte, bit);
            }
        }
    }
    if (p.fault_code() != 0) {
        std::printf("%s! fault code %u\n", CONT, p.fault_code());
    }
}

void print_balancing_bits(const CellBalancingBits& p) {
    unsigned count = 0;
    for (size_t cell = 0; cell < MAX_CELLS; ++cell) {
        count += p.cell_balancing(cell) ? 1 : 0;
    }

    if (count == 0) {
        std::printf("no cell balancing  (raw ");
        print_hex(p.bits);
        std::printf(")\n");
        return;
    }

    std::printf("%u balancing:", count);
    for (size_t cell = 0; cell < MAX_CELLS; ++cell) {
        if (p.cell_balancing(cell)) {
            std::printf(" #%zu", cell + 1);
        }
    }
    std::printf("  (raw ");
    print_hex(p.bits);
    std::printf(")\n");
}

void print_fault_record(const FaultRecord& p, unsigned ordinal) {
    std::printf("record %u: %04u-%02u-%02u %02u:%02u:%02u  %s (%s)\n", ordinal, p.year, p.month,
                p.day, p.hour, p.minute, p.second, fault_record_name(p.record_id),
                p.occurred ? "occurred" : "cleared");
    std::printf("%s  pack %.1f V, %.1f A, SOC %.1f %%, chg MOS %s, dsg MOS %s\n", CONT,
                p.total_voltage_dv / 10.0, p.current_da / 10.0, p.soc_pm / 10.0,
                on_off(p.charge_mos_on), on_off(p.discharge_mos_on));
    std::printf("%s  cells %u mV (#%u) .. %u mV (#%u), temps %d C (#%u) .. %d C (#%u)\n", CONT,
                p.lowest_cell_mv, p.lowest_cell_number, p.highest_cell_mv, p.highest_cell_number,
                p.lowest_temp_c, p.lowest_temp_number, p.highest_temp_c, p.highest_temp_number);
    std::printf("%s  fault code %u, record checksum %s\n", CONT, p.fault_code,
                p.checksum_ok ? "ok" : "MISMATCH");
}

// The alarm value's unit follows the class; see AlarmThreshold's documentation.
void print_alarm_value(AlarmClass cls, uint16_t value) {
    switch (cls) {
        case AlarmClass::TOTAL_OVERVOLTAGE:
        case AlarmClass::TOTAL_UNDERVOLTAGE:
            std::printf("%.1f V", value / 10.0);
            break;
        case AlarmClass::TEMPERATURE_DIFFERENCE:
            std::printf("%u C", value);
            break;
        default:
            std::printf("%u mV", value);
            break;
    }
}

// Returns false when the event carried a payload this function does not know
// how to print, which can only mean a command was added without a printer.
bool print_payload(const Event& event, unsigned ordinal) {
    if (const CapacityVoltage* p = as_capacity_voltage(event)) {
        std::printf("%u mAh (%.1f Ah), %u mV (%.2f V)\n", p->capacity_mah, p->capacity_mah / 1000.0,
                    p->voltage_mv, p->voltage_mv / 1000.0);
    } else if (const BmuCellTempCount* p = as_bmu_cell_temp_count(event)) {
        std::printf("%u board(s), cells %u/%u/%u, sensors %u/%u/%u\n", p->board_count,
                    p->cell_count[0], p->cell_count[1], p->cell_count[2], p->temp_count[0],
                    p->temp_count[1], p->temp_count[2]);
    } else if (const TotalChargeDischargeAh* p = as_total_charge_discharge_ah(event)) {
        std::printf("charged %u Ah, discharged %u Ah\n", p->charge_ah, p->discharge_ah);
    } else if (const BatteryOperationMode* p = as_battery_operation_mode(event)) {
        std::printf("%s, %s power key, built %04u-%02u-%02u, auto sleep %u s, zero drift %.1f A\n",
                    battery_type_name(p->battery_type),
                    p->operation_mode == 0x02 ? "short-press" : "long-press", p->year, p->month,
                    p->day, p->auto_sleep_s, p->zero_drift_current_da / 10.0);
    } else if (const FirmwareIndex* p = as_firmware_index(event)) {
        print_ascii(p->text.data(), p->text.size());
        std::printf("\n");
    } else if (const TextPayload* p = as_text(event)) {
        print_ascii(p->text.data(), p->len);
        std::printf("  (%u bytes)\n", p->len);
    } else if (const VersionPayload* p = as_version(event)) {
        print_ascii(p->text.data(), p->len);
        std::printf("  (%u bytes)\n", p->len);
    } else if (const RawPayload* p = as_raw(event)) {
        // 0x58's reply layout is undefined by the document, so the bytes are
        // all there is to show.
        print_hex(p->data);
        std::printf("  (undecoded: the document defines no layout)\n");
    } else if (const CellVoltageAlarm* p = as_cell_voltage_alarm(event)) {
        std::printf("over %u / %u mV, under %u / %u mV  (L1 / L2)\n", p->overvoltage_l1_mv,
                    p->overvoltage_l2_mv, p->undervoltage_l1_mv, p->undervoltage_l2_mv);
    } else if (const TotalVoltageAlarm* p = as_total_voltage_alarm(event)) {
        std::printf("over %.1f / %.1f V, under %.1f / %.1f V  (L1 / L2)\n",
                    p->overvoltage_l1_dv / 10.0, p->overvoltage_l2_dv / 10.0,
                    p->undervoltage_l1_dv / 10.0, p->undervoltage_l2_dv / 10.0);
    } else if (const CurrentAlarm* p = as_current_alarm(event)) {
        std::printf("charge %.1f / %.1f A, discharge %.1f / %.1f A  (L1 / L2)\n",
                    p->charge_overcurrent_l1_da / 10.0, p->charge_overcurrent_l2_da / 10.0,
                    p->discharge_overcurrent_l1_da / 10.0, p->discharge_overcurrent_l2_da / 10.0);
    } else if (const TemperatureAlarm* p = as_temperature_alarm(event)) {
        std::printf("charge   over %d / %d C, under %d / %d C  (L1 / L2)\n", p->charge_over_l1_c,
                    p->charge_over_l2_c, p->charge_under_l1_c, p->charge_under_l2_c);
        std::printf("%sdischarge over %d / %d C, under %d / %d C  (L1 / L2)\n", CONT,
                    p->discharge_over_l1_c, p->discharge_over_l2_c, p->discharge_under_l1_c,
                    p->discharge_under_l2_c);
    } else if (const SocAlarm* p = as_soc_alarm(event)) {
        std::printf("high %.1f / %.1f %%, low %.1f / %.1f %%  (L1 / L2)\n", p->over_l1_pm / 10.0,
                    p->over_l2_pm / 10.0, p->under_l1_pm / 10.0, p->under_l2_pm / 10.0);
    } else if (const DifferenceAlarm* p = as_difference_alarm(event)) {
        std::printf("voltage %u / %u mV, temperature %u / %u C  (L1 / L2)\n", p->voltage_diff_l1_mv,
                    p->voltage_diff_l2_mv, p->temp_diff_l1_c, p->temp_diff_l2_c);
    } else if (const BalancingParams* p = as_balancing_params(event)) {
        std::printf("start above %u mV, start on %u mV spread\n", p->start_voltage_mv,
                    p->start_difference_mv);
    } else if (const CurrentParams* p = as_current_params(event)) {
        std::printf("alarm current %u A, shunt %u uOhm\n", p->alarm_current_a,
                    p->sense_resistor_uohm);
    } else if (const Rtc* p = as_rtc(event)) {
        std::printf("%04u-%02u-%02u %02u:%02u:%02u\n", p->year, p->month, p->day, p->hour,
                    p->minute, p->second);
    } else if (const FaultRecord* p = as_fault_record(event)) {
        print_fault_record(*p, ordinal);
    } else if (const BoardNumber* p = as_board_number(event)) {
        std::printf("board %u of %u BMU(s)\n", p->board_number, p->bmu_count);
    } else if (const HeatingTemperature* p = as_heating_temperature(event)) {
        std::printf("heater below %d C, fan above %d C, key %s\n", p->heating_start_c,
                    p->fan_start_c, key_control_name(p->key_control_mos));
    } else if (const ActiveBalancingSwitch* p = as_active_balancing_switch(event)) {
        std::printf("%s\n", on_off(p->enabled));
    } else if (const ActiveBalancingParams* p = as_active_balancing_params(event)) {
        std::printf("%u cells in series, %.1f A balancing current\n", p->series_count,
                    p->balancing_current_da / 10.0);
    } else if (const InverterParams* p = as_inverter_params(event)) {
        std::printf("%s over %s\n", inverter_type_name(p->inverter_type),
                    inverter_bus_name(p->comm_type));
    } else if (const CurrentAlarmThreshold* p = as_current_alarm_threshold(event)) {
        std::printf("charge %.1f A after %.2f s, discharge %.1f A after %.2f s\n",
                    p->charge_overcurrent_da / 10.0, p->charge_delay_cs / 100.0,
                    p->discharge_overcurrent_da / 10.0, p->discharge_delay_cs / 100.0);
    } else if (const TempAlarmThreshold* p = as_temp_alarm_threshold(event)) {
        std::printf("charge    trip %d C after %.1f s, recover %d C after %.1f s\n",
                    p->charge_alarm_c, p->charge_delay_ds / 10.0, p->charge_recovery_c,
                    p->charge_recovery_delay_ds / 10.0);
        std::printf("%sdischarge trip %d C after %.1f s, recover %d C after %.1f s\n", CONT,
                    p->discharge_alarm_c, p->discharge_delay_ds / 10.0, p->discharge_recovery_c,
                    p->discharge_recovery_delay_ds / 10.0);
    } else if (const AlarmThreshold* p = as_alarm_threshold(event)) {
        const AlarmClass cls = alarm_class_of(event.id);
        std::printf("trip ");
        print_alarm_value(cls, p->value);
        std::printf(" after %.2f s, recover ", p->delay_cs / 100.0);
        print_alarm_value(cls, p->recovery_value);
        std::printf(" after %.2f s\n", p->recovery_delay_cs / 100.0);
    } else if (const TotalVoltageCurrentSoc* p = as_total_voltage_current_soc(event)) {
        std::printf("%.1f V cumulative, %.1f V measured, %.1f A, SOC %.1f %%\n",
                    p->cumulative_voltage_dv / 10.0, p->measured_voltage_dv / 10.0,
                    p->current_da / 10.0, p->soc_pm / 10.0);
    } else if (const CellVoltageExtremes* p = as_cell_voltage_extremes(event)) {
        std::printf("high %u mV (#%u), low %u mV (#%u), spread %d mV\n", p->highest_mv,
                    p->highest_cell_number, p->lowest_mv, p->lowest_cell_number,
                    static_cast<int>(p->highest_mv) - static_cast<int>(p->lowest_mv));
    } else if (const CellTemperatureExtremes* p = as_cell_temperature_extremes(event)) {
        std::printf("high %d C (#%u), low %d C (#%u)\n", p->highest_c, p->highest_cell_number,
                    p->lowest_c, p->lowest_cell_number);
    } else if (const ChargeDischargeMosStatus* p = as_charge_discharge_mos(event)) {
        std::printf("%s, chg MOS %s, dsg MOS %s, %u mAh remaining, life %u\n",
                    charge_state_name(p->state), on_off(p->charge_mos_state != 0),
                    on_off(p->discharge_mos_state != 0), p->remaining_capacity_mah, p->bms_life);
    } else if (const StatusInfo* p = as_status_info(event)) {
        std::printf("%u cells, %u sensors, charger %s, load %s, %u cycles, board %d C\n",
                    p->series_count, p->temp_sensor_count, yes_no(p->charger_connected),
                    yes_no(p->load_connected), p->cycle_count, p->onboard_temp_c);
        std::printf("%sDI1-4 %u%u%u%u, DO1-4 %u%u%u%u\n", CONT, (p->io_bits >> 0U) & 1U,
                    (p->io_bits >> 1U) & 1U, (p->io_bits >> 2U) & 1U, (p->io_bits >> 3U) & 1U,
                    (p->io_bits >> 4U) & 1U, (p->io_bits >> 5U) & 1U, (p->io_bits >> 6U) & 1U,
                    (p->io_bits >> 7U) & 1U);
    } else if (const CellVoltages* p = as_cell_voltages(event)) {
        print_cell_voltages(*p);
    } else if (const CellTemperatures* p = as_cell_temperatures(event)) {
        print_cell_temperatures(*p);
    } else if (const CellBalancingBits* p = as_cell_balancing_bits(event)) {
        print_balancing_bits(*p);
    } else if (const FaultStatus* p = as_fault_status(event)) {
        print_fault_status(*p);
    } else if (const BalancingState* p = as_balancing_state(event)) {
        std::printf("%s, %.1f A, position %u\n", on_off(p->enabled), p->current_da / 10.0,
                    p->position);
    } else if (const BatteryStatus* p = as_battery_status(event)) {
        std::printf("SOH %.1f %%\n", p->soh_pm / 10.0);
    } else if (const WakeupSource* p = as_wakeup_source(event)) {
        std::printf("key %s, button %s, 485 %s, CAN %s, charge/discharge current %s\n",
                    yes_no(p->key_signal), yes_no(p->button_signal), yes_no(p->rs485_signal),
                    yes_no(p->can_signal), yes_no(p->charge_discharge_current));
    } else if (const MosControlAck* p = as_mos_control_ack(event)) {
        // Unreachable from this program, which never writes -- but keeping the
        // chain total means it stays a complete printer for any Event.
        std::printf("requested %s, device reports %s\n", on_off(p->requested_on),
                    on_off(p->reported_on));
    } else {
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// The schedule
// -----------------------------------------------------------------------------

struct Probe {
    const char* section; // Non-null on the first probe of a section.
    DataId id;
};

// Room to spare over the 66 a full sweep schedules: the 27 reads in 0x50..0x6A,
// 27 alarm thresholds, 11 telemetry reads and the wake-up source.
constexpr size_t MAX_PROBES = 96;

// 0x94 and 0x51 are what cache the cell and sensor counts that size the 0x95
// and 0x96 paginated reads, so both are scheduled ahead of them.
size_t build_schedule(Probe (&out)[MAX_PROBES], const Options& opt) {
    size_t n = 0;
    const char* section = nullptr;

    auto add = [&](DataId id) {
        out[n].section = section;
        out[n].id = id;
        section = nullptr;
        ++n;
    };

    section = "Identification";
    add(DataId::CAPACITY_VOLTAGE);
    add(DataId::BMU_CELL_TEMP_COUNT);
    add(DataId::BATTERY_OPERATION_MODE);
    add(DataId::FIRMWARE_INDEX);
    add(DataId::MANUFACTURER_NAME);
    add(DataId::BATTERY_NAME);
    add(DataId::BATTERY_SERIAL_NUMBER);
    add(DataId::SN_SERIAL_NUMBER);
    add(DataId::BATTERY_PRODUCTION_DATE);
    add(DataId::SOFTWARE_VERSION);
    add(DataId::HARDWARE_VERSION);
    add(DataId::BOARD_NUMBER);

    section = "Live telemetry";
    add(DataId::TOTAL_VOLTAGE_CURRENT_SOC);
    add(DataId::CHARGE_DISCHARGE_MOS_STATUS);
    add(DataId::STATUS_INFO);
    add(DataId::BATTERY_STATUS);
    add(DataId::TOTAL_CHARGE_DISCHARGE_AH);
    add(DataId::CELL_VOLTAGE_EXTREMES);
    add(DataId::CELL_TEMPERATURE_EXTREMES);
    add(DataId::CELL_VOLTAGES);
    add(DataId::CELL_TEMPERATURES);
    add(DataId::BALANCING_STATE);
    add(DataId::CELL_BALANCING_BITS);
    add(DataId::FAULT_STATUS);
    add(DataId::WAKEUP_SOURCE);
    add(DataId::RTC);

    section = "Configuration";
    add(DataId::CELL_VOLTAGE_ALARM);
    add(DataId::TOTAL_VOLTAGE_ALARM);
    add(DataId::CURRENT_ALARM);
    add(DataId::TEMPERATURE_ALARM);
    add(DataId::SOC_ALARM);
    add(DataId::DIFFERENCE_ALARM);
    add(DataId::BALANCING_PARAMS);
    add(DataId::CURRENT_PARAMS);
    add(DataId::HEATING_TEMPERATURE);
    add(DataId::ACTIVE_BALANCING_SWITCH);
    add(DataId::ACTIVE_BALANCING_PARAMS);
    add(DataId::INVERTER_PARAMS);

    if (!opt.skip_alarm_thresholds) {
        section = "Alarm thresholds";
        for (uint8_t level = 0; level < ALARM_LEVEL_COUNT; ++level) {
            for (uint8_t cls = 0; cls < ALARM_CLASS_COUNT; ++cls) {
                add(alarm_threshold_id(static_cast<AlarmClass>(cls),
                                       static_cast<AlarmLevel>(level)));
            }
        }
    }

    if (!opt.skip_fault_records) {
        section = "Stored fault records";
        add(DataId::FAULT_RECORDS);
    }

    return n;
}

// The alarm block collapses onto one to_string() name, so the class and level
// are recovered from the Data ID to keep 27 lines apart.
void probe_label(DataId id, char* out, size_t cap) {
    if (is_alarm_threshold_id(id)) {
        std::snprintf(out, cap, "%s L%u", alarm_class_name(alarm_class_of(id)),
                      static_cast<unsigned>(alarm_level_of(id)) + 1);
    } else {
        std::snprintf(out, cap, "%s", to_string(id));
    }
}

QueueStatus issue(HaidiBMS& bms, DataId id, const Options& opt) {
    if (is_alarm_threshold_id(id)) {
        return bms.poll_alarm_threshold(alarm_class_of(id), alarm_level_of(id));
    }
    switch (id) {
        case DataId::CAPACITY_VOLTAGE:
            return bms.poll_capacity_voltage();
        case DataId::BMU_CELL_TEMP_COUNT:
            return bms.poll_bmu_cell_temp_count();
        case DataId::TOTAL_CHARGE_DISCHARGE_AH:
            return bms.poll_total_charge_discharge_ah();
        case DataId::BATTERY_OPERATION_MODE:
            return bms.poll_battery_operation_mode();
        case DataId::FIRMWARE_INDEX:
            return bms.poll_firmware_index();
        case DataId::MANUFACTURER_NAME:
            return bms.poll_manufacturer_name();
        case DataId::BATTERY_NAME:
            return bms.poll_battery_name();
        case DataId::BATTERY_SERIAL_NUMBER:
            return bms.poll_battery_serial_number();
        case DataId::BATTERY_PRODUCTION_DATE:
            return bms.poll_battery_production_date();
        case DataId::CELL_VOLTAGE_ALARM:
            return bms.poll_cell_voltage_alarm();
        case DataId::TOTAL_VOLTAGE_ALARM:
            return bms.poll_total_voltage_alarm();
        case DataId::CURRENT_ALARM:
            return bms.poll_current_alarm();
        case DataId::TEMPERATURE_ALARM:
            return bms.poll_temperature_alarm();
        case DataId::SOC_ALARM:
            return bms.poll_soc_alarm();
        case DataId::DIFFERENCE_ALARM:
            return bms.poll_difference_alarm();
        case DataId::BALANCING_PARAMS:
            return bms.poll_balancing_params();
        case DataId::CURRENT_PARAMS:
            return bms.poll_current_params();
        case DataId::RTC:
            return bms.poll_rtc();
        case DataId::SOFTWARE_VERSION:
            return bms.poll_software_version();
        case DataId::HARDWARE_VERSION:
            return bms.poll_hardware_version();
        case DataId::FAULT_RECORDS:
            return bms.poll_fault_records();
        case DataId::BOARD_NUMBER:
            return bms.poll_board_number();
        case DataId::HEATING_TEMPERATURE:
            return bms.poll_heating_temperature();
        case DataId::ACTIVE_BALANCING_SWITCH:
            return bms.poll_active_balancing_switch();
        case DataId::ACTIVE_BALANCING_PARAMS:
            return bms.poll_active_balancing_params();
        case DataId::INVERTER_PARAMS:
            return bms.poll_inverter_params();
        case DataId::SN_SERIAL_NUMBER:
            return bms.poll_sn_serial_number();
        case DataId::TOTAL_VOLTAGE_CURRENT_SOC:
            return bms.poll_total_voltage_current_soc();
        case DataId::CELL_VOLTAGE_EXTREMES:
            return bms.poll_cell_voltage_extremes();
        case DataId::CELL_TEMPERATURE_EXTREMES:
            return bms.poll_cell_temperature_extremes();
        case DataId::CHARGE_DISCHARGE_MOS_STATUS:
            return bms.poll_charge_discharge_mos_status();
        case DataId::STATUS_INFO:
            return bms.poll_status_info();
        case DataId::CELL_VOLTAGES:
            return bms.poll_cell_voltages(static_cast<uint8_t>(opt.cell_count));
        case DataId::CELL_TEMPERATURES:
            return bms.poll_cell_temperatures(static_cast<uint8_t>(opt.temp_count));
        case DataId::CELL_BALANCING_BITS:
            return bms.poll_cell_balancing_bits();
        case DataId::FAULT_STATUS:
            return bms.poll_fault_status();
        case DataId::BALANCING_STATE:
            return bms.poll_balancing_state();
        case DataId::BATTERY_STATUS:
            return bms.poll_battery_status();
        case DataId::WAKEUP_SOURCE:
            return bms.poll_wakeup_source();
        default:
            return QueueStatus::BAD_REQUEST;
    }
}

const char* queue_status_name(QueueStatus status) {
    switch (status) {
        case QueueStatus::QUEUED:
            return "QUEUED";
        case QueueStatus::QUEUE_FULL:
            return "QUEUE_FULL";
        case QueueStatus::NO_TX_HANDLER:
            return "NO_TX_HANDLER";
        case QueueStatus::BAD_REQUEST:
            return "BAD_REQUEST";
    }
    return "?";
}

// -----------------------------------------------------------------------------
// Running a sweep
// -----------------------------------------------------------------------------

struct Tally {
    unsigned responses = 0;
    unsigned timeouts = 0;
    unsigned errors = 0;
    unsigned undecoded = 0;
    unsigned this_probe = 0; // Events seen for the probe now running.
};

// One handler for every event. The probe's label has already been printed
// without a newline, so the first event of a probe completes that line and any
// further one starts its own. Further events come from a fault-record stream,
// or from a protocol error the request survived, which its real outcome follows.
void on_event(void* ctx, const Event& event) {
    auto& tally = *static_cast<Tally*>(ctx);
    continuation(event.id);
    ++tally.this_probe;

    switch (event.kind) {
        case EventKind::RESPONSE:
            ++tally.responses;
            if (!print_payload(event, tally.this_probe)) {
                ++tally.undecoded;
                std::printf("decoded, but this program has no printer for it\n");
            }
            break;
        case EventKind::TIMEOUT:
            // The protocol defines no "unsupported command" reply, so a command
            // this firmware does not implement is indistinguishable from a lost
            // one and lands here too.
            ++tally.timeouts;
            std::printf("-- no reply (%u attempt%s)\n", event.retries + 1U,
                        event.retries == 0 ? "" : "s");
            break;
        case EventKind::PROTOCOL_ERROR:
            ++tally.errors;
            std::printf("-- protocol error: %s\n", to_string(event.error));
            break;
    }
}

// Advances the session by one iteration. Returns false only on a fatal read
// error, which means the port went away.
bool pump(HaidiBMS& bms, int fd) {
    bms.tick(monotonic_ms());

    uint8_t buf[128];
    const ssize_t n = ::read(fd, buf, sizeof buf);
    if (n > 0) {
        trace_bytes("<-", buf, static_cast<size_t>(n));
        bms.feed(buf, static_cast<size_t>(n));
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        std::fprintf(stderr, "read: %s\n", std::strerror(errno));
        return false;
    }

    // 13 bytes at 9600 8N1 take 13.5 ms, so there is nothing to gain from
    // spinning faster than a couple of milliseconds.
    timespec idle{0, 2 * 1000 * 1000};
    ::nanosleep(&idle, nullptr);
    return true;
}

// A backstop only: tick() expires every real deadline, so this fires only if
// the session somehow never settles. Generous, because a long fault-record
// stream is legitimately slow.
constexpr uint32_t PROBE_GUARD_MS = 30000;

bool run_sweep(HaidiBMS& bms, int fd, const Probe* schedule, size_t count, const Options& opt,
               Tally& tally) {
    for (size_t i = 0; i < count && g_stop == 0; ++i) {
        if (schedule[i].section != nullptr) {
            rule(schedule[i].section);
        }

        char label[48];
        probe_label(schedule[i].id, label, sizeof label);
        field(schedule[i].id, label);
        std::fflush(stdout);

        tally.this_probe = 0;
        const QueueStatus status = issue(bms, schedule[i].id, opt);
        if (status != QueueStatus::QUEUED) {
            continuation(schedule[i].id);
            std::printf("-- not sent: %s\n", queue_status_name(status));
            continue;
        }

        const uint32_t guard = monotonic_ms() + PROBE_GUARD_MS;
        while (bms.busy() || bms.pending() > 0) {
            if (!pump(bms, fd)) {
                std::printf("\n");
                return false;
            }
            if (g_stop != 0) {
                break;
            }
            if (static_cast<int32_t>(monotonic_ms() - guard) >= 0) {
                continuation(schedule[i].id);
                std::printf("-- gave up after %u ms\n", PROBE_GUARD_MS);
                bms.reset();
                break;
            }
        }

        // The label was printed without a newline, so the line has to be
        // closed however the probe ended. A fault-record request whose first
        // frame is the 0xFF sentinel emits nothing at all, which is simply what
        // an empty log looks like.
        if (tally.this_probe == 0) {
            continuation(schedule[i].id);
            if (g_stop != 0) {
                std::printf("-- interrupted\n");
            } else if (schedule[i].id == DataId::FAULT_RECORDS) {
                std::printf("none stored\n");
            } else {
                std::printf("-- no event\n");
            }
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    bool want_help = false;
    if (!parse_options(argc, argv, opt, want_help)) {
        usage(argv[0]);
        return 2;
    }
    if (want_help) {
        usage(argv[0]);
        return 0;
    }

    g_trace = opt.trace;

    const int fd = open_serial(opt.device, opt.baud);
    if (fd < 0) {
        return 1;
    }

    // Leave the sweep to finish the probe it is on, then stop cleanly, so a
    // Ctrl-C still prints the summary.
    struct sigaction sa{};
    sa.sa_handler = &on_signal;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    int fd_ctx = fd;
    Tally tally;

    HaidiBMS bms{static_cast<HostAddress>(opt.host_address)};
    bms.set_tx_handler(&on_tx, &fd_ctx);
    bms.set_event_handler(&on_event, &tally);
    bms.set_response_timeout_ms(opt.response_timeout_ms);
    bms.set_interframe_timeout_ms(opt.interframe_timeout_ms);
    bms.set_max_retries(static_cast<uint8_t>(opt.retries));
    bms.tick(monotonic_ms());

    Probe schedule[MAX_PROBES];
    const size_t probes = build_schedule(schedule, opt);

    bool ok = true;
    for (unsigned sweep = 1; ok && g_stop == 0 && (opt.sweeps == 0 || sweep <= opt.sweeps);
         ++sweep) {
        std::printf("\n%s\n HAIDI BMS dump -- %s @ %u 8N1, host 0x%02X"
                    " -- sweep %u, %zu commands\n%s\n",
                    BANNER, opt.device, opt.baud, opt.host_address, sweep, probes, BANNER);

        ok = run_sweep(bms, fd, schedule, probes, opt, tally);

        if (!ok || g_stop != 0 || (opt.sweeps != 0 && sweep == opt.sweeps)) {
            continue;
        }
        for (uint32_t waited = 0; waited < opt.interval_ms && g_stop == 0; waited += 2) {
            if (!pump(bms, fd)) {
                break;
            }
        }
    }

    rule("Summary");
    std::printf("  %u replied, %u no reply, %u protocol errors, %u undecoded, %u junk bytes\n",
                tally.responses, tally.timeouts, tally.errors, tally.undecoded,
                bms.discarded_bytes());
    if (g_stop != 0) {
        std::printf("  interrupted\n");
    }

    ::close(fd);
    return ok ? 0 : 1;
}
