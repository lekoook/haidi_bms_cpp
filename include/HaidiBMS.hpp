#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace haidi
{

static constexpr size_t MESSAGE_MAX_LEN = 13;
static constexpr uint8_t START_FLAG = 0xA5;

using MessageBytes = std::array<uint8_t, MESSAGE_MAX_LEN>;

enum class HostAddress : uint8_t { BLE = 0x80, GPRS = 0x20, COMP = 0x40, BROADCAST = 0xFF };

class HaidiMessage {
    public:
    virtual ~HaidiMessage() = default;
    virtual MessageBytes send_uart() = 0;

    protected:
    HostAddress host_address_ = HostAddress{HostAddress::COMP};
    uint8_t data_id_ = 0;
    HaidiMessage(HostAddress host_address, uint8_t data_id);
};

class ReadCapacityVoltage : public HaidiMessage {
    public:
    ReadCapacityVoltage(HostAddress host_address);

    MessageBytes send_uart() override;
};

class HaidiBMS {
    public:
    HaidiBMS(HostAddress host_address, uint8_t address = 0x01);
    uint8_t get_capacity();
    uint8_t get_voltage();

    private:
    HostAddress host_address_{HostAddress::COMP};
    uint8_t address_ = 0;
};

} // namespace haidi
