#include "disk.h"
#include "io.h"

extern void console_print(const char* str);
extern void itoa(uint64_t n, char* str);

namespace Disk {

    // ================================================================
    // ATA I/O port addresses (primary bus)
    // ================================================================
    static constexpr uint16_t ATA_DATA        = 0x1F0;
    static constexpr uint16_t ATA_ERROR       = 0x1F1;
    static constexpr uint16_t ATA_SECT_COUNT  = 0x1F2;
    static constexpr uint16_t ATA_LBA_LO      = 0x1F3;
    static constexpr uint16_t ATA_LBA_MID     = 0x1F4;
    static constexpr uint16_t ATA_LBA_HI      = 0x1F5;
    static constexpr uint16_t ATA_DRIVE_HEAD  = 0x1F6;
    static constexpr uint16_t ATA_STATUS      = 0x1F7;
    static constexpr uint16_t ATA_COMMAND     = 0x1F7;
    static constexpr uint16_t ATA_ALT_STATUS  = 0x3F6;

    // ATA commands
    static constexpr uint8_t CMD_IDENTIFY     = 0xEC;
    static constexpr uint8_t CMD_READ_SECTORS = 0x20;
    static constexpr uint8_t CMD_WRITE_SECTORS= 0x30;
    static constexpr uint8_t CMD_CACHE_FLUSH  = 0xE7;

    // Status register bits
    static constexpr uint8_t SR_BSY  = 0x80;  // Busy
    static constexpr uint8_t SR_DRDY = 0x40;  // Drive ready
    static constexpr uint8_t SR_DRQ  = 0x08;  // Data request
    static constexpr uint8_t SR_ERR  = 0x01;  // Error

    // ================================================================
    // Internal state
    // ================================================================
    static BlockDevice drive;
    static uint16_t identify_data[256];  // Raw IDENTIFY response

    // ================================================================
    // ATA helpers
    // ================================================================

    // 400ns delay by reading alternate status 4 times
    static void ata_400ns() {
        inb(ATA_ALT_STATUS);
        inb(ATA_ALT_STATUS);
        inb(ATA_ALT_STATUS);
        inb(ATA_ALT_STATUS);
    }

    // Wait for BSY to clear (with timeout). Returns status byte.
    static uint8_t ata_wait_not_busy() {
        for (int i = 0; i < 100000; i++) {
            uint8_t status = inb(ATA_STATUS);
            if (!(status & SR_BSY)) return status;
        }
        return 0xFF;  // Timeout
    }

    // Wait for BSY clear AND DRQ set (data ready). Returns true on success.
    static bool ata_wait_drq() {
        ata_400ns();
        for (int i = 0; i < 100000; i++) {
            uint8_t status = inb(ATA_STATUS);
            if (status & SR_ERR) return false;
            if (!(status & SR_BSY) && (status & SR_DRQ)) return true;
        }
        return false;  // Timeout
    }

    // Extract a string from IDENTIFY data (bytes are swapped per word)
    static void ata_string(uint16_t* data, int start_word, int word_count, char* out) {
        int pos = 0;
        for (int w = start_word; w < start_word + word_count; w++) {
            out[pos++] = (char)(data[w] >> 8);    // High byte first
            out[pos++] = (char)(data[w] & 0xFF);  // Low byte second
        }
        // Null-terminate and trim trailing spaces
        out[pos] = '\0';
        while (pos > 0 && out[pos - 1] == ' ') {
            out[--pos] = '\0';
        }
    }

    // ================================================================
    // IDENTIFY — Detect drive and read its parameters
    // ================================================================
    static bool ata_identify() {
        // Select master drive (bit 4 = 0 for master)
        outb(ATA_DRIVE_HEAD, 0xA0);
        ata_400ns();

        // Zero the parameter registers
        outb(ATA_SECT_COUNT, 0);
        outb(ATA_LBA_LO, 0);
        outb(ATA_LBA_MID, 0);
        outb(ATA_LBA_HI, 0);

        // Send IDENTIFY command
        outb(ATA_COMMAND, CMD_IDENTIFY);
        ata_400ns();

        // Check if any device is there
        uint8_t status = inb(ATA_STATUS);
        if (status == 0) return false;  // No drive on bus

        // Wait for BSY to clear
        status = ata_wait_not_busy();
        if (status == 0xFF) return false;  // Timeout

        // After BSY clears, check LBA_MID and LBA_HI.
        // If non-zero, this is NOT an ATA drive (might be ATAPI/SATA).
        if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
            return false;
        }

        // Wait for DRQ (data ready) or ERR
        if (!ata_wait_drq()) return false;

        // Read 256 words (512 bytes) of IDENTIFY data
        for (int i = 0; i < 256; i++) {
            identify_data[i] = inw(ATA_DATA);
        }

        return true;
    }

    // ================================================================
    // Public API
    // ================================================================

    bool init() {
        // Zero the drive info
        drive.present = false;
        drive.total_sectors = 0;
        drive.size_mb = 0;
        drive.lba_supported = false;
        for (int i = 0; i < 41; i++) drive.model[i] = 0;
        for (int i = 0; i < 21; i++) drive.serial[i] = 0;

        if (!ata_identify()) {
            console_print("\n[DISK] No ATA drive detected on primary bus");
            return false;
        }

        drive.present = true;

        // Extract model string (words 27-46, 20 words = 40 chars)
        ata_string(identify_data, 27, 20, drive.model);

        // Extract serial number (words 10-19, 10 words = 20 chars)
        ata_string(identify_data, 10, 10, drive.serial);

        // Check LBA support (word 49, bit 9)
        drive.lba_supported = (identify_data[49] & (1 << 9)) != 0;

        // Total LBA28 sectors (words 60-61 as a 32-bit value)
        drive.total_sectors = (uint32_t)identify_data[60]
                            | ((uint32_t)identify_data[61] << 16);

        // Size in MB (sectors * 512 / 1048576)
        drive.size_mb = drive.total_sectors / 2048;

        // Print boot info
        char buf[32];
        console_print("\n[DISK] ATA drive detected: ");
        console_print(drive.model);
        console_print("\n[DISK] ");
        itoa(drive.total_sectors, buf); console_print(buf);
        console_print(" sectors (");
        itoa(drive.size_mb, buf); console_print(buf);
        console_print(" MB), LBA ");
        console_print(drive.lba_supported ? "yes" : "no");

        return true;
    }

    bool read_sector(uint32_t lba, void* buffer) {
        if (!drive.present || !drive.lba_supported || !buffer) return false;
        if (lba >= drive.total_sectors) return false;

        // Select drive + top 4 bits of LBA (LBA mode: bit 6 = 1)
        outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));

        // Set sector count to 1
        outb(ATA_SECT_COUNT, 1);

        // Set LBA bytes
        outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
        outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

        // Send READ SECTORS command
        outb(ATA_COMMAND, CMD_READ_SECTORS);

        // Wait for data
        if (!ata_wait_drq()) return false;

        // Read 256 words (512 bytes)
        uint16_t* buf16 = (uint16_t*)buffer;
        for (int i = 0; i < 256; i++) {
            buf16[i] = inw(ATA_DATA);
        }

        return true;
    }

    bool read_sectors(uint32_t lba, uint32_t count, void* buffer) {
        uint8_t* dst = (uint8_t*)buffer;
        for (uint32_t i = 0; i < count; i++) {
            if (!read_sector(lba + i, dst + i * 512)) return false;
        }
        return true;
    }

    bool write_sector(uint32_t lba, const void* buffer) {
        if (!drive.present || !drive.lba_supported || !buffer) return false;
        if (lba >= drive.total_sectors) return false;

        outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_SECT_COUNT, 1);
        outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
        outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

        outb(ATA_COMMAND, CMD_WRITE_SECTORS);

        if (!ata_wait_drq()) return false;

        const uint16_t* buf16 = (const uint16_t*)buffer;
        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA, buf16[i]);
        }

        // Flush write cache
        outb(ATA_COMMAND, CMD_CACHE_FLUSH);
        ata_wait_not_busy();

        return true;
    }

    bool is_present() { return drive.present; }
    const BlockDevice* get_info() { return &drive; }
}
