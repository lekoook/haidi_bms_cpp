#include <HaidiBMS.hpp>

namespace haidi
{

HaidiMessage::HaidiMessage(HostAddress host_address, uint8_t data_id)
    : host_address_(host_address), data_id_(data_id) {}

MessageBytes ReadCapacityVoltage::send_uart() {
    MessageBytes msg{START_FLAG, static_cast<uint8_t>(host_address_),
                     data_id_,   8,
                     0x00,       0x00,
                     0x00,       0x00,
                     0x00,       0x00,
                     0x00,       0x00};
    return msg;
}

ReadCapacityVoltage::ReadCapacityVoltage(HostAddress host_address)
    : HaidiMessage(host_address, 0x50) {}

HaidiBMS::HaidiBMS(HostAddress host_address, uint8_t address)
    : host_address_(host_address), address_(address) {}

uint8_t HaidiBMS::get_capacity() {}

uint8_t HaidiBMS::get_voltage() {}

} // namespace haidi
