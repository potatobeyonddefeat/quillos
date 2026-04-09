#include "network.h"
#include "pci.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Network {

    static constexpr uint32_t MAX_PACKET_BUFFERS = 8;
    static constexpr uint16_t MAX_PACKET_SIZE = 1536;

    struct PacketBuffer {
        uint8_t data[MAX_PACKET_SIZE];
        uint16_t length;
        bool in_use;
    };

    static PacketBuffer rx_buffers[MAX_PACKET_BUFFERS];
    static PacketBuffer tx_buffers[MAX_PACKET_BUFFERS];
    static MACAddress local_mac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
    static uint32_t local_ip = 0x0A000002; // 10.0.0.2
    static bool nic_present = false;
    static uint16_t nic_vendor = 0;
    static uint16_t nic_device_id = 0;

    bool init() {
        for (uint32_t i = 0; i < MAX_PACKET_BUFFERS; i++) {
            rx_buffers[i].in_use = false;
            rx_buffers[i].length = 0;
            tx_buffers[i].in_use = false;
            tx_buffers[i].length = 0;
        }

        nic_present = false;

        // Check PCI for a network controller (class 0x02)
        const PCI::Device* nic = PCI::find_device(0x02, 0x00);
        if (nic) {
            detect_nic(nic->vendor_id, nic->device_id);
        }

        if (nic_present) {
            console_print("\n[NET] NIC detected (vendor=");
            char buf[16];
            itoa(nic_vendor, buf);
            console_print(buf);
            console_print(" device=");
            itoa(nic_device_id, buf);
            console_print(buf);
            console_print(")");

            console_print("\n[NET] IP: 10.0.0.2, MAC: 52:54:00:12:34:56");
        } else {
            console_print("\n[NET] No NIC found");
        }

        return true;
    }

    void detect_nic(uint16_t vendor, uint16_t device) {
        nic_vendor = vendor;
        nic_device_id = device;
        nic_present = true;
    }

    bool send_packet(const uint8_t* data, uint16_t length) {
        if (!nic_present || !data || length > MAX_PACKET_SIZE) return false;

        for (uint32_t i = 0; i < MAX_PACKET_BUFFERS; i++) {
            if (!tx_buffers[i].in_use) {
                for (uint16_t j = 0; j < length; j++)
                    tx_buffers[i].data[j] = data[j];
                tx_buffers[i].length = length;
                tx_buffers[i].in_use = true;
                return true;
            }
        }
        return false; // All TX buffers full
    }

    bool receive_packet(uint8_t* data, uint16_t* length) {
        if (!data || !length) return false;

        for (uint32_t i = 0; i < MAX_PACKET_BUFFERS; i++) {
            if (rx_buffers[i].in_use) {
                for (uint16_t j = 0; j < rx_buffers[i].length; j++)
                    data[j] = rx_buffers[i].data[j];
                *length = rx_buffers[i].length;
                rx_buffers[i].in_use = false;
                return true;
            }
        }
        return false;
    }

    bool is_present() { return nic_present; }
    uint32_t get_ip() { return local_ip; }
    void set_ip(uint32_t ip) { local_ip = ip; }
    const MACAddress* get_mac() { return &local_mac; }
}
