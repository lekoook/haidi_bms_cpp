# haidi_bms_cpp 🔋

An asynchronous, transport-agnostic C++17 driver for **HAIDI battery management systems**
using the RS485 / UART protocol **V4.0**. CAN is not supported yet
(see *Supported interfaces* below).

You provide the bytes and the clock. The library encodes requests, frames and decodes replies,
matches each reply to its request, retries on timeout, reassembles multi-frame replies, and
delivers typed results through callbacks. It never opens a port, reads a clock or starts a
thread, so the same code runs on a Linux host over `/dev/ttyUSB0`, on a microcontroller over a
UART peripheral, or against a byte array in a unit test.

## ✨ Features

- ⚡ **Asynchronous**: every `poll_*()` returns immediately, and the result arrives later as an `Event`.
- 🔌 **Transport-agnostic**: supply a transmit callback, feed in received bytes, and call `tick()`.
- 🧱 **Embedded-friendly**: no heap allocation, exceptions or RTTI, and fixed-size buffers throughout.
- 🎯 **Typed results**: checked `as_*()` accessors return `nullptr` if the event isn't the reply you asked for.
- 🧩 **Layered**: the frame codec, byte-stream framer and payload decoders can be used on their own.

## 🔗 Supported interfaces

| Interface | Status |
| --- | --- |
| RS485 | ✅ Supported |
| UART | ✅ Supported |
| CAN | 🚧 Not yet supported, planned for a future release |

The HAIDI protocol document also defines a CAN interface, but this library does not implement
it yet. For now, connect to the BMS over RS485 or UART.

## 📋 Supported commands

| Area | Examples |
| --- | --- |
| Live telemetry | pack voltage / current / SOC, cell & temperature extremes, MOSFET status, faults, SOH |
| Per-cell reads | every cell voltage and every temperature sensor |
| Identification | manufacturer, battery name, serial numbers, production date, firmware & hardware versions |
| Configuration | balancing, current and inverter parameters, RTC, heating, all 27 alarm thresholds |
| Control | charge and discharge MOSFET on/off |

The protocol specification is included at
[`docs/CAN 485 UART Protocol V4.0 HAIDI.pdf`](docs/).

## 🛠️ Requirements

- A C++17 compiler (GCC, Clang, MSVC, arm-none-eabi-g++, …)
- CMake 3.16 or newer, if you build with CMake

## 📦 Installation

The library target is `haidi_bms_cpp`. Linking it adds `include/` to your include path, so
`#include <haidi_bms.hpp>` works straight away.

### CMake `FetchContent` (recommended)

```cmake
include(FetchContent)

FetchContent_Declare(haidi_bms_cpp
    GIT_REPOSITORY https://github.com/lekoook/haidi_bms_cpp.git
    GIT_TAG        master   # or a release tag / commit SHA
)
FetchContent_MakeAvailable(haidi_bms_cpp)

target_link_libraries(my_app PRIVATE haidi_bms_cpp)
```

### CMake `add_subdirectory`

Add the repository to your project (as a submodule, subtree or plain copy), then:

```cmake
add_subdirectory(third_party/haidi_bms_cpp)
target_link_libraries(my_app PRIVATE haidi_bms_cpp)
```

When included either way, the library's tests and examples are not built.

### Without CMake

Add `src/*.cpp` to your build and `include/` to your include path. This works well with
IDE-based embedded toolchains.

> [!IMPORTANT]
> Compile your own code as **C++17** too. Linking `haidi_bms_cpp` doesn't set the standard for
> your targets, and code compiled as an older standard can fail to link.
>
> ```cmake
> target_compile_features(my_app PRIVATE cxx_std_17)
> ```

There is no `install()` support or `find_package()` config yet, so use one of the methods above.

## 🚀 Quick start

You connect a `haidi::HaidiBMS` to your transport with four calls:

| Call | What you do with it |
| --- | --- |
| `set_tx_handler()` | Register a callback that writes bytes to the wire |
| `set_event_handler()` | Register a callback that receives replies, timeouts and errors |
| `feed()` | Pass in bytes as you receive them |
| `tick()` | Call regularly with a monotonic millisecond count |

```cpp
#include <haidi_bms.hpp>

using namespace haidi;

// Writes an encoded frame to your transport.
void on_tx(void* ctx, const uint8_t* data, size_t len) {
    const int fd = *static_cast<int*>(ctx);
    ::write(fd, data, len);
}

// Receives every result.
void on_event(void* /*ctx*/, const Event& event) {
    if (event.kind != EventKind::RESPONSE) {
        std::printf("%s failed: %s\n", to_string(event.id), to_string(event.kind));
        return;
    }

    if (const TotalVoltageCurrentSoc* p = as_total_voltage_current_soc(event)) {
        std::printf("%.1f V  %.1f A  SOC %.1f %%\n",
                    p->cumulative_voltage_dv / 10.0,
                    p->current_da / 10.0,
                    p->soc_pm / 10.0);
    }
}

int main() {
    // open_serial() and monotonic_ms() are yours to provide.
    // Open the port at 9600 8N1, with no flow control and a non-blocking or short-timeout read.
    int fd = open_serial("/dev/ttyUSB0");

    HaidiBMS bms{HostAddress::COMP};
    bms.set_tx_handler(&on_tx, &fd);
    bms.set_event_handler(&on_event, nullptr);

    bms.poll_total_voltage_current_soc();   // returns immediately

    for (;;) {
        bms.tick(monotonic_ms());

        uint8_t buf[64];
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n > 0) {
            bms.feed(buf, static_cast<size_t>(n));
        }
    }
}
```

For a complete program, see [examples/poll_loop.cpp](examples/poll_loop.cpp).

## 📘 Usage guide

### Always call `tick()`

> [!WARNING]
> Call `tick()` regularly, for example every loop iteration or from a periodic timer.
> Timeouts and retries only happen inside `tick()`. Without it, a single lost reply stalls the
> session for good, and cell voltage or temperature reads with no known count never finish.
> Everything can seem fine on the bench and then fail in the field on the first dropped byte.

### Handling events

Every result arrives as an `Event`:

| `event.kind` | Meaning |
| --- | --- |
| `EventKind::RESPONSE` | A decoded reply. Read it with the matching `as_*()` accessor. |
| `EventKind::TIMEOUT` | No reply arrived, even after retries. |
| `EventKind::PROTOCOL_ERROR` | A frame was rejected or the exchange failed. See `event.error`. |

Every command has its own `as_*()` accessor, named after the command it reads. It returns a
pointer to the decoded payload, or `nullptr` if the event isn't a successful reply to that
command, so you can safely try one after another:

```cpp
if (const CellVoltages* v = as_cell_voltages(event)) {
    for (uint8_t i = 0; i < v->count; ++i) {
        std::printf("cell %u: %u mV\n", i + 1, v->mv[i]);
    }
}
```

Commands that share a payload type also have a generic accessor that accepts any of them:
`as_text()`, `as_version()`, `as_alarm_threshold()`, `as_current_alarm_threshold()`,
`as_temp_alarm_threshold()` and `as_mos_control_ack()`. These suit code that handles replies by
payload type, such as a logger. When you know which command you sent, use its own accessor.

Use `to_string()` on `EventKind`, `ErrorCode` or `DataId` to get a readable name for logging.

> [!NOTE]
> A single request can produce **more than one event**. A corrupted frame, or a frame from the
> wrong address or for the wrong command, is reported as a `PROTOCOL_ERROR` while the request
> keeps waiting, and the real result follows later. Don't treat every error as the end of a
> request. The documentation for each `ErrorCode` says whether it ends the request.

### Per-request handlers

Every `poll_*()` and `set_*_mos()` call accepts its own handler and context pointer. This is
useful when a result belongs somewhere specific, such as a one-off read at start-up:

```cpp
bms.poll_capacity_voltage(&on_capacity, &app);
```

The per-request handler runs first, then the session-wide handler. It receives every event for
that request, including timeouts and errors. The context pointer must stay valid until the
request completes. You can start new polls from inside a handler.

### Request queue

The protocol allows only one request in flight at a time. Requests made while the bus is busy
are queued and sent in order:

```cpp
bms.poll_cell_voltage_extremes();
bms.poll_status_info();
bms.poll_fault_status();      // all three are queued and sent one after another
```

Each call returns a `QueueStatus`:

| Value | Meaning |
| --- | --- |
| `QUEUED` | Accepted, and sent now or once the bus is free |
| `QUEUE_FULL` | Too many requests are pending (see *Queue size* under Configuration) |
| `NO_TX_HANDLER` | `set_tx_handler()` hasn't been called |
| `BAD_REQUEST` | Invalid arguments, e.g. an out-of-range alarm class |

To check on the queue, use `busy()`, `pending()` and `in_flight()`. Call `reset()` to drop the
request in flight and everything queued. No events are sent for requests it drops.

### Timeouts and retries

| Setter | Default | Controls |
| --- | --- | --- |
| `set_response_timeout_ms()` | 250 ms | How long to wait for the first frame of a reply |
| `set_interframe_timeout_ms()` | 250 ms | How long to wait between frames of a multi-frame reply |
| `set_max_retries()` | 1 | How many times to resend a request that gets no reply |

### Cell voltages and temperatures

`poll_cell_voltages()` and `poll_cell_temperatures()` return multi-frame replies, and the
protocol doesn't say how many cells or sensors a pack has. Give the library a count in one of
these ways:

- Pass it: `bms.poll_cell_voltages(16);`
- Or poll `poll_status_info()` or `poll_bmu_cell_temp_count()` first. The count from that reply
  is cached and used automatically, even if the requests are queued back to back.

With no count, the read ends when the inter-frame timeout expires, and the payload's `count`
tells you how many values arrived.

### Fault records

`poll_fault_records()` produces **one event per stored record** and no event after the last
one. The stream is over once `in_flight()` no longer returns `DataId::FAULT_RECORDS`. If the
stream stops early, you get `ErrorCode::TRUNCATED`.

### Alarm thresholds

One call covers all 27 threshold commands:

```cpp
bms.poll_alarm_threshold(AlarmClass::CELL_OVERVOLTAGE, AlarmLevel::LEVEL_2);
```

Read the result with the accessor for that class, passing the same level:

```cpp
if (const AlarmThreshold* t = as_cell_overvoltage_threshold(event, AlarmLevel::LEVEL_2)) {
    std::printf("trips at %u mV\n", t->value);
}
```

| Class | Accessor | Payload |
| --- | --- | --- |
| `CELL_OVERVOLTAGE` | `as_cell_overvoltage_threshold()` | `AlarmThreshold` |
| `CELL_UNDERVOLTAGE` | `as_cell_undervoltage_threshold()` | `AlarmThreshold` |
| `CURRENT` | `as_overcurrent_threshold()` | `CurrentAlarmThreshold` |
| `HIGH_TEMPERATURE` | `as_high_temperature_threshold()` | `TempAlarmThreshold` |
| `LOW_TEMPERATURE` | `as_low_temperature_threshold()` | `TempAlarmThreshold` |
| `TOTAL_OVERVOLTAGE` | `as_total_overvoltage_threshold()` | `AlarmThreshold` |
| `TOTAL_UNDERVOLTAGE` | `as_total_undervoltage_threshold()` | `AlarmThreshold` |
| `VOLTAGE_DIFFERENCE` | `as_voltage_difference_threshold()` | `AlarmThreshold` |
| `TEMPERATURE_DIFFERENCE` | `as_temperature_difference_threshold()` | `AlarmThreshold` |

### MOSFET control

```cpp
bms.set_discharge_mos(false);
bms.set_charge_mos(true);
```

The BMS replies with the state it applied. If that differs from what you requested, the event
is a `PROTOCOL_ERROR` with `ErrorCode::WRITE_REJECTED`. `as_discharge_mos_control()` and
`as_charge_mos_control()` still return the payload in that case, so you can compare
`requested_on` with `reported_on`.

> [!NOTE]
> The protocol document marks every byte of the production date reply (`0x58`) as reserved.
> `as_battery_production_date()` assumes it has the same layout as the RTC reply (`0x61`) and
> decodes it as an `Rtc`.

### Threading

`HaidiBMS` is not thread-safe. Call all of its methods from one thread or context, or protect
access with your own lock.

## ⚙️ Configuration

### CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `HAIDI_BUILD_TESTS` | `ON` standalone, `OFF` as a subproject | Build the GoogleTest suite |
| `HAIDI_BUILD_EXAMPLES` | `ON` standalone, `OFF` as a subproject | Build the example programs (POSIX only) |

### Queue size

The request queue holds 24 requests by default. To change it, define
`HAIDI_REQUEST_QUEUE_CAPACITY` for the library and for everything that includes its headers:

```cmake
target_compile_definitions(haidi_bms_cpp PUBLIC HAIDI_REQUEST_QUEUE_CAPACITY=32)
```

## 📂 Examples

Both examples need a POSIX system with `termios`.

**[examples/poll_loop.cpp](examples/poll_loop.cpp)** shows a minimal complete integration. It
sets up the serial port, reads identification at start-up, then repeatedly reads telemetry and
prints the decoded values.

```bash
cmake --build build --target haidi_poll_loop
./build/haidi_poll_loop /dev/ttyUSB0 1000     # device, sweep interval in ms
```

**[examples/dump_all.cpp](examples/dump_all.cpp)** reads all 66 read commands and prints every
decoded value, which helps when bringing up an unfamiliar pack. It never sends MOSFET or
configuration writes, so it's safe to run against a live battery.

```bash
cmake --build build --target haidi_dump_all
./build/haidi_dump_all -d /dev/ttyUSB0          # one full sweep, then exit
./build/haidi_dump_all -d /dev/ttyUSB0 -n 0     # sweep until interrupted
./build/haidi_dump_all -d /dev/ttyUSB0 -v       # also print every frame in hex
./build/haidi_dump_all -h                       # all options
```

For a catalogue of every request and the payload it decodes to, see
[tests/test_commands.cpp](tests/test_commands.cpp).

## 🩺 Troubleshooting

**A command always times out.**
The pack's firmware probably doesn't implement it. The protocol has no "unsupported command"
reply, so a timeout is the only signal. `haidi_dump_all` lists these as `no reply`.

**Requests return `QUEUE_FULL` after running for a while.**
`tick()` isn't being called, so a lost reply is never timed out and the queue fills up.

**`discarded_bytes()` keeps increasing.**
The link is noisy or the baud rate is wrong. HAIDI packs use 9600 8N1.

**A per-cell read returns fewer values than expected.**
A reply that ends early is still reported as a `RESPONSE`, with a smaller `count`. Values after
a missing frame are dropped instead of being reported as 0. If this happens often, increase
`set_interframe_timeout_ms()` or pass the cell count explicitly.

**Cell voltages look shifted, or the first few cells read 0 mV.**
The library already handles this, but it's worth knowing about if you're porting another driver.
Multi-frame replies number their frames, and packs don't always start from the number the
protocol document specifies. For example, this pack numbers its `0x95` reply from 1 where the
document says 0:

```text
A5 01 95 08 | 01 0B B8 0B B8 0B B8 00 | 8D
A5 01 95 08 | 02 0B B8 0B B8 0B B8 00 | 8E
              ^^ frame counter
```

The library uses whichever numbering the pack sends, so no configuration is needed.

**Seeing exactly what's on the wire.**
Run `haidi_dump_all -v` to print every frame sent and received.

## 🧭 API reference

| Header | Contents |
| --- | --- |
| [include/haidi_bms.hpp](include/haidi_bms.hpp) | `HaidiBMS`: the session, requests, queue, timeouts |
| [include/haidi_event.hpp](include/haidi_event.hpp) | `Event`, `EventKind`, `ErrorCode` and the `as_*()` accessors |
| [include/haidi_payloads.hpp](include/haidi_payloads.hpp) | Decoded payload structs, with units for every field |
| [include/haidi_protocol.hpp](include/haidi_protocol.hpp) | Protocol constants, Data IDs, frame encoding and checksum |
| [include/haidi_frame_parser.hpp](include/haidi_frame_parser.hpp) | Standalone byte-stream framer |

Every public symbol has Doxygen documentation. To build the HTML reference:

```bash
sudo apt install doxygen graphviz        # or: brew install doxygen graphviz
cmake -B build                           # re-run if Doxygen was installed after the first configure
cmake --build build --target docs
xdg-open build/docs/html/index.html      # macOS: open, Windows: start
```

## 🔨 Development

Build and run the tests (GoogleTest is found on the system or downloaded automatically):

```bash
git clone https://github.com/lekoook/haidi_bms_cpp.git
cd haidi_bms_cpp
cmake -B build
cmake --build build -j
ctest --test-dir build
```

Additional options and targets:

| Option / target | Purpose |
| --- | --- |
| `-DHAIDI_ENABLE_CLANG_TIDY=ON` | Run clang-tidy during the build |
| `-DHAIDI_CLANG_TIDY_WERROR=ON` | Treat clang-tidy warnings as errors |
| `--target format` / `format-check` | Apply / check `.clang-format` |
| `--target lint` / `lint-fix` | Run clang-tidy over `src/` / and apply fixes |
| `--target docs-check` | Fail if any public symbol is undocumented |

## 📓 Versioning

This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Changes are
listed in [CHANGELOG.md](CHANGELOG.md).

## 📜 License

MIT. See [LICENSE](LICENSE).
