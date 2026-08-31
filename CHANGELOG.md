# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the version is below `1.0.0`, a breaking change bumps the minor version.

## [Unreleased]

## [0.1.0] - 2026-09-14

Initial release: a complete, tested driver for the HAIDI CAN/485/UART protocol V4.0.

### Added

- **Frame codec** (`haidi_protocol.hpp`) — 13-byte frame encoding, checksum, big-endian field
  readers, the `DataId` enumeration of every documented command, `HostAddress`, and the
  scaling helpers for current, temperature and year offsets.
- **Byte-stream framer** (`haidi_frame_parser.hpp`) — reassembles arbitrarily chunked serial input
  into whole, checksum-verified frames. Resynchronises by dropping a single byte and rescanning,
  so a genuine frame that began inside a corrupt one still survives, and counts junk bytes via
  `discarded()` for diagnosing a noisy link or a baud-rate mismatch.
- **Typed payloads** (`haidi_payloads.hpp`) — a decoded struct per command family, plus
  `RawPayload`, which hands back the production date reply (`0x58`) untouched because the
  protocol document never specifies its layout.
- **Events** (`haidi_event.hpp`) — the `Event` type, `EventKind`, `ErrorCode`, and checked
  `as_*()` accessors that return `nullptr` on a mismatch rather than reinterpreting another
  command's bytes. `to_string()` overloads for `DataId`, `EventKind` and `ErrorCode`. Each
  `ErrorCode` documents whether it ends the request or leaves it in flight — `FRAME_SEQUENCE`,
  raised for a frame counter above 16 with a stray `0xFF` sentinel included, leaves it in flight.
- **Session** (`haidi_bms.hpp`) — the asynchronous driver: a fixed-capacity request queue holding
  one transaction outstanding as protocol section 5.3 requires, response and inter-frame
  timeouts, retries, fault-record streaming terminated by the `0xFF` sentinel, and dispatch to a
  session-wide handler plus an optional per-request one.
- **Multi-frame reassembly** — tolerant of duplicate and out-of-order frames, and of either frame
  numbering convention: the base is derived per transfer from the counters that actually arrive
  rather than asserted from the protocol document, so a pack that numbers `0x95` from 1 where the
  document says 0 decodes correctly on any paginated command, and at full width its 16 frames are
  accepted whether they arrive as 0..15 or 1..16. `MultiFrameInfo::frame_base` remains as the
  documented default. The buffer is cleared per transaction, so a reply can never inherit the
  previous one's bytes, and a reply is truncated at the first missing frame rather than returned
  full-length with a hole in it — a hole in `0x95` being indistinguishable from cells genuinely
  reading 0 mV.
- **Command coverage** — 40 `poll_*()` methods plus `set_charge_mos()` and
  `set_discharge_mos()`, spanning live telemetry, per-cell voltage and temperature reads,
  identification, configuration parameters, and all 27 alarm thresholds collapsed into a single
  `poll_alarm_threshold(AlarmClass, AlarmLevel)` call.
- **MOSFET write verification** — the protocol defines no ACK or NAK, so an echoed state
  differing from the requested one is surfaced as `ErrorCode::WRITE_REJECTED`, with the event
  still carrying the `MosControlAck` holding both values.
- **Auto-sized paginated reads** — `poll_cell_voltages()` and `poll_cell_temperatures()` size
  themselves from the cell and sensor counts cached from the most recent `0x94` or `0x51` reply
  when no explicit count is given.
- **Introspection** — `busy()`, `pending()`, `in_flight()`, `cached_cell_count()`,
  `cached_temp_count()`, `discarded_bytes()` and `reset()`.
- **Build system** — CMake 3.16+, C++17, library target `haidi_bms_cpp` publishing `include/`
  as a `PUBLIC` include directory. `HAIDI_BUILD_TESTS` defaults on standalone and off as a
  subproject, so a consumer never fetches GoogleTest.
- **Test suite** — 177 GoogleTest cases across the protocol, framer, payloads, event accessors,
  session state machine, every command, and multi-frame reassembly. Built against the public
  headers only.
- **Developer tooling** — `docs` and `docs-check` (Doxygen), `format` and `format-check`
  (clang-format), `lint` and `lint-fix` (clang-tidy), plus the `HAIDI_ENABLE_CLANG_TIDY` and
  `HAIDI_CLANG_TIDY_WERROR` options for linting inline with the build. `docs-check` runs with
  `WARN_NO_PARAMDOC`, so a public function with an undocumented parameter or return value fails
  the target.
- **Polling example** — `examples/poll_loop.cpp`, a complete POSIX serial polling loop built as
  the `haidi_poll_loop` target.
- **Hardware dump example** — `examples/dump_all.cpp`, built as the `haidi_dump_all` target: a
  bring-up tool that walks all 66 read commands against a real BMS on a serial port and prints
  every reply decoded into engineering units. Covers identification, live telemetry, per-cell
  voltages and temperatures, configuration, all 27 alarm thresholds and the stored fault-record
  stream, and decodes the 0x98 fault bits by name from the protocol document's table. Reads
  only — the two MOSFET control writes are deliberately absent, so it is safe against a live
  pack. Device, baud, host address, timeouts, retries, cell and sensor counts, sweep count and
  interval are all command-line options, and `-v` traces every frame sent and received in hex,
  which is what distinguishes a command the firmware does not implement from a link problem.
- **Documentation** — a Doxygen comment on every public symbol, including every parameter and
  return value, enforced by `docs-check`, with `@note` entries recording which reading was chosen
  wherever the protocol document contradicts itself. Overview page in `docs/mainpage.dox`;
  `README.md` covers integration and usage.
- **Licence** — MIT.

[Unreleased]: https://github.com/lekoook/haidi_bms_cpp/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/lekoook/haidi_bms_cpp/releases/tag/v0.1.0
