/* The stream: the touch frames and the state of the pads, sent to the host on endpoint EP6. The host starts
 * and stops it with a three byte message on EP7 ("h", a command, a checksum). */

#include "ss321.h"

#define STREAM_CTL_MAGIC        'h'
#define STREAM_CTL_OFF_AA       0xaa        /* off */
#define STREAM_CTL_OFF          0xbb        /* off, and the stream endpoint is free */
#define STREAM_CTL_ON_CC        0xcc        /* on */
#define STREAM_CTL_ON           0xdd        /* on, and the stream endpoint is free */

#define STREAM_PACKET_SIZE      0x40
#define STREAM_PERIOD_MS        25

/* Main loop task: carry out a start or stop of the stream. */
void stream_control_task(void)
{
    if (stream_ctl_pending != 1) {
        return;
    }
    stream_ctl_pending = 0;
    if (stream_ctl_buffer[0] == STREAM_CTL_MAGIC && stream_ctl_buffer[1] == STREAM_CTL_OFF_AA) {
        stream_mode = 0;
    }
    if (stream_ctl_buffer[0] == STREAM_CTL_MAGIC && stream_ctl_buffer[1] == STREAM_CTL_OFF) {
        stream_mode = 0;
        ep6_ready = 1;
    }
    if (stream_ctl_buffer[0] == STREAM_CTL_MAGIC && stream_ctl_buffer[1] == STREAM_CTL_ON_CC) {
        stream_mode = 1;
    }
    if (stream_ctl_buffer[0] == STREAM_CTL_MAGIC && stream_ctl_buffer[1] == STREAM_CTL_ON) {
        stream_mode = 2;
        ep6_ready = 1;
    }
}

/* Put a packet in the queue of the stream; `flags` are or-ed into its first byte. The DBT packet
 * (flags 0) carries the delayed pads when the input delay is on, in the WinUSB mode. */
void stream_queue_push(const volatile uint8_t *packet, int flags)
{
    volatile uint8_t *entry = stream_queue[stream_queue_head];

    if (settings.input_delay == 0 || settings.device_mode != MODE_WINUSB || flags != 0) {
        memcpy((void *)entry, (const void *)packet, STREAM_PACKET_SIZE);
    } else {
        entry[0] = packet[0];
        entry[1] = packet[1];
        entry[2] = packet[2];
        entry[3] = packet[3];
        entry[0x32] = packet[0x32];
        entry[0x33] = packet[0x33];
        entry[0x34] = packet[0x34];
        entry[0x35] = packet[0x35];
        memcpy((void *)&entry[4], (const void *)stream_delay_queue[input_queue_read].pad, PAD_COUNT);
    }
    entry[0] |= (uint8_t)flags;
    stream_queue_head = stream_queue_head + 1;
    if (stream_queue_head > STREAM_QUEUE_LENGTH - 1) {
        stream_queue_head = 0;
    }
    if (stream_queue_head == stream_queue_tail) {
        stream_queue_head = 0;
        stream_queue_tail = 0;
    }
}

/* Write a packet to the stream endpoint; returns the number of bytes. */
uint32_t stream_send(const volatile uint8_t *data, uint32_t size)
{
    uint32_t buffer = USBD_BUFSEG6;
    uint32_t i = 0;

    if (size != 0) {
        for (i = 0; i < size; i++) {
            USB_SRAM[i + buffer] = data[i];
        }
        USBD_MXPLD6 = STREAM_PACKET_SIZE;
    }
    return i;
}

/* Send the oldest packet of the queue when the endpoint is free, at most one every 4 ms. */
void stream_queue_pop(void)
{
    if (ep6_ready == 1 && stream_queue_head != stream_queue_tail && (tick_ms & 3) == 0 &&
        stream_pop_tick != tick_ms) {
        stream_pop_tick = tick_ms;
        ep6_ready = 0;
        stream_send(stream_queue[stream_queue_tail], STREAM_PACKET_SIZE);
        stream_queue_tail = stream_queue_tail + 1;
        if (stream_queue_tail > STREAM_QUEUE_LENGTH - 1) {
            stream_queue_tail = 0;
        }
        if (stream_queue_tail == stream_queue_head) {
            stream_queue_tail = 0;
            stream_queue_head = 0;
        }
    }
}

/* Main loop task: hand the frames the touch controllers sent to the personality, and, when the stream is
 * on, queue the touch frames and the DBT packet every 25 ms. */
void touch_report_task(void)
{
    uint32_t port;

    for (port = 0; port < TOUCH_PORTS; port = (port + 1) & 0xff) {
        if ((touch_frame[port][0] & 0xf0) != 0) {
            touch_frame[port][0] &= 0xf;
            if (touch_state == TOUCH_STATE_RUNNING) {
                touch_decode_hook((const uint8_t *)touch_frame[port], port);
            }
        }
    }
    if (stream_mode != 0) {
        if (stream_push_tick != tick_ms && tick_ms % STREAM_PERIOD_MS == 0) {
            stream_push_tick = tick_ms;
            stream_queue_push(touch_frame[0], 0xc0);
            stream_queue_push(touch_frame[1], 0xd0);
            stream_queue_push(touch_frame[2], 0xe0);
            stream_queue_push(touch_frame[3], 0xf0);
            stream_queue_push((const volatile uint8_t *)&dbt_packet, 0);
        }
    }
    stream_queue_pop();
}
