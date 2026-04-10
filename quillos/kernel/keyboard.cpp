#include "io.h"
#include "shell.h"
#include "idt.h"
#include <stdint.h>

// ================================================================
// PS/2 keyboard driver — Scancode Set 1
//
// Tracks Shift and Ctrl modifiers. Generates proper ASCII including
// uppercase letters, symbols (!@#$...), and standard control codes
// for Ctrl+letter combinations (Ctrl+A=0x01 ... Ctrl+Z=0x1A).
// ================================================================

static const char sc_lower[] = {
    /*00*/ 0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    /*0F*/ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    /*1D*/ 0,  'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    /*2C*/ 'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

static const char sc_upper[] = {
    /*00*/ 0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    /*0F*/ '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    /*1D*/ 0,  'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    /*2C*/ 'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};

static bool shift_down = false;
static bool ctrl_down  = false;
static bool ext_prefix = false; // After 0xE0

static void keyboard_irq_handler(InterruptFrame*) {
    uint8_t sc = inb(0x60);

    // Handle extended-key prefix (arrows, etc.)
    if (sc == 0xE0) {
        ext_prefix = true;
        return;
    }

    bool release = (sc & 0x80) != 0;
    uint8_t code = sc & 0x7F;

    if (ext_prefix) {
        ext_prefix = false;
        // TODO: handle arrow keys etc. here. For now, ignore.
        return;
    }

    // Modifier keys
    if (code == 0x2A || code == 0x36) {  // LShift or RShift
        shift_down = !release;
        return;
    }
    if (code == 0x1D) {                  // LCtrl (RCtrl would be E0 1D)
        ctrl_down = !release;
        return;
    }

    // Ignore all other key releases
    if (release) return;

    // Bounds check
    if (code >= sizeof(sc_lower)) return;

    char c = shift_down ? sc_upper[code] : sc_lower[code];
    if (c == 0) return;

    // Ctrl+letter -> control character (0x01..0x1A)
    // Also works for Ctrl+lowercase since shift state is orthogonal
    if (ctrl_down) {
        char base = c;
        if (base >= 'a' && base <= 'z') {
            c = (char)(base - 'a' + 1);
        } else if (base >= 'A' && base <= 'Z') {
            c = (char)(base - 'A' + 1);
        } else {
            return; // Ignore other Ctrl combinations
        }
    }

    shell_update(c);
}

void keyboard_init() {
    irq_register(1, keyboard_irq_handler);
}
