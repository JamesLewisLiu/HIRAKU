// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
/*
 * PlayStation SIO1 as a UART (serial bits on txd/rxd, with RTS/CTS and DTR/DSR), from
 * windyfairy's MAME fork (branch bemani_k573msu_wip, 928dfcb, src/devices/cpu/psx/sio.cpp),
 * where it replaces MAME's SIO1. HIRAKU: a device of its own (psxsio1_uart_device), used in
 * place of MAME's SIO1 only on 573 boards with a multi-session unit (the unit talks to the game
 * through it), brought to current MAME's API. MAME's SIO1 is unchanged.
 */

#ifndef MAME_CPU_PSX_SIO_UART_H
#define MAME_CPU_PSX_SIO_UART_H

#pragma once

#include "diserial.h"

DECLARE_DEVICE_TYPE(PSX_SIO1_UART, psxsio1_uart_device)

class psxsio1_uart_device : public device_t,
	public device_serial_interface
{
public:
	// construction/destruction
	psxsio1_uart_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock);

	// configuration helpers
	auto irq_handler() { return m_irq_handler.bind(); }
	auto txd_handler() { return m_txd_handler.bind(); }
	auto dtr_handler() { return m_dtr_handler.bind(); }
	auto rts_handler() { return m_rts_handler.bind(); }

	uint8_t data_r();
	void data_w(uint8_t data);
	uint16_t status_r();

	virtual uint32_t read(offs_t offset, uint32_t mem_mask);
	virtual void write(offs_t offset, uint32_t data, uint32_t mem_mask);

	void write_rxd(int state);
	void write_cts(int state);
	void write_dsr(int state);

protected:
	psxsio1_uart_device(
		const machine_config& mconfig,
		device_type type,
		const char* tag,
		device_t* owner,
		uint32_t clock);

	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

	TIMER_CALLBACK_MEMBER(sio_tick);

	void sio_timer_adjust();

	void command_w(uint16_t data);
	void mode_w(uint16_t data);

	void receive_character(uint8_t ch);

	void transmit_clock();
	bool is_tx_enabled() const;
	void check_for_tx_start();
	void start_tx();

private:
	devcb_write_line m_irq_handler;
	devcb_write_line m_txd_handler;
	devcb_write_line m_dtr_handler;
	devcb_write_line m_rts_handler;

	uint16_t m_status;
	uint16_t m_control;
	uint16_t m_mode;
	uint32_t m_baud;
	bool m_delayed_tx_en;

	bool m_cts;
	bool m_dsr;
	bool m_rxd;
	int m_txc_count;
	int m_br_factor;

	/* data being received */
	uint8_t m_rx_data;
	// HIRAKU: the PS1 SIO's 8-byte receive FIFO (its RX interrupt mode counts 1, 2, 4 or 8 bytes);
	// the fork kept one byte, and a busy 573 then lost the unit's bytes (an overrun)
	uint8_t m_rx_fifo[8];
	uint8_t m_rx_head, m_rx_count;
	/* tx buffer */
	uint8_t m_tx_data;
	// count of rxd bits
	u8 m_rxd_bits;
	u8 m_data_bits_count;

	emu_timer* m_timer;
};


#endif // MAME_CPU_PSX_SIO_UART_H
