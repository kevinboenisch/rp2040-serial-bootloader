#pragma once

#include "jpo/jcomp/jcomp_protocol.h"

/**
 * @brief Initialize the stored flash sector pointer. Call this in main() to set the data buffer
 */
void core1_init_stored_flash_sector(uint8_t* data);

/**
 * @brief Copy the stored flash sector to the provided buffer.
 * Size must be FLASH_SECTOR_SIZE.
 */
void copy_stored_flash_sector(uint8_t* data_out);

/**
 * @brief Handle the STOR command on core1 (non blocking).
 */
bool core1_store_handler(const JCOMP_MSG msg);
