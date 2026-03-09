#include <limine.h>
#include <stdint.h>
#include <stddef.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

static void hcf() {
    for (;;) {
        asm("hlt");
    }
}

extern "C" void _start() {

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    struct limine_framebuffer *fb =
        framebuffer_request.response->framebuffers[0];

    uint32_t *pixels = (uint32_t*)fb->address;

    for (uint64_t y = 0; y < fb->height; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            pixels[y * (fb->pitch / 4) + x] = 0x0000FF;
        }
    }

    hcf();
}
