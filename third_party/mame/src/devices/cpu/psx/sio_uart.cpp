// license:BSD-3-Clause
// copyright-holders:smf, windyfairy
/*
 * PlayStation SIO1 as a UART: see sio_uart.h. From windyfairy's MAME fork (branch
 * bemani_k573msu_wip, 928dfcb): its psxsio1_device, renamed, brought to current MAME's API.
 */

#include "emu.h"

#include <algorithm>
#include <iterator>
#include "sio_uart.h"

// the fork's register bits (its sio.h)
#define SIO_BUF_SIZE ( 8 )
#define SIO_STATUS_TX_RDY ( 1 << 0 )
#define SIO_STATUS_RX_RDY ( 1 << 1 )
#define SIO_STATUS_TX_EMPTY ( 1 << 2 )
#define SIO_STATUS_PARITY_ERROR ( 1 << 3 )
#define SIO_STATUS_OVERRUN_ERROR ( 1 << 4 )
#define SIO_STATUS_OVERRUN ( 1 << 4 )
#define SIO_STATUS_FRAMING_ERROR ( 1 << 5 )
#define SIO_STATUS_RX ( 1 << 6 )
#define SIO_STATUS_DSR ( 1 << 7 )
#define SIO_STATUS_CTS ( 1 << 8 )
#define SIO_STATUS_IRQ ( 1 << 9 )
#define SIO_CONTROL_BIT_TXEN 0
#define SIO_CONTROL_BIT_DTR 1
#define SIO_CONTROL_BIT_RXEN 2
#define SIO_CONTROL_BIT_TX 3
#define SIO_CONTROL_BIT_IACK 4
#define SIO_CONTROL_BIT_RTS 5
#define SIO_CONTROL_BIT_RESET 6
#define SIO_CONTROL_BIT_TX_IENA 10
#define SIO_CONTROL_BIT_RX_IENA 11
#define SIO_CONTROL_BIT_DSR_IENA 12
#define SIO_CONTROL_TX_ENA ( 1 << SIO_CONTROL_BIT_TXEN )
#define SIO_CONTROL_TXEN ( 1 << SIO_CONTROL_BIT_TXEN )
#define SIO_CONTROL_DTR ( 1 << SIO_CONTROL_BIT_DTR )
#define SIO_CONTROL_RXEN ( 1 << SIO_CONTROL_BIT_RXEN )
#define SIO_CONTROL_TX ( 1 << SIO_CONTROL_BIT_TX )
#define SIO_CONTROL_IACK ( 1 << SIO_CONTROL_BIT_IACK )
#define SIO_CONTROL_RTS ( 1 << SIO_CONTROL_BIT_RTS )
#define SIO_CONTROL_RESET ( 1 << SIO_CONTROL_BIT_RESET )
#define SIO_CONTROL_RX_IMODE (( 1 << 8 ) | ( 1 << 9 ))
#define SIO_CONTROL_TX_IENA ( 1 << SIO_CONTROL_BIT_TX_IENA )
#define SIO_CONTROL_RX_IENA ( 1 << SIO_CONTROL_BIT_RX_IENA )
#define SIO_CONTROL_DSR_IENA ( 1 << SIO_CONTROL_BIT_DSR_IENA )

#define LOG_STAT    (1U << 1)
#define LOG_COM     (1U << 2)
#define LOG_MODE    (1U << 3)
#define LOG_BITS    (1U << 4)
#define LOG_GENERAL    (1U << 5)

#define VERBOSE (0)//LOG_STAT | LOG_MODE | LOG_GENERAL | LOG_BITS | LOG_COM)

#include "logmacro.h"

#define LOGGENERAL(...)  LOGMASKED(LOG_GENERAL,  __VA_ARGS__)
#define LOGSTAT(...)  LOGMASKED(LOG_STAT,  __VA_ARGS__)
#define LOGCOM(...)   LOGMASKED(LOG_COM,   __VA_ARGS__)
#define LOGMODE(...)  LOGMASKED(LOG_MODE,  __VA_ARGS__)
#define LOGBITS(...)  LOGMASKED(LOG_BITS,  __VA_ARGS__)

DEFINE_DEVICE_TYPE(PSX_SIO1_UART, psxsio1_uart_device, "psxsio1_uart", "Sony PSX SIO-1 (UART)")

// SIO1
psxsio1_uart_device::psxsio1_uart_device(
	const machine_config& mconfig,
	device_type type,
	const char* tag,
	device_t* owner,
	uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	m_irq_handler(*this),
	m_txd_handler(*this),
	m_dtr_handler(*this),
	m_rts_handler(*this),
	m_cts(1),
	m_dsr(1),
	m_rxd(1),
	m_timer(nullptr)
{
}

psxsio1_uart_device::psxsio1_uart_device(const machine_config& mconfig, const char* tag, device_t* owner, uint32_t clock) :
	psxsio1_uart_device(mconfig, PSX_SIO1_UART, tag, owner, clock)
{
}


//-------------------------------------------------
//  device_resolve_objects - resolve objects that
//  may be needed for other devices to set
//  initial conditions at start time
//-------------------------------------------------


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void psxsio1_uart_device::device_start()
{
	m_timer = timer_alloc(FUNC(psxsio1_uart_device::sio_tick), this);

	save_item(NAME(m_status));
	save_item(NAME(m_control));
	save_item(NAME(m_mode));
	save_item(NAME(m_delayed_tx_en));
	save_item(NAME(m_cts));
	save_item(NAME(m_dsr));
	save_item(NAME(m_rxd));
	save_item(NAME(m_br_factor));
	save_item(NAME(m_rx_data));
	save_item(NAME(m_rx_fifo));
	save_item(NAME(m_rx_head));
	save_item(NAME(m_rx_count));
	save_item(NAME(m_tx_data));
	save_item(NAME(m_rxd_bits));
	save_item(NAME(m_txc_count));
	save_item(NAME(m_data_bits_count));
}




/*-------------------------------------------------
	receive_clock
-------------------------------------------------*/

void psxsio1_uart_device::sio_timer_adjust()
{
	attotime n_time;

	if (m_baud != 0 && m_br_factor != 0)
	{
		n_time = attotime::from_hz(44100 * 768) * (m_br_factor * m_baud);
		//LOGGENERAL("sio_timer_adjust( %s ) = %s ( %d x %d )\n", tag(), n_time.as_string(), m_br_factor, m_baud);
	}
	else
	{
		n_time = attotime::never;

		LOGGENERAL("sio_timer_adjust( %s ) invalid baud rate ( %d x %d )\n", tag(), m_br_factor, m_baud);
	}

	m_timer->adjust(n_time);
}


TIMER_CALLBACK_MEMBER(psxsio1_uart_device::sio_tick)
{
	transmit_clock();
	sio_timer_adjust();
}

/*-------------------------------------------------
	is_tx_enabled
-------------------------------------------------*/
bool psxsio1_uart_device::is_tx_enabled() const
{
	return BIT(m_control, SIO_CONTROL_BIT_TXEN);// && !m_cts;
}

/*-------------------------------------------------
	check_for_tx_start
-------------------------------------------------*/
void psxsio1_uart_device::check_for_tx_start()
{
	if (is_tx_enabled() && (m_status & (SIO_STATUS_TX_EMPTY | SIO_STATUS_TX_RDY)) == SIO_STATUS_TX_EMPTY)
	{
		start_tx();
	}
}

/*-------------------------------------------------
	start_tx
-------------------------------------------------*/
void psxsio1_uart_device::start_tx()
{
	LOGGENERAL("start_tx %02x\n", m_tx_data);
	transmit_register_setup(m_tx_data);
	m_status &= ~SIO_STATUS_TX_EMPTY;
}

/*-------------------------------------------------
	transmit_clock
-------------------------------------------------*/

void psxsio1_uart_device::transmit_clock()
{
	/* if diserial has bits to send, make them so */
	if (!is_transmit_register_empty())
	{
		uint8_t data = transmit_register_get_data_bit();
		LOGBITS("8251: Tx Present a %d, %04x\n", data, m_status);
		m_txd_handler(data);

		if (is_transmit_register_empty())
		{
			m_status |= SIO_STATUS_TX_EMPTY;
			m_status |= SIO_STATUS_TX_RDY;
		}
	}

	if (!(m_status & SIO_STATUS_IRQ) && (m_status & SIO_STATUS_TX_EMPTY) && (m_status & SIO_STATUS_TX_RDY) && BIT(m_control, SIO_CONTROL_BIT_TX_IENA))
	{
		m_status |= SIO_STATUS_IRQ;
		m_irq_handler(1);
	}
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void psxsio1_uart_device::device_reset()
{
	LOGGENERAL("SIO1: Reset\n");

	/* what is the default setup when the 8251 has been reset??? */

	/* i8251 datasheet explains the state of tx pin at reset */
	/* tx is set to 1 */
	m_txd_handler(1);

	/* assumption */
	m_rts_handler(1);
	m_dtr_handler(1);

	transmit_register_reset();
	receive_register_reset();

	/* no character to read by cpu */
	/* transmitter is ready and is empty */
	m_status = SIO_STATUS_TX_EMPTY | SIO_STATUS_TX_RDY;
	LOGSTAT("status is reset to %02x\n", m_status);
	m_mode = 0;
	m_control = 0;
	m_rx_data = 0;
	std::fill(std::begin(m_rx_fifo), std::end(m_rx_fifo), 0);
	m_rx_head = m_rx_count = 0;
	m_tx_data = 0;
	m_br_factor = 1;
	m_txc_count = 0;

	m_cts = 1;
}



/*-------------------------------------------------
	control_w
-------------------------------------------------*/

void psxsio1_uart_device::command_w(uint16_t data)
{
	/* command */
	m_control = data;

	LOGCOM("SIO1: Command byte: %02x\n", data);
	LOGCOM(" Tx enable: %d\n", data & SIO_CONTROL_TXEN ? 1 : 0); // bit 0: 0 = transmit disable 1 = transmit enable
	LOGCOM(" DTR      : %d\n", data & SIO_CONTROL_DTR ? 1 : 0); // bit 1: 0 = /DTR set to 1    1 = /DTR set to 0
	LOGCOM(" Rx enable: %d\n", data & SIO_CONTROL_RXEN ? 1 : 0); // bit 2: 0 = receive disable  1 = receive enable
	LOGCOM(" TX Level : %d\n", data & SIO_CONTROL_TX ? 1 : 0); // bit 3: 0 = Normal, 1 = Inverted, during Inactivity & Stop bits
	LOGCOM(" Int Ack  : %d\n", data & SIO_CONTROL_IACK ? 1 : 0); // bit 4: 0 = No change, 1 = Reset SIO_STAT.Bits 3,4,5,9
	LOGCOM(" RTS      : %d\n", data & SIO_CONTROL_RTS ? 1 : 0); // bit 5: 0 = /RTS set to 1    1 = /RTS set to 0
	LOGCOM(" Reset    : %d\n", data & SIO_CONTROL_RESET ? 1 : 0); // bit 6: 0 = normal operation 1 = internal reset
	LOGCOM(" TX IENA  : %d\n", data & SIO_CONTROL_TX_IENA ? 1 : 0);
	LOGCOM(" RX IENA  : %d\n", data & SIO_CONTROL_RX_IENA ? 1 : 0);
	LOGCOM(" DSR IENA : %d\n", data & SIO_CONTROL_DSR_IENA ? 1 : 0);

	m_rts_handler(!BIT(data, SIO_CONTROL_BIT_RTS));
	m_dtr_handler(!BIT(data, SIO_CONTROL_BIT_DTR));

	if (BIT(data, SIO_CONTROL_BIT_RESET))
	{
		m_status &= (SIO_STATUS_RX_RDY | SIO_STATUS_DSR);
		m_status |= SIO_STATUS_TX_EMPTY | SIO_STATUS_TX_RDY;
		m_control &= ~SIO_CONTROL_RESET; // TODO: Should this be reset here?
	}

	if (BIT(data, SIO_CONTROL_BIT_IACK))
	{
		m_status &= ~(SIO_STATUS_OVERRUN_ERROR | SIO_STATUS_PARITY_ERROR | SIO_STATUS_FRAMING_ERROR | SIO_STATUS_IRQ);
		m_control &= ~SIO_CONTROL_IACK;
		m_irq_handler(0);
	}

	m_dtr_handler((m_control & SIO_CONTROL_DTR) == 0);

	if (BIT(data, SIO_CONTROL_BIT_TX))
		m_txd_handler(0);
}

void psxsio1_uart_device::mode_w(uint16_t data)
{
	LOGGENERAL("SIO1: Mode byte = %02X\n", data);

	m_mode = data;

	m_data_bits_count = ((data >> 2) & 0x03) + 5;
	LOGGENERAL("Character length: %d\n", m_data_bits_count);

	parity_t parity = PARITY_NONE;
	switch (data & 0x30)
	{
	case 0x10:
		LOGGENERAL("Enable ODD parity checking.\n");
		parity = PARITY_ODD;
		break;

	case 0x30:
		LOGGENERAL("Enable EVEN parity checking.\n");
		parity = PARITY_EVEN;
		break;

	default:
		LOGGENERAL("Disable parity check.\n");
	}

	stop_bits_t stop_bits = STOP_BITS_0;
	m_br_factor = 1;

	switch (data & 0xc0)
	{
	case 0x40:
		stop_bits = STOP_BITS_1;
		LOGGENERAL("stop bit: 1 bit\n");
		break;

	case 0x80:
		stop_bits = STOP_BITS_1_5;
		LOGGENERAL("stop bit: 1.5 bits\n");
		break;

	case 0xc0:
		stop_bits = STOP_BITS_2;
		LOGGENERAL("stop bit: 2 bits\n");
		break;

	default:
		LOGGENERAL("stop bit: inhibit\n");
		break;
	}

	set_data_frame(1, m_data_bits_count, parity, stop_bits);

	switch (data & 0x03)
	{
	case 1:
		m_br_factor = 1;
		break;

	case 2:
		m_br_factor = 16;
		break;

	case 3:
		m_br_factor = 64;
		break;

	default:
		m_br_factor = 0;
		break;
	}

	receive_register_reset();
	m_txc_count = 0;

	sio_timer_adjust();
}



/*-------------------------------------------------
	status_r
-------------------------------------------------*/

uint16_t psxsio1_uart_device::status_r()
{
	uint16_t status = (m_cts << 8) | (m_dsr << 7) | m_status;
	return status;
}



/*-------------------------------------------------
	data_w
-------------------------------------------------*/

// HIRAKU: HK_MSU_TRACE=1 prints the bytes on the link
static const bool hk_trace = getenv("HK_MSU_TRACE") != nullptr;

void psxsio1_uart_device::data_w(uint8_t data)
{
	if (hk_trace) fprintf(stderr, "sio1 tx %02x%s at %.6f\n", data, BIT(m_control, SIO_CONTROL_BIT_TXEN) ? "" : " (tx off)", machine().time().as_double());
	if (!BIT(m_control, SIO_CONTROL_BIT_TXEN))
		return;

	m_tx_data = data;

	m_status &= ~SIO_STATUS_TX_RDY;

	LOGBITS("TX data_w %02x\n", data);
	check_for_tx_start();
}



/*-------------------------------------------------
	receive_character - called when last
	bit of data has been received
-------------------------------------------------*/

void psxsio1_uart_device::receive_character(uint8_t ch)
{
	if (hk_trace) fprintf(stderr, "sio1 rx %02x at %.6f\n", ch, machine().time().as_double());
	LOGBITS("RX receive_character %02x\n", ch);

	LOGSTAT("status RX READY test %02x\n", m_status);
	/* HIRAKU: into the FIFO; full, and another has arrived: an overrun, and it is lost */
	if (m_rx_count == std::size(m_rx_fifo))
	{
		if (hk_trace) fprintf(stderr, "sio1 overrun: %02x lost at %.6f\n", ch, machine().time().as_double());
		m_status |= SIO_STATUS_OVERRUN_ERROR;
		LOGSTAT("status overrun set\n");
	}
	else
	{
		m_rx_fifo[(m_rx_head + m_rx_count++) % std::size(m_rx_fifo)] = ch;
		if (hk_trace && m_rx_count > 1) fprintf(stderr, "sio1 fifo %u bytes at %.6f\n", m_rx_count, machine().time().as_double());
	}
	m_rx_data = m_rx_fifo[m_rx_head];

	LOGSTAT("status pre RX READY set %02x\n", m_status);
	m_status |= SIO_STATUS_RX_RDY;
	LOGSTAT("status post RX READY set %02x\n", m_status);
}



/*-------------------------------------------------
	data_r - read data
-------------------------------------------------*/

uint8_t psxsio1_uart_device::data_r()
{
	LOGGENERAL("read data: %02x, STATUS=%02x\n", m_rx_data, m_status);
	/* HIRAKU: the FIFO's oldest; RX_RDY stays while more are waiting */
	const uint8_t data = m_rx_count ? m_rx_fifo[m_rx_head] : m_rx_data;
	if (!machine().side_effects_disabled() && m_rx_count)
	{
		m_rx_head = (m_rx_head + 1) % std::size(m_rx_fifo);
		m_rx_count--;
		if (m_rx_count)
			m_rx_data = m_rx_fifo[m_rx_head];
	}
	if (!machine().side_effects_disabled() && !m_rx_count)
	{
		m_status &= ~SIO_STATUS_RX_RDY;
		LOGSTAT("status RX_READY cleared\n");
	}
	return data;
}


uint32_t psxsio1_uart_device::read(offs_t offset, uint32_t mem_mask)
{
	uint32_t data = 0;

	switch (offset % 4)
	{
	case 0: data = data_r(); break;
	case 1: data = status_r(); break;
	case 2: data = (m_control << 16) | m_mode; break;
	case 3: data = m_baud << 16; break;
	}

	return data;
}

void psxsio1_uart_device::write(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	switch (offset % 4)
	{
	case 0:
		data_w(data);
		break;
	case 2:
		if (ACCESSING_BITS_0_15)
		{
			mode_w(data & 0xffff);
		}
		if (ACCESSING_BITS_16_31)
		{
			command_w(data >> 16);
		}

		if (m_br_factor != 0 && m_baud != 0) {
			LOGCOM("baudrate: %d\n", (41000 * 768) / (m_br_factor * m_baud));
		}
		break;
	case 3:
		if (ACCESSING_BITS_16_31)
		{
			m_baud = data >> 16;

			if (m_br_factor != 0 && m_baud != 0) {
				LOGCOM("baudrate: %d\n", (41000 * 768) / (m_br_factor * m_baud));
			}

			sio_timer_adjust();
		}
		break;
	}
}

void psxsio1_uart_device::write_rxd(int state)
{
	if (!BIT(m_control, SIO_CONTROL_BIT_RXEN))
		return;

	m_rxd = state;
	LOGBITS("8251: Presented a %d\n", m_rxd);

	if (BIT(m_control, SIO_CONTROL_BIT_RXEN))
	{
		receive_register_update_bit(m_rxd);

		if (is_receive_register_full())
		{
			receive_register_extract();

			if (is_receive_parity_error())
				m_status |= SIO_STATUS_PARITY_ERROR;
			if (is_receive_framing_error())
				m_status |= SIO_STATUS_FRAMING_ERROR;

			auto val = get_received_char();

			receive_character(val);

			if (BIT(m_control, SIO_CONTROL_BIT_RX_IENA)) {
				m_status |= SIO_STATUS_IRQ;
				m_irq_handler(1);
			}
		}
	}
}

void psxsio1_uart_device::write_cts(int state)
{
	m_cts = !state;

	if (started())
	{
		check_for_tx_start();
	}
}

void psxsio1_uart_device::write_dsr(int state)
{
	auto doInt = !state && m_dsr != !state;

	m_dsr = !state;

	if (doInt && BIT(m_control, SIO_CONTROL_BIT_DSR_IENA))
	{
		m_status |= SIO_STATUS_IRQ;
		m_irq_handler(1);
	}
}
