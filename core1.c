#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/flash.h"

#include "core1.h"
#include "jpo/jcomp/debug.h"

#include "stdalign.h"

// Debug tags
#define T_CORE1 0 // "core1"

// Mutex
auto_init_mutex(_state_mutex);

// Page data to store for writing, 4k in size
// Optimization: initialize in main() to keep it in RAM (top of the stack) instead of the binary (flash)
static uint8_t* _stored_flash_sector = NULL;

void core1_init_stored_flash_sector(uint8_t* data)
{
    _stored_flash_sector = data;
}

static void handle_store(const JCOMP_MSG msg)
{
    // First 4 bytes are STOR
    uint32_t offset = jcomp_msg_get_uint32(msg, 4);
    uint32_t size = jcomp_msg_get_uint32(msg, 8);
    uint8_t* data_in = msg->payload + 12;

    if (msg->payload_size - 12 != size) {
        DBG_SEND(T_ERROR, "core1 handle_store: msg->payload_size:%d != size:%d", msg->payload_size, size);
        return;
    }
	if (offset + size > FLASH_SECTOR_SIZE) {
    	DBG_SEND(T_ERROR, "core1 handle_store: offset:%d + size:%d > FLASH_SECTOR_SIZE:%d", offset, size, FLASH_SECTOR_SIZE);
		return;
	}

    mutex_enter_blocking(&_state_mutex);
    
	if (offset == 0) {
		// Clear buffer
		memset(_stored_flash_sector, 0, FLASH_SECTOR_SIZE);
	}
	// Copy data
    memcpy(_stored_flash_sector + offset, data_in, size);

    mutex_exit(&_state_mutex);
}

void copy_stored_flash_sector(uint8_t* data_out)
{
    mutex_enter_blocking(&_state_mutex);
    memcpy(data_out, _stored_flash_sector, FLASH_SECTOR_SIZE);
    mutex_exit(&_state_mutex);
}

bool core1_store_handler(const JCOMP_MSG msg)
{
	if (jcomp_msg_has_str(msg, 0, "STOR"))
    {
		DBG_SEND(T_CORE1, "core1_store_handler: STOR");
        handle_store(msg);
		return true;
	}
	return false;
}
