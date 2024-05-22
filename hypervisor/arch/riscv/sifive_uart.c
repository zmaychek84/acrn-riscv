/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <acrn/config.h>
#include <types.h>

#define UART_BASE (volatile unsigned char *)0x10010000

#define UART_REG_TXFIFO		0
#define UART_REG_RXFIFO		1
#define UART_REG_TXCTRL		2
#define UART_REG_RXCTRL		3
#define UART_REG_IE		4
#define UART_REG_IP		5
#define UART_REG_DIV		6

#define UART_TXFIFO_FULL	0x80000000
#define UART_RXFIFO_EMPTY	0x80000000
#define UART_RXFIFO_DATA	0x000000ff
#define UART_TXCTRL_TXEN	0x1
#define UART_RXCTRL_RXEN	0x1

static void write32(volatile uint32_t *addr, char c)
{
	*addr = c;
}

static uint32_t read32(volatile uint32_t *addr)
{
	return *addr;
}

static void write8(volatile unsigned char *addr, char c)
{
	*addr = c;
}

static char read8(volatile unsigned char *addr)
{
	return *addr;
}

static inline unsigned int uart_min_clk_divisor(uint64_t in_freq,
						uint64_t max_target_hz)
{
	uint64_t quotient = (in_freq + max_target_hz - 1) / (max_target_hz);

	if (quotient == 0)
		return 0;
	else
		return quotient - 1;
}

static uint32_t get_reg(uint32_t num)
{
	return read32((volatile uint32_t *)(UART_BASE + (num * 0x4)));
}

static void set_reg(uint32_t num, uint32_t val)
{
	write32(val, UART_BASE + (num * 0x4));
}

void sifive_uart_init()
{
	set_reg(UART_REG_DIV, uart_min_clk_divisor(0, 115200));
	set_reg(UART_REG_IE, 0);
	set_reg(UART_REG_TXCTRL, UART_TXCTRL_TXEN);
	set_reg(UART_REG_RXCTRL, UART_RXCTRL_RXEN);
}

static void sifive_uart_putc(char ch)
{
	while (get_reg(UART_REG_TXFIFO) & UART_TXFIFO_FULL)
		;

	write8(UART_BASE + (UART_REG_TXFIFO * 0x4), ch);
}

void put_char(char ch)
{
	sifive_uart_putc(ch);
}

static int sifive_uart_getc(void)
{
	uint32_t ret = get_reg(UART_REG_RXFIFO);

	if (!(ret & UART_RXFIFO_EMPTY))
		return ret & UART_RXFIFO_DATA;

	return -1;
}

static void get_char(char *c)
{
	int ret;
	do {
		ret = sifive_uart_getc();
	} while (ret == -1);

	*c = ret;
}

char uart16550_getc(void)
{
	char c;

	get_char(&c);
	return c;
}

size_t uart16550_puts(const char *buf, uint32_t len)
{
	int i = 0;
	if (buf == 0)
		return 0;

	for (i = 0U; i < len; i++) {
		if (buf[i] == '\n')
			put_char('\r');
		put_char(buf[i]);
	}

	return len;
}

void uart16550_init(bool early_boot)
{}

void early_putch(char c)
{
	put_char(c);
}

char early_getch(void)
{
	char c;

	get_char(&c);
	return c;
}

void early_printk(char *buf)
{
	int i = 0;
	if (buf == 0)
		return;

	while (buf[i] != '\0') {
		if (buf[i] == '\n')
			put_char('\r');
		put_char(buf[i++]);
	}
}

/* The initial log level*/
void npk_log_write(const char *buf, size_t buf_len)
{}