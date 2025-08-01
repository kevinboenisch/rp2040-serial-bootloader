/**
 * Copyright (c) 2021 Brian Starkey <stark3y@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <string.h>

#include "version.h"
#include "config.h"
#include "core1.h"
#include "oled.h"
#include "oled_indicators.h"

#include "RP2040.h"
#include "pico/time.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/structs/dma.h"
#include "hardware/structs/watchdog.h"
#include "hardware/gpio.h"
#include "hardware/resets.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/time.h" // Perf measurement

#include "jpo/jcomp/jcomp_protocol.h"
#include "jpo/jcomp/jcomp_brain.h"
#include "jpo/jcomp/debug.h"

#include "stdalign.h"

// Debug enable/disable
#define T_BOOT 0 // "boot"
#define T_TIME "time" // time output at the end of the bootloader. not much overhead, keep enabled.

// The bootloader can be entered in three ways:
//  - BOOTLOADER_ENTRY_PIN is low
//  - Watchdog scratch[5] == BOOTLOADER_ENTRY_MAGIC && scratch[6] == ~BOOTLOADER_ENTRY_MAGIC
//  - No valid image header
#define BOOTLOADER_ENTRY_PIN 15
#define BOOTLOADER_ENTRY_MAGIC 0xb105f00d

#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define UART_BAUD   921600

#define CMD_READ   (('R' << 0) | ('E' << 8) | ('A' << 16) | ('D' << 24))
#define CMD_CSUM   (('C' << 0) | ('S' << 8) | ('U' << 16) | ('M' << 24))
#define CMD_CRC    (('C' << 0) | ('R' << 8) | ('C' << 16) | ('C' << 24))
#define CMD_ERASE  (('E' << 0) | ('R' << 8) | ('A' << 16) | ('S' << 24))
#define CMD_STORE  (('S' << 0) | ('T' << 8) | ('O' << 16) | ('R' << 24)) // jpo
#define CMD_CEWR   (('C' << 0) | ('E' << 8) | ('W' << 16) | ('R' << 24)) // jpo
#define CMD_SEAL   (('S' << 0) | ('E' << 8) | ('A' << 16) | ('L' << 24))
#define CMD_GO     (('G' << 0) | ('O' << 8) | ('G' << 16) | ('O' << 24))
#define CMD_INFO   (('I' << 0) | ('N' << 8) | ('F' << 16) | ('O' << 24))

#define RSP_OK       (('O' << 0) | ('K' << 8) | ('O' << 16) | ('K' << 24))
#define RSP_ERR      (('E' << 0) | ('R' << 8) | ('R' << 16) | ('!' << 24))
#define RSP_ERR_CRC  (('C' << 0) | ('R' << 8) | ('C' << 16) | ('!' << 24))
#define RSP_NO_RESP (0)

// Bootloader error as a JCOMP_RV value
#define JCOMP_ERR_BOOTLOADER (JCOMP_ERR_CLIENT + 1)

#define IMAGE_HEADER_OFFSET (BOOTLOADER_SIZE_KB * 1024)

#define WRITE_ADDR_MIN (XIP_BASE + IMAGE_HEADER_OFFSET + FLASH_SECTOR_SIZE)
#define ERASE_ADDR_MIN (XIP_BASE + IMAGE_HEADER_OFFSET)
#define FLASH_ADDR_MAX (XIP_BASE + PICO_FLASH_SIZE_BYTES)

// Maximum size in JCOMP, minus bytes for the opcode/addr/len args
#define MAX_DATA_LEN (JCOMP_MAX_PAYLOAD_SIZE - 12)

// Perf improvement: noack support
static bool _last_msg_noack = false;

// Perf measurement
static uint32_t _start_ms = 0;
static uint32_t _flash_total_ms = 0;
static uint32_t time_ms()
{
	return to_ms_since_boot(get_absolute_time());
}


static void disable_interrupts(void)
{
	SysTick->CTRL &= ~1;

	NVIC->ICER[0] = 0xFFFFFFFF;
	NVIC->ICPR[0] = 0xFFFFFFFF;
}

static void reset_peripherals(void)
{
    reset_block(~(
            RESETS_RESET_IO_QSPI_BITS |
            RESETS_RESET_PADS_QSPI_BITS |
            RESETS_RESET_SYSCFG_BITS |
            RESETS_RESET_PLL_SYS_BITS
    ));
}

static void jump_to_vtor(uint32_t vtor)
{
	// Derived from the Leaf Labs Cortex-M3 bootloader.
	// Copyright (c) 2010 LeafLabs LLC.
	// Modified 2021 Brian Starkey <stark3y@gmail.com>
	// Originally under The MIT License
	uint32_t reset_vector = *(volatile uint32_t *)(vtor + 0x04);

	SCB->VTOR = (volatile uint32_t)(vtor);

	asm volatile("msr msp, %0"::"g"
			(*(volatile uint32_t *)vtor));
	asm volatile("bx %0"::"r" (reset_vector));
}

static uint32_t size_read(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out);
static uint32_t handle_read(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
static uint32_t size_csum(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out);
static uint32_t handle_csum(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
static uint32_t size_crc(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out);
static uint32_t handle_crc(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
static uint32_t handle_erase(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
static uint32_t handle_copyEraseWrite(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
static uint32_t handle_seal(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
static uint32_t handle_go(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
static uint32_t handle_info(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);

struct command_desc {
	uint32_t opcode;
	uint32_t nargs;
	uint32_t resp_nargs;
	uint32_t (*size)(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out);
	uint32_t (*handle)(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out);
};

const struct command_desc cmds[] = {
	{
		// READ addr len
		// OKOK [data]
		.opcode = CMD_READ,
		.nargs = 2,
		.resp_nargs = 0,
		.size = &size_read,
		.handle = &handle_read,
	},
	{
		// CSUM addr len
		// OKOK csum
		.opcode = CMD_CSUM,
		.nargs = 2,
		.resp_nargs = 1,
		.size = &size_csum,
		.handle = &handle_csum,
	},
	{
		// CRCC addr len
		// OKOK crc
		.opcode = CMD_CRC,
		.nargs = 2,
		.resp_nargs = 1,
		.size = &size_crc,
		.handle = &handle_crc,
	},
	{
		// ERAS addr len
		// OKOK
		.opcode = CMD_ERASE,
		.nargs = 2,
		.resp_nargs = 0,
		.size = NULL,
		.handle = &handle_erase,
	},
	{
		// STOR addr len [data]
		// OKOK crc (of the 4k storage buffer)
		.opcode = CMD_STORE,
		.nargs = 2,
		.resp_nargs = 1,
		.size = NULL,
		.handle = NULL,
	},
	{
		// CEWR addr len crc (of the stored buffer) progress (0-100)
		// OKOK crc (of the written page)
		// CRC! if in-memory data does not match the CRC
		// ERR! if erase fails
		.opcode = CMD_CEWR,
		.nargs = 4,
		.resp_nargs = 1,
		.size = NULL,
		.handle = &handle_copyEraseWrite,
	},
	{
		// SEAL vtor len crc
		// OKOK
		.opcode = CMD_SEAL,
		.nargs = 3,
		.resp_nargs = 0,
		.size = NULL,
		.handle = &handle_seal,
	},
	{
		// GOGO vtor
		// NO RESPONSE
		.opcode = CMD_GO,
		.nargs = 1,
		.resp_nargs = 0,
		.size = NULL,
		.handle = &handle_go,
	},
	{
		// INFO
		// OKOK flash_start flash_size erase_size write_size max_data_len
		.opcode = CMD_INFO,
		.nargs = 0,
		.resp_nargs = 5,
		.size = NULL,
		.handle = &handle_info,
	},
};
const unsigned int N_CMDS = (sizeof(cmds) / sizeof(cmds[0]));
const uint32_t MAX_NARG = 5;

static bool is_error(uint32_t status)
{
	return status == RSP_ERR;
}

static uint32_t size_read(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	uint32_t size = args_in[1];
	if (size > MAX_DATA_LEN) {
		return RSP_ERR;
	}

	// TODO: Validate address

	*data_len_out = 0;
	*resp_data_len_out = size;

	return RSP_OK;
}

static uint32_t handle_read(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	memcpy(resp_data_out, (void *)addr, size);

	return RSP_OK;
}

static uint32_t size_csum(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	if ((addr & 0x3) || (size & 0x3)) {
		// Must be aligned
		return RSP_ERR;
	}

	// TODO: Validate address

	*data_len_out = 0;
	*resp_data_len_out = 0;

	return RSP_OK;
}

static uint32_t handle_csum(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t dummy_dest;
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	int channel = dma_claim_unused_channel(true);

	dma_channel_config c = dma_channel_get_default_config(channel);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_sniff_enable(&c, true);

	dma_hw->sniff_data = 0;
	dma_sniffer_enable(channel, 0xf, true);

	dma_channel_configure(channel, &c, &dummy_dest, (void *)addr, size / 4, true);

	dma_channel_wait_for_finish_blocking(channel);

	dma_sniffer_disable();
	dma_channel_unclaim(channel);

	*resp_args_out = dma_hw->sniff_data;

	return RSP_OK;
}

static uint32_t size_crc(uint32_t *args_in, uint32_t *data_len_out, uint32_t *resp_data_len_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	if ((addr & 0x3) || (size & 0x3)) {
		// Must be aligned
		return RSP_ERR;
	}

	// TODO: Validate address

	*data_len_out = 0;
	*resp_data_len_out = 0;

	return RSP_OK;
}

// ptr must be 4-byte aligned and len must be a multiple of 4
static uint32_t calc_crc32(void *ptr, uint32_t len)
{
	uint32_t dummy_dest, crc;

	int channel = dma_claim_unused_channel(true);
	dma_channel_config c = dma_channel_get_default_config(channel);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_sniff_enable(&c, true);

	// Seed the CRC calculation
	dma_hw->sniff_data = 0xffffffff;

	// Mode 1, then bit-reverse the result gives the same result as
	// golang's IEEE802.3 implementation
	dma_sniffer_enable(channel, 0x1, true);
	dma_hw->sniff_ctrl |= DMA_SNIFF_CTRL_OUT_REV_BITS;

	dma_channel_configure(channel, &c, &dummy_dest, ptr, len / 4, true);

	dma_channel_wait_for_finish_blocking(channel);

	// Read the result before resetting
	crc = dma_hw->sniff_data ^ 0xffffffff;

	dma_sniffer_disable();
	dma_channel_unclaim(channel);

	return crc;
}

static uint32_t handle_crc(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];

	resp_args_out[0] = calc_crc32((void *)addr, size);

	return RSP_OK;
}


static uint32_t do_erase(uint32_t addr, uint32_t size)
{
	if ((addr < ERASE_ADDR_MIN) || (addr + size > FLASH_ADDR_MAX)) {
		// Outside flash
		return RSP_ERR;
	}

	if ((addr & (FLASH_SECTOR_SIZE - 1)) || (size & (FLASH_SECTOR_SIZE - 1))) {
		// Must be aligned
		return RSP_ERR;
	}

	uint32_t start_ms = time_ms();
	flash_range_erase(addr - XIP_BASE, size);
	_flash_total_ms += time_ms() - start_ms;

	return RSP_OK;
}

static uint32_t handle_erase(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];
	
	return do_erase(addr, size);
}

static void do_write(uint32_t addr, uint32_t size, uint8_t* data_in)
{
	uint32_t start_ms = time_ms();
	flash_range_program(addr - XIP_BASE, data_in, size);
	_flash_total_ms += time_ms() - start_ms;
}

static uint32_t handle_copyEraseWrite(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	uint32_t addr = args_in[0];
	uint32_t size = args_in[1];
	uint32_t expected_crc = args_in[2];
	uint32_t progress = args_in[3]; // 0-100
	(void)progress; // unused if OLED_ENABLED is false

	//DBG_SEND(T_BOOT, "handle_copyEraseWrite addr: 0x% xsize: %d expected_crc:0x%x", addr, size, expected_crc);

	// Page data to write, 4k in size
	alignas(4) uint8_t flash_sector_to_write[FLASH_SECTOR_SIZE] = {0};
	copy_stored_flash_sector(flash_sector_to_write);

	// Verify the CRC of the in-memory data (flash_sector to write)
	uint32_t mem_crc = calc_crc32(flash_sector_to_write, FLASH_SECTOR_SIZE);
	if (mem_crc != expected_crc) {
		DBG_SEND(T_WARN, "handle_copyEraseWrite addr: 0x%x size: %d expected_crc:0x%x != mem_crc:0x%x", addr, size, expected_crc, mem_crc);
		return RSP_ERR_CRC;
	}

	//DBG_SEND(T_BOOT, "cewr: do_erase addr: %d size: %d", addr, size);
	uint32_t resp = do_erase(addr, size);
	if (resp != RSP_OK) {
		DBG_SEND(T_ERROR, "handle_copyEraseWrite addr: 0x%x size: %d, erase failed.", addr, size);
		return resp;
	}

	uint32_t offset = 0;
	while (offset < size)
	{
		uint32_t write_size = FLASH_PAGE_SIZE;
		if (size - offset < FLASH_PAGE_SIZE) {
			write_size = size - offset;
		}

		//DBG_SEND(T_BOOT, "cewr write: addr: %d size: %d offset: %d", addr, write_size, offset);
		do_write(addr + offset, write_size, flash_sector_to_write + offset);

		offset += write_size;
	}
	
	// return CRC of actual written data (NOT flash_sector_to_write)
	resp_args_out[0] = calc_crc32((void *)addr, size);

#if OLED_ENABLED
	oled_draw_progress_bar(progress);
#endif

	return RSP_OK;
}

struct image_header { 
	uint32_t vtor;
	uint32_t size;
	uint32_t crc;
	uint8_t pad[FLASH_PAGE_SIZE - (3 * 4)];
};
static_assert(sizeof(struct image_header) == FLASH_PAGE_SIZE, "image_header must be FLASH_PAGE_SIZE bytes");

static bool image_header_ok(struct image_header *hdr)
{
	uint32_t *vtor = (uint32_t *)hdr->vtor;

	uint32_t calc = calc_crc32((void *)hdr->vtor, hdr->size);

	// CRC has to match
	if (calc != hdr->crc) {
		return false;
	}

	// Stack pointer needs to be in RAM
	if (vtor[0] < SRAM_BASE) {
		return false;
	}

	// Reset vector should be in the image, and thumb (bit 0 set)
	if ((vtor[1] < hdr->vtor) || (vtor[1] > hdr->vtor + hdr->size) || !(vtor[1] & 1)) {
		return false;
	}

	// Looks OK.
	return true;
}


static uint32_t handle_seal(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	struct image_header hdr = {
		.vtor = args_in[0],
		.size = args_in[1],
		.crc = args_in[2],
	};

	if ((hdr.vtor & 0xff) || (hdr.size & 0x3)) {
		// Must be aligned
		return RSP_ERR;
	}

	if (!image_header_ok(&hdr)) {
		return RSP_ERR;
	}

	uint32_t start_ms = time_ms();
	flash_range_erase(IMAGE_HEADER_OFFSET, FLASH_SECTOR_SIZE);
	flash_range_program(IMAGE_HEADER_OFFSET, (const uint8_t *)&hdr, sizeof(hdr));
	_flash_total_ms += time_ms() - start_ms;

	struct image_header *check = (struct image_header *)(XIP_BASE + IMAGE_HEADER_OFFSET);
	if (memcmp(&hdr, check, sizeof(hdr))) {
		return RSP_ERR;
	}

	// Perf info
	uint32_t total_ms = time_ms() - _start_ms;
	DBG_SEND(T_TIME, "time (ms): total: %d flash: %d", total_ms, _flash_total_ms);

	return RSP_OK;
}

static uint32_t handle_go(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	disable_interrupts();

	reset_peripherals();

	jump_to_vtor(args_in[0]);

	while(1);

	return RSP_ERR;
}

static uint32_t handle_info(uint32_t *args_in, uint8_t *data_in, uint32_t *resp_args_out, uint8_t *resp_data_out)
{
	_start_ms = time_ms();
	_flash_total_ms = 0;

	resp_args_out[0] = WRITE_ADDR_MIN;
	resp_args_out[1] = (XIP_BASE + PICO_FLASH_SIZE_BYTES) - WRITE_ADDR_MIN;
	resp_args_out[2] = FLASH_SECTOR_SIZE;
	resp_args_out[3] = FLASH_PAGE_SIZE;
	resp_args_out[4] = MAX_DATA_LEN;

	return RSP_OK;
}

static const struct command_desc *find_command_desc(uint32_t opcode)
{
	unsigned int i;

	for (i = 0; i < N_CMDS; i++) {
		if (cmds[i].opcode == opcode) {
			return &cmds[i];
		}
	}

	return NULL;
}

struct cmd_context {
	uint8_t *uart_buf; // message payload (in/out)
	const struct command_desc *desc;
	uint32_t opcode;
	uint32_t status;
	uint32_t *args;
	uint8_t *data;
	uint32_t *resp_args;
	uint8_t *resp_data;
	uint32_t data_len;
	uint32_t resp_data_len;
};


static JCOMP_RV read_message(struct cmd_context *ctx, JCOMP_MSG in_msg) 
{
	size_t size = 0;
	uint16_t pos = 0; // position
	JCOMP_RV err = JCOMP_OK;

	_last_msg_noack = in_msg->type == JCOMP_MSG_TYPE_EVENT_NOACK;

	// Read opcode: state_read_opcode(ctx)
	//X serial_read_blocking((uint8_t *)&ctx->opcode, sizeof(ctx->opcode));
	err = jcomp_msg_get_bytes(in_msg, pos, 
		(uint8_t*)&ctx->opcode,
		sizeof(ctx->opcode), 
		&size);
	pos += size;

	if (err) {
		DBG_SEND(T_ERROR, "Failed to read opcode: %d", err);
		return err;
	}

	// Read args: state_read_args(ctx)
	const struct command_desc *desc = find_command_desc(ctx->opcode);
	if (!desc) {
		DBG_SEND(T_ERROR, "Failed to find cmd desc for opcode: %d", ctx->opcode);
		//X TODO: Error handler that can do args?
		ctx->status = RSP_ERR;
		return JCOMP_ERR_BOOTLOADER;
	}

	ctx->desc = desc;
	ctx->args = (uint32_t *)(ctx->uart_buf + sizeof(ctx->opcode));
	ctx->data = (uint8_t *)(ctx->args + desc->nargs);
	ctx->resp_args = ctx->args;
	ctx->resp_data = (uint8_t *)(ctx->resp_args + desc->resp_nargs);

	//X serial_read_blocking((uint8_t *)ctx->args, sizeof(*ctx->args) * desc->nargs);
	err = jcomp_msg_get_bytes(in_msg, pos,
		(uint8_t*)ctx->args, 
		sizeof(*ctx->args) * desc->nargs, 
		&size);
	pos += size;

	if (err) {
		DBG_SEND(T_ERROR, "Failed to read args: %d", err);
		return err;
	}

	// Read data: state_read_data(ctx)
	//X const struct command_desc *desc = ctx->desc;
	if (desc->size) {
		ctx->status = desc->size(ctx->args, &ctx->data_len, &ctx->resp_data_len);
		if (is_error(ctx->status)) {
			DBG_SEND(T_ERROR, "Failed to find data size, ctx->status: %d", ctx->status);
			return JCOMP_ERR_BOOTLOADER;
		}
	} else {
		ctx->data_len = 0;
		ctx->resp_data_len = 0;
	}

	//X TODO: Check sizes
	//X serial_read_blocking((uint8_t *)ctx->data, ctx->data_len);
	err = jcomp_msg_get_bytes(in_msg, pos,
		(uint8_t*)ctx->data, 
		ctx->data_len, 
		&size);
	pos += size;

	if (err) {
		DBG_SEND(T_ERROR, "Failed to read data: %d", err);
		return err;
	}

	return JCOMP_OK;
}

static JCOMP_RV send_msg_core(JCOMP_MSG resp, const uint8_t* payload, size_t len) {
	JCOMP_RV err = jcomp_msg_set_bytes(resp, 0, payload, len);
	if (err) {
		DBG_SEND(T_ERROR, "Failed to set response bytes: %d", err);
		return err;
	}
	err = jcomp_send_msg(resp);
	if (err) {
		DBG_SEND(T_ERROR, "Failed to send response: %d", err);
		return err;
	}
	return JCOMP_OK;
}
static JCOMP_RV send_response(uint8_t request_id, const uint8_t* payload, size_t len) {
	// Create either a response or a noack event (depending on the last message)
	uint8_t resp_buf[JCOMP_MSG_BUF_SIZE(len)];
	JCOMP_MSG resp = NULL;
	if (_last_msg_noack) {
		resp = jcomp_create_event_noack(len, resp_buf, sizeof(resp_buf));
	}
	else {
		resp = jcomp_create_response(request_id, len, resp_buf, sizeof(resp_buf));
	}

	if (!resp) {
		DBG_SEND(T_ERROR, "Failed to create response");
		return JCOMP_ERR_BOOTLOADER;
	}
	JCOMP_RV err = send_msg_core(resp, payload, len);
	return err;
}
static JCOMP_RV send_error(uint8_t request_id) {
	uint32_t data = RSP_ERR;
	return send_response(request_id, (uint8_t*)&data, sizeof(data));
}


static JCOMP_RV handle_data(struct cmd_context *ctx, uint8_t request_id)
{
	// Handle data: state_handle_data(ctx)
	const struct command_desc *desc = ctx->desc;

	if (desc->handle) {
		ctx->status = desc->handle(ctx->args, ctx->data, ctx->resp_args, ctx->resp_data);
		if (is_error(ctx->status)) {
			DBG_SEND(T_ERROR, "Failed to handle data, ctx->status: 0x%x", ctx->status);
			return JCOMP_ERR_BOOTLOADER;
		}
	} else {
		DBG_SEND(T_ERROR, "No handle function for opcode: 0x%x", ctx->opcode);
		ctx->status = RSP_OK;
	}

	size_t resp_len = sizeof(ctx->status) + (sizeof(*ctx->resp_args) * desc->resp_nargs) + ctx->resp_data_len;
	memcpy(ctx->uart_buf, &ctx->status, sizeof(ctx->status));
	
	// Special case, no response
	if (ctx->status == RSP_NO_RESP) {
		return JCOMP_OK;
	}

	// Send a response
	JCOMP_RV err = send_response(request_id, ctx->uart_buf, resp_len);
	return err;
}

static bool should_stay_in_bootloader()
{
	bool wd_says_so = (watchdog_hw->scratch[5] == BOOTLOADER_ENTRY_MAGIC) &&
		(watchdog_hw->scratch[6] == ~BOOTLOADER_ENTRY_MAGIC);

	return !gpio_get(BOOTLOADER_ENTRY_PIN) || wd_says_so;
}

void process_message(struct cmd_context *ctx, JCOMP_MSG in_msg) {
	JCOMP_RV err = read_message(ctx, in_msg);
	if (err) {
		DBG_SEND(T_ERROR, "Failed to read message rv: %d", err);
		send_error(jcomp_msg_id(in_msg));
		return;
	}
	err = handle_data(ctx, jcomp_msg_id(in_msg));
	if (err) {
		DBG_SEND(T_ERROR, "Failed to handle data rv: %d", err);
		send_error(jcomp_msg_id(in_msg));
		return;
	}
	// Done
}

void init_serial(void) {
    // USB
	// BUG FIX: delay mitigates the "Open (SetCommState): Unknown error code 31" bug,
	// which requires the user to physically reset the brain. 
	// It still happens, but with no delay it was happening after every soft (watchdog) reboot. 
	// see https://www.pivotaltracker.com/story/show/185554935
    // NOTE: use the same fix in the JPO bootloader

	// 100 ms is not enough.
    // 200 ms works most of the time, but is less reliable after BOOTSEL. 
	sleep_ms(400);
	stdio_init_all();
}

// in radio code see device_info.hpp
#define CMD_SET_JOY_REPORT_FAR  "fSETJOY-"
#define JOYC_OFF        "off-"
void disable_joystick_message_flood()
{
    uint8_t id = jcomp_get_next_cmd_id_bradio();
    JCOMP_CREATE_MSG(cmd_joy_on, JCOMP_MSG_TYPE_COMMAND_EVENT, id, 12);

    jcomp_msg_append_str(cmd_joy_on, CMD_SET_JOY_REPORT_FAR);
    jcomp_msg_append_str(cmd_joy_on, JOYC_OFF);
    jcomp_send_msg(cmd_joy_on);
    // What do do with the rv?
}

int main(void)
{
	// Binary size optimization (keep buffers in RAM to save flash space)
	// !! DO NOT MOVE/REFACTOR !! 
	// MUST BE IN main(), on top of the stack.
	alignas(4) uint8_t stored_flash_sector[FLASH_SECTOR_SIZE] = {0};
	core1_init_stored_flash_sector(stored_flash_sector);
#if OLED_ENABLED
	OLED_VTable_Obj oled_driver = {0};
#endif
#if OLED_INDICATORS_ENABLED
	OLED_VTable_Obj indicators_driver = {0};
#endif
	// End optimization


	gpio_init(BOOTLOADER_ENTRY_PIN);
	gpio_pull_up(BOOTLOADER_ENTRY_PIN);
	gpio_set_dir(BOOTLOADER_ENTRY_PIN, 0);

	sleep_ms(10);

	struct image_header *hdr = (struct image_header *)(XIP_BASE + IMAGE_HEADER_OFFSET);

	if (!should_stay_in_bootloader() && image_header_ok(hdr)) {
		uint32_t vtor = *((uint32_t *)(XIP_BASE + IMAGE_HEADER_OFFSET));
		disable_interrupts();
		reset_peripherals();
		jump_to_vtor(vtor);
	}

	init_serial();

	jcomp_init();
	jcomp_set_env_type("BOOT:" VERSION_TIMESTAMP);

#if OLED_INDICATORS_ENABLED
	// Should be done before oled_init(), as the latter clears the screen and renders indicators 
	indicators_init(&indicators_driver);
#endif
#if OLED_ENABLED
	oled_init(&oled_driver);
#endif

	disable_joystick_message_flood();

	jcomp_add_core1_handler(core1_store_handler);

	struct cmd_context ctx;
	uint8_t uart_buf[(sizeof(uint32_t) * (1 + MAX_NARG)) + MAX_DATA_LEN];
	ctx.uart_buf = uart_buf;

	while (true) 
	{
		JCOMP_RECEIVE_MSG(in_msg, rv, 30000);
		if (rv == JCOMP_OK) {
			process_message(&ctx, in_msg);
		}
		else {
			// Error receiving. Not much we can do.
			// Timeout is ok, just wait again
		}
	}

	return 0;
}
