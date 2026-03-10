#pragma once
#include <stdint.h>

extern "C" void idt_init();
extern "C" void load_idt(void* ptr); // The assembly function
