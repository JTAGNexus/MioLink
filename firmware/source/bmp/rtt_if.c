/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2021 Koen De Vleeschauwer
 * Modified 2024 Dmitry Rezvanov <dmitry.rezvanov@yandex.ru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "general.h"
#include "platform.h"
#include <assert.h>
#include "usb_serial.h"
#include "rtt.h"
#include "rtt_if.h"

/*********************************************************************
*
*       rtt terminal i/o
*
**********************************************************************
*/

/* usb uart receive buffer */
static char recv_buf[RTT_DOWN_BUF_SIZE];
static uint32_t recv_head = 0;
static uint32_t recv_tail = 0;

/* data from host to target: number of free bytes in usb receive buffer */
inline static uint32_t recv_bytes_free()
{
	if (recv_tail <= recv_head)
		return RTT_DOWN_BUF_SIZE - recv_head + recv_tail - 1U;
	return recv_tail - recv_head - 1U;
}

/* debug_serial_receive_callback is called when usb uart has received new data for target.
   this routine has to be fast */

void rtt_serial_receive_callback(void)
{
	char usb_buf[64];

    const uint32_t len = tud_cdc_n_read(USB_SERIAL_TARGET, usb_buf, sizeof(usb_buf));

	/* skip flag: drop packet if not enough free buffer space */
	if (rtt_flag_skip && len > recv_bytes_free())
    {
		return;
	}

	/* copy data to recv_buf */
	for (int i = 0; i < len; i++)
    {
		uint32_t next_recv_head = (recv_head + 1U) % sizeof(recv_buf);
		if (next_recv_head == recv_tail)
			break; /* overflow */
		recv_buf[recv_head] = usb_buf[i];
		recv_head = next_recv_head;
	}
}

/* rtt host to target: read one character */
int32_t rtt_getchar(const uint32_t channel)
{
	int retval;
    (void)channel;

	if (recv_head == recv_tail)
		return -1;
	retval = (uint8_t)recv_buf[recv_tail];
	recv_tail = (recv_tail + 1U) % sizeof(recv_buf);

	return retval;
}

/* rtt host to target: true if no characters available for reading */
bool rtt_nodata(const uint32_t channel)
{
    /* only support reading from down channel 0 */
    if (channel != 0U)
        return true;

	return recv_head == recv_tail;
}

/* rtt target to host: write string */
uint32_t rtt_write(const uint32_t channel, const char *buf, uint32_t len)
{
    /* only support writing to up channel 0 */
    if (channel != 0U)
        return len;

	if ((len != 0) &&
        usb_get_config() &&
        gdb_serial_get_dtr() &&
        tud_cdc_n_connected(USB_SERIAL_TARGET))
    {
		for (uint32_t p = 0; p < len; p += 64) {
			uint32_t plen = MIN(64, len - p);
			uint32_t start_ms = platform_time_ms();
			while (tud_cdc_n_write(USB_SERIAL_TARGET, buf + p, plen) != plen) {
				if (platform_time_ms() - start_ms >= 25)
					return 0; /* drop silently */
			}
		}
        tud_cdc_n_write_flush(USB_SERIAL_TARGET);
	}
	return len;
}