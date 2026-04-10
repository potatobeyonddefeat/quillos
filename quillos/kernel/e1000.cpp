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

    static constexpr uint32_t CTRL_RST  = (1 << 26);
    static constexpr uint32_t CTRL_SLU  = (1 << 6);
    static constexpr uint32_t CTRL_ASDE = (1 << 5);
    static constexpr uint32_t RCTL_EN   = (1 << 1);
    static constexpr uint32_t RCTL_BAM  = (1 << 15);
    static constexpr uint32_t RCTL_SECRC = (1 << 26);
    static constexpr uint32_t TCTL_EN   = (1 << 1);
    static constexpr uint32_t TCTL_PSP  = (1 << 3);

    static constexpr int NUM_TX = 8;
    static constexpr int NUM_RX = 8;
    static constexpr int BUF_SIZE = 2048;

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

    // All DMA memory is PMM-allocated (physical addresses known)
    static volatile uint32_t* mmio = nullptr;
    static TxDesc* tx_ring = nullptr;  // Virtual pointer to PMM-allocated ring
    static RxDesc* rx_ring = nullptr;
    static uint64_t tx_ring_phys = 0;
    static uint64_t rx_ring_phys = 0;
    static uint64_t tx_buf_phys[NUM_TX];
    static uint8_t* tx_buf_virt[NUM_TX];
    static uint64_t rx_buf_phys[NUM_RX];
    static uint8_t* rx_buf_virt[NUM_RX];

    static uint8_t mac_addr[6];
    static int tx_cur = 0;
    static int rx_cur = 0;
    static bool ready = false;

    static uint32_t rd(uint32_t reg) { return mmio[reg / 4]; }
    static void wr(uint32_t reg, uint32_t val) { mmio[reg / 4] = val; }

    bool init() {
        // Find E1000 on PCI (8086:100E or 8086:100F)
        const PCI::Device* nic = nullptr;
        for (uint32_t i = 0; i < PCI::get_count(); i++) {
            const PCI::Device* d = PCI::get_device(i);
            if (d && d->vendor_id == 0x8086 &&
                (d->device_id == 0x100E || d->device_id == 0x100F)) {
                nic = d;
                break;
            }
        }
        if (!nic) {
            // Try finding any network controller (class 0x02)
            for (uint32_t i = 0; i < PCI::get_count(); i++) {
                const PCI::Device* d = PCI::get_device(i);
                if (d && d->class_code == 0x02 && d->vendor_id == 0x8086) {
                    nic = d;
                    break;
                }
            }
        }
        if (!nic) {
            console_print("\n[E1000] No Intel NIC found");
            return false;
        }

        char buf[32];
        console_print("\n[E1000] Found PCI ");
        itoa(nic->vendor_id, buf); console_print(buf);
        console_print(":");
        itoa(nic->device_id, buf); console_print(buf);

        // Read BAR0 — may be 64-bit
        uint32_t bar0_low = PCI::config_read(nic->bus, nic->device, nic->function, 0x10);
        uint64_t bar0_phys = bar0_low & ~0xFULL;

        // Check if 64-bit BAR (type bits 1:2 == 0b10)
        if ((bar0_low & 0x6) == 0x4) {
            uint32_t bar0_high = PCI::config_read(nic->bus, nic->device, nic->function, 0x14);
            bar0_phys |= ((uint64_t)bar0_high << 32);
        }

        if (bar0_phys == 0) {
            console_print("\n[E1000] BAR0 is zero");
            return false;
        }

        // Enable PCI bus mastering
        PCI::enable_bus_mastering(nic->bus, nic->device, nic->function);

        // Map MMIO via HHDM
        mmio = (volatile uint32_t*)Memory::phys_to_virt(bar0_phys);

        // Reset
        uint32_t ctrl = rd(REG_CTRL);
        wr(REG_CTRL, ctrl | CTRL_RST);
        for (volatile int i = 0; i < 200000; i++);
        // Wait for reset to complete
        while (rd(REG_CTRL) & CTRL_RST) {
            for (volatile int i = 0; i < 1000; i++);
        }

        // Disable interrupts
        wr(REG_IMC, 0xFFFFFFFF);
        rd(REG_ICR);

        // Link up
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

        // Check for valid MAC
        if (mac_addr[0] == 0 && mac_addr[1] == 0 && mac_addr[2] == 0 &&
            mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0) {
            console_print("\n[E1000] MAC is all zeros — EEPROM issue");
            return false;
        }

        // Clear multicast table
        for (int i = 0; i < 128; i++) wr(REG_MTA + i * 4, 0);

        // ---- Allocate RX ring and buffers from PMM ----
        rx_ring_phys = Memory::pmm_alloc_page();
        if (!rx_ring_phys) { console_print("\n[E1000] RX ring alloc failed"); return false; }
        rx_ring = (RxDesc*)Memory::phys_to_virt(rx_ring_phys);

        for (int i = 0; i < NUM_RX; i++) {
            rx_buf_phys[i] = Memory::pmm_alloc_page();
            if (!rx_buf_phys[i]) { console_print("\n[E1000] RX buf alloc failed"); return false; }
            rx_buf_virt[i] = (uint8_t*)Memory::phys_to_virt(rx_buf_phys[i]);

            rx_ring[i].addr = rx_buf_phys[i];
            rx_ring[i].status = 0;
            rx_ring[i].length = 0;
        }

        wr(REG_RDBAL, (uint32_t)(rx_ring_phys & 0xFFFFFFFF));
        wr(REG_RDBAH, (uint32_t)(rx_ring_phys >> 32));
        wr(REG_RDLEN, NUM_RX * sizeof(RxDesc));
        wr(REG_RDH, 0);
        wr(REG_RDT, NUM_RX - 1);
        rx_cur = 0;

        wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

        // ---- Allocate TX ring and buffers from PMM ----
        tx_ring_phys = Memory::pmm_alloc_page();
        if (!tx_ring_phys) { console_print("\n[E1000] TX ring alloc failed"); return false; }
        tx_ring = (TxDesc*)Memory::phys_to_virt(tx_ring_phys);

        for (int i = 0; i < NUM_TX; i++) {
            tx_buf_phys[i] = Memory::pmm_alloc_page();
            if (!tx_buf_phys[i]) { console_print("\n[E1000] TX buf alloc failed"); return false; }
            tx_buf_virt[i] = (uint8_t*)Memory::phys_to_virt(tx_buf_phys[i]);

            tx_ring[i].addr = tx_buf_phys[i];
            tx_ring[i].status = 1; // DD = done (available)
            tx_ring[i].cmd = 0;
            tx_ring[i].length = 0;
        }

        wr(REG_TDBAL, (uint32_t)(tx_ring_phys & 0xFFFFFFFF));
        wr(REG_TDBAH, (uint32_t)(tx_ring_phys >> 32));
        wr(REG_TDLEN, NUM_TX * sizeof(TxDesc));
        wr(REG_TDH, 0);
        wr(REG_TDT, 0);
        tx_cur = 0;

        wr(REG_TCTL, TCTL_EN | TCTL_PSP | (15 << 4) | (64 << 12));
        wr(REG_TIPG, 10 | (10 << 10) | (10 << 20));

        ready = true;

        console_print("\n[E1000] MAC ");
        for (int i = 0; i < 6; i++) {
            const char* hex = "0123456789ABCDEF";
            char h[4];
            h[0] = hex[mac_addr[i] >> 4];
            h[1] = hex[mac_addr[i] & 0xF];
            h[2] = (i < 5) ? ':' : '\0';
            h[3] = '\0';
            console_print(h);
        }
        console_print(", link up");

        return true;
    }

    bool send(const uint8_t* data, uint16_t len) {
        if (!ready || !data || len == 0 || len > 1514) return false;

        // Wait for descriptor to be available
        int timeout = 10000;
        while (!(tx_ring[tx_cur].status & 1) && --timeout > 0) {
            for (volatile int i = 0; i < 100; i++);
        }
        if (timeout == 0) return false;

        // Copy packet to DMA buffer
        for (uint16_t i = 0; i < len; i++) {
            tx_buf_virt[tx_cur][i] = data[i];
        }

        tx_ring[tx_cur].length = len;
        tx_ring[tx_cur].cmd = (1<<0) | (1<<1) | (1<<3); // EOP|IFCS|RS
        tx_ring[tx_cur].status = 0;

        tx_cur = (tx_cur + 1) % NUM_TX;
        wr(REG_TDT, tx_cur);

        return true;
    }

    bool poll_receive(uint8_t* data, uint16_t* len) {
        if (!ready || !data || !len) return false;

        if (!(rx_ring[rx_cur].status & 1)) return false; // No packet (DD=0)

        uint16_t pkt_len = rx_ring[rx_cur].length;
        if (pkt_len > BUF_SIZE) pkt_len = BUF_SIZE;

        for (uint16_t i = 0; i < pkt_len; i++) {
            data[i] = rx_buf_virt[rx_cur][i];
        }
        *len = pkt_len;

        // Reset descriptor
        rx_ring[rx_cur].status = 0;
        int old = rx_cur;
        rx_cur = (rx_cur + 1) % NUM_RX;
        wr(REG_RDT, old);

        return true;
    }

    const uint8_t* get_mac() { return mac_addr; }
    bool is_ready() { return ready; }
}
