#pragma once

// Bootloader size. Must be 4k aligned. 
// Was 12k originally. Make sure to match:
// 1) danilom_bootloader/bootloader.ld
// 2) jpo-software/resources/build_config/jpo_bootloadable.ld
// 3) danilom_micropython/ports/rp2/jpo_memmap_mp.ld
#define BOOTLOADER_SIZE_KB 60

// OLED display enabled
#define OLED_ENABLED (1)

// OLED indicators enabled
#define OLED_INDICATORS_ENABLED (1)
