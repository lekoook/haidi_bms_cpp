// Copyright (c) 2026 Xavier Lee <kokteng1313@gmail.com>
// SPDX-License-Identifier: MIT

#include <haidi_protocol.hpp>

namespace haidi
{

uint8_t calc_chksum(const MessageBytes& bytes) {
    // Section 5.3 note 2: sum of every preceding byte, low byte only. The
    // accumulator is deliberately uint8_t so the truncation is the spec's.
    uint8_t sum = 0;
    for (size_t i = 0; i < MSG_LAST_INDEX; ++i) {
        sum = static_cast<uint8_t>(sum + bytes[i]);
    }
    return sum;
}

MessageBytes encode(HostAddress host, DataId id, const DataBytes& data) {
    MessageBytes msg{};
    msg[MSG_START_INDEX] = START_FLAG;
    msg[MSG_ADDRESS_INDEX] = static_cast<uint8_t>(host);
    msg[MSG_DATA_ID_INDEX] = static_cast<uint8_t>(id);
    msg[MSG_LEN_INDEX] = LENGTH_FLAG;
    for (size_t i = 0; i < DATA_LEN; ++i) {
        msg[DATA_OFFSET + i] = data[i];
    }
    msg[MSG_LAST_INDEX] = calc_chksum(msg);
    return msg;
}

MessageBytes encode(HostAddress host, DataId id) {
    // Every read command documents Byte0~Byte7 as reserved, sent as zero.
    return encode(host, id, DataBytes{});
}

bool is_known_data_id(DataId id) {
    const auto raw = static_cast<uint8_t>(id);
    return (raw >= 0x50 && raw <= 0x6A) || (raw >= 0x70 && raw <= 0x8A) ||
           (raw >= 0x90 && raw <= 0x9A) || (raw >= 0xD8 && raw <= 0xDA);
}

DataBytes data_of(const MessageBytes& frame) {
    DataBytes data{};
    for (size_t i = 0; i < DATA_LEN; ++i) {
        data[i] = frame[DATA_OFFSET + i];
    }
    return data;
}

} // namespace haidi
