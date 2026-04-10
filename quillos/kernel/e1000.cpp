#include "e1000.h"
#include "pci.h"
#include "memory.h"
#include "io.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace E1000 {

    // E1000 register offsets
    static constexpr uint32_t REG_CTRL   = 0x0000;
    static constexpr uint32_t REG_STATUS = 0x0008;
    static constexpr uint32_t REG_EERD   = 0x0014;
    static constexpr uint32_t REG_ICR    = 0x00C0;
    static constexpr uint32_t REG_IMS    = 0x00D0;
    static constexpr uint32_t REG_IMC    = 0x00D8;
    static constexpr uint32_t REG_RCTL   = 0x0100;
    static constexpr uint32_t REG_TCTL   = 0x0400;
    static constexpr uint32_t REG_TIPG   = 0x0410;
    static constexpr uint32_t REG_RDBAL  = 0x2800;
    static constexpr uint32_t REG_RDBAH  = 0x2804;
    static constexpr uint32_t REG_RDLEN  = 0x2808;
    static constexpr uint32_t REG_RDH    = 0x2810;
    static constexpr uint32_t REG_RDT    = 0x2818;
    static constexpr uint32_t REG_TDBAL  = 0x3800;
    static constexpr uint32_t REG_TDBAH  = 0x3804;
    static constexpr uint32_t REG_TDLEN  = 0x3808;
    static constexpr uint32_t REG_TDH    = 0x3810;
    static constexpr uint32_t REG_TDT    = 0x3818;
    static constexpr uint32_t REG_RAL    = 0x5400;
    static constexpr uint32_t REG_RAH    = 0x5404;
    static constexpr uint32_t REG_MTA    = 0x5200;

    // CTRL bits
    static constexpr uint32_t CTRL_RST  = (1 << 26);
    static constexpr uint32_t CTRL_SLU  = (1 << 6);
    static constexpr uint32_t CTRL_ASDE = (1 << 5);

    // RCTL bits
    static constexpr uint32_t RCTL_EN   = (1 << 1);
    static constexpr uint32_t RCTL_BAM  = (1 << 15);
    static constexpr uint32_t RCTL_BSIZE_2048 = (0 << 16);
    static constexpr uint32_t RCTL_SECRC = (1 << 26);

    // TCTL bits
    static constexpr uint32_t TCTL_EN   = (1 << 1);
    static constexpr uint32_t TCTL_PSP  = (1 << 3);

    // TX descriptor command bits
    static constexpr uint8_t TCMD_EOP  = (1 << 0);
    static constexpr uint8_t TCMD_IFCS = (1 << 1);
    static constexpr uint8_t TCMD_RS   = (1 << 3);

    // RX descriptor status bits
    static constexpr uint8_t RSTA_DD   = (1 << 0);
    static constexpr uint8_t RSTA_EOP  = (1 << 1);

    static constexpr int NUM_TX = 32;
    static constexpr int NUM_RX = 32;
    static constexpr int BUF_SIZE = 2048;

    // Descriptor structures
    struct TxDesc {
        uint64_t addr;
        uint16_t length;
        uint8_t  cso;
        uint8_t  cmd;
        uint8_t  status;
        uint8_t  css;
        uint16_t special;
    } __attribute__((packed));

    struct RxDesc {
        uint64_t addr;
        uint16_t length;
        uint16_t checksum;
        uint8_t  status;
        uint8_t  errors;
        uint16_t special;
    } __attribute__((packed));

    // Static descriptor rings and buffers (must be physically accessible)
    static TxDesc tx_ring[NUM_TX] __attribute__((aligned(128)));
    static RxDesc rx_ring[NUM_RX] __attribute__((aligned(128)));
    static uint8_t tx_bufs[NUM_TX][BUF_SIZE] __attribute__((aligned(16)));
    static uint8_t rx_bufs[NUM_RX][BUF_SIZE] __attribute__((aligned(16)));

    static volatile uint32_t* mmio = nullptr;
    static uint8_t mac_addr[6];
    static int tx_cur = 0;
    static int rx_cur = 0;
    static bool ready = false;

    static uint32_t rd(uint32_t reg) { return mmio[reg / 4]; }
    static void wr(uint32_t reg, uint32_t val) { mmio[reg / 4] = val; }

    bool init() {
        // Find E1000 on PCI bus (Intel 82540EM: 8086:100E)
        const PCI::Device* nic = nullptr;
        for (uint32_t i = 0; i < PCI::get_count(); i++) {
            const PCI::Device* d = PCI::get_device(i);
            if (d && d->vendor_id == 0x8086 && d->device_id == 0x100E) {
                nic = d;
                break;
            }
        }
        // Also check for 82545EM (8086:100F) used by some QEMU configs
        if (!nic) {
            for (uint32_t i = 0; i < PCI::get_count(); i++) {
                const PCI::Device* d = PCI::get_device(i);
                if (d && d->vendor_id == 0x8086 &&
                    (d->device_id == 0x100F || d->device_id == 0x10D3 ||
                     d->device_id == 0x153A)) {
                    nic = d;
                    break;
                }
            }
        }
        if (!nic) {
            console_print("\n[E1000] No Intel E1000 NIC found on PCI bus");
            return false;
        }

        // Read BAR0 (MMIO base address)
        uint32_t bar0 = PCI::config_read(nic->bus, nic->device, nic->function, 0x10);
        bar0 &= ~0xF; // Mask flags
        if (bar0 == 0) {
            console_print("\n[E1000] BAR0 is zero");
            return false;
        }

        // Enable PCI bus mastering (required for DMA)
        PCI::enable_bus_mastering(nic->bus, nic->device, nic->function);

        // Map MMIO via HHDM
        mmio = (volatile uint32_t*)Memory::phys_to_virt((uint64_t)bar0);

        // Reset the device
        wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);
        for (volatile int i = 0; i < 100000; i++);  // Wait for reset
        wr(REG_CTRL, rd(REG_CTRL) & ~CTRL_RST);
        for (volatile int i = 0; i < 100000; i++);

        // Disable interrupts (we'll poll)
        wr(REG_IMC, 0xFFFFFFFF);
        rd(REG_ICR); // Clear pending

        // Set link up
        wr(REG_CTRL, rd(REG_CTRL) | CTRL_SLU | CTRL_ASDE);

        // Read MAC from RAL/RAH
        uint32_t ral = rd(REG_RAL);
        uint32_t rah = rd(REG_RAH);
        mac_addr[0] = ral & 0xFF;
        mac_addr[1] = (ral >> 8) & 0xFF;
        mac_addr[2] = (ral >> 16) & 0xFF;
        mac_addr[3] = (ral >> 24) & 0xFF;
        mac_addr[4] = rah & 0xFF;
        mac_addr[5] = (rah >> 8) & 0xFF;

        // Clear multicast table
        for (int i = 0; i < 128; i++) {
            wr(REG_MTA + i * 4, 0);
        }

        // ---- Initialize RX ----
        for (int i = 0; i < NUM_RX; i++) {
            rx_ring[i].addr = Memory::virt_to_phys(&rx_bufs[i][0]);
            rx_ring[i].status = 0;
        }

        uint64_t rx_phys = Memory::virt_to_phys(&rx_ring[0]);
        wr(REG_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFF));
        wr(REG_RDBAH, (uint32_t)(rx_phys >> 32));
        wr(REG_RDLEN, NUM_RX * sizeof(RxDesc));
        wr(REG_RDH, 0);
        wr(REG_RDT, NUM_RX - 1);
        rx_cur = 0;

        // Enable receiver: accept broadcast, 2048-byte buffers, strip CRC
        wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);

        // ---- Initialize TX ----
        for (int i = 0; i < NUM_TX; i++) {
            tx_ring[i].addr = Memory::virt_to_phys(&tx_bufs[i][0]);
            tx_ring[i].status = 1; // DD bit set = descriptor available
            tx_ring[i].cmd = 0;
        }

        uint64_t tx_phys = Memory::virt_to_phys(&tx_ring[0]);
        wr(REG_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFF));
        wr(REG_TDBAH, (uint32_t)(tx_phys >> 32));
        wr(REG_TDLEN, NUM_TX * sizeof(TxDesc));
        wr(REG_TDH, 0);
        wr(REG_TDT, 0);
        tx_cur = 0;

        // Enable transmitter + pad short packets
        wr(REG_TCTL, TCTL_EN | TCTL_PSP | (15 << 4) | (64 << 12));
        wr(REG_TIPG, 10 | (10 << 10) | (10 << 20));

        ready = true;

        char buf[8];
        console_print("\n[E1000] Ready, MAC ");
        for (int i = 0; i < 6; i++) {
            const char* hex = "0123456789ABCDEF";
            buf[0] = hex[mac_addr[i] >> 4];
            buf[1] = hex[mac_addr[i] & 0xF];
            buf[2] = (i < 5) ? ':' : '\0';
            buf[3] = '\0';
            console_print(buf);
        }

        return true;
    }

    bool send(const uint8_t* data, uint16_t len) {
        if (!ready || !data || len == 0 || len > 1514) return false;

        // Wait for current TX descriptor to be available
        while (!(tx_ring[tx_cur].status & 1)) {
            // Descriptor not done yet — spin briefly
            for (volatile int i = 0; i < 1000; i++);
        }

        // Copy packet data into TX buffer
        for (uint16_t i = 0; i < len; i++) {
            tx_bufs[tx_cur][i] = data[i];
        }

        // Set up descriptor
        tx_ring[tx_cur].length = len;
        tx_ring[tx_cur].cmd = TCMD_EOP | TCMD_IFCS | TCMD_RS;
        tx_ring[tx_cur].status = 0;

        // Bump tail pointer — this tells the E1000 to transmit
        int old_cur = tx_cur;
        tx_cur = (tx_cur + 1) % NUM_TX;
        wr(REG_TDT, tx_cur);

        (void)old_cur;
        return true;
    }

    bool poll_receive(uint8_t* data, uint16_t* len) {
        if (!ready || !data || !len) return false;

        // Check if current RX descriptor has a packet
        if (!(rx_ring[rx_cur].status & RSTA_DD)) {
            return false; // No packet available
        }

        // Read the packet
        uint16_t pkt_len = rx_ring[rx_cur].length;
        if (pkt_len > BUF_SIZE) pkt_len = BUF_SIZE;

        for (uint16_t i = 0; i < pkt_len; i++) {
            data[i] = rx_bufs[rx_cur][i];
        }
        *len = pkt_len;

        // Reset descriptor for reuse
        rx_ring[rx_cur].status = 0;

        // Advance and update tail
        int old_cur = rx_cur;
        rx_cur = (rx_cur + 1) % NUM_RX;
        wr(REG_RDT, old_cur);

        return true;
    }

    const uint8_t* get_mac() { return mac_addr; }
    bool is_ready() { return ready; }
}
