#include "disk.h"
#include "io.h"

extern void console_print(const char* str);

namespace Disk {

    static constexpr uint16_t ATA_PRIMARY_IO   = 0x1F0;
    static constexpr uint16_t ATA_PRIMARY_CTRL = 0x3F6;
    static constexpr uint8_t ATA_CMD_IDENTIFY  = 0xEC;
    static constexpr uint8_t ATA_CMD_READ      = 0x20;
    static constexpr uint8_t ATA_CMD_WRITE     = 0x30;
    static constexpr uint8_t ATA_CMD_FLUSH     = 0xE7;
    static constexpr uint8_t ATA_SR_BSY        = 0x80;
    static constexpr uint8_t ATA_SR_DRQ        = 0x08;
    static constexpr uint8_t ATA_SR_ERR        = 0x01;

    static bool primary_present = false;

    static void ata_delay() {
        // Read status 4 times for 400ns delay
        for (int i = 0; i < 4; i++) inb(ATA_PRIMARY_CTRL);
    }

    static bool ata_poll() {
        ata_delay();
        for (int i = 0; i < 100000; i++) {
            uint8_t status = inb(ATA_PRIMARY_IO + 7);
            if (status & ATA_SR_ERR) return false;
            if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return true;
        }
        return false;
    }

    bool init() {
        // Select master drive on primary bus
        outb(ATA_PRIMARY_IO + 6, 0xA0);
        ata_delay();

        // Clear sector count and LBA registers
        outb(ATA_PRIMARY_IO + 2, 0);
        outb(ATA_PRIMARY_IO + 3, 0);
        outb(ATA_PRIMARY_IO + 4, 0);
        outb(ATA_PRIMARY_IO + 5, 0);

        // Send IDENTIFY command
        outb(ATA_PRIMARY_IO + 7, ATA_CMD_IDENTIFY);
        ata_delay();

        uint8_t status = inb(ATA_PRIMARY_IO + 7);
        if (status == 0) {
            console_print("\n[DISK] No ATA drive detected");
            primary_present = false;
            return false;
        }

        // Wait for BSY to clear
        for (int i = 0; i < 100000; i++) {
            status = inb(ATA_PRIMARY_IO + 7);
            if (!(status & ATA_SR_BSY)) break;
        }

        // Check LBA mid/high — non-zero means not ATA
        if (inb(ATA_PRIMARY_IO + 4) != 0 || inb(ATA_PRIMARY_IO + 5) != 0) {
            console_print("\n[DISK] Non-ATA device on primary bus");
            primary_present = false;
            return false;
        }

        // Wait for DRQ or ERR
        if (!ata_poll()) {
            console_print("\n[DISK] ATA identify failed");
            primary_present = false;
            return false;
        }

        // Read and discard 256 words of identify data
        for (int i = 0; i < 256; i++) inw(ATA_PRIMARY_IO);

        primary_present = true;
        console_print("\n[DISK] ATA drive detected on primary bus");
        return true;
    }

    bool read_sector(uint32_t lba, void* buffer) {
        if (!primary_present || !buffer) return false;

        outb(ATA_PRIMARY_IO + 6, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_PRIMARY_IO + 2, 1); // 1 sector
        outb(ATA_PRIMARY_IO + 3, lba & 0xFF);
        outb(ATA_PRIMARY_IO + 4, (lba >> 8) & 0xFF);
        outb(ATA_PRIMARY_IO + 5, (lba >> 16) & 0xFF);
        outb(ATA_PRIMARY_IO + 7, ATA_CMD_READ);

        if (!ata_poll()) return false;

        uint16_t* buf = (uint16_t*)buffer;
        for (int i = 0; i < 256; i++)
            buf[i] = inw(ATA_PRIMARY_IO);

        return true;
    }

    bool write_sector(uint32_t lba, const void* buffer) {
        if (!primary_present || !buffer) return false;

        outb(ATA_PRIMARY_IO + 6, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_PRIMARY_IO + 2, 1);
        outb(ATA_PRIMARY_IO + 3, lba & 0xFF);
        outb(ATA_PRIMARY_IO + 4, (lba >> 8) & 0xFF);
        outb(ATA_PRIMARY_IO + 5, (lba >> 16) & 0xFF);
        outb(ATA_PRIMARY_IO + 7, ATA_CMD_WRITE);

        if (!ata_poll()) return false;

        const uint16_t* buf = (const uint16_t*)buffer;
        for (int i = 0; i < 256; i++)
            outw(ATA_PRIMARY_IO, buf[i]);

        // Flush cache
        outb(ATA_PRIMARY_IO + 7, ATA_CMD_FLUSH);
        ata_delay();
        for (int i = 0; i < 100000; i++) {
            if (!(inb(ATA_PRIMARY_IO + 7) & ATA_SR_BSY)) break;
        }

        return true;
    }

    bool is_present() { return primary_present; }
}
