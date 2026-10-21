/* Frames received from the touch controllers.
 *
 * Every frame is [header][length][payload of `length` bytes][checksum], the checksum being the
 * sum of all the bytes before it, so a frame is length + 3 bytes long. The frames are parsed
 * straight out of the receive ring of the port. */

#include "ss321.h"

/* Frame headers. The length byte of a frame is fixed for each header. */
#define FRAME_TOUCH_DATA        0xad        /* length 0x40: the readings of the eight pads */
#define FRAME_A0                0xa0        /* length 0x40: stored for the setup tool */
#define FRAME_A1                0xa1        /* length 0x40 */
#define FRAME_AA                0xaa        /* length 0x40 */
#define FRAME_INFO              0xb6        /* length 6: version information */
#define FRAME_B5                0xb5        /* length 2: acknowledge; length 0x24: the tuning words */
#define FRAME_STATUS            0xde        /* length 2: mode config or mode scan acknowledge */
#define FRAME_PARAM_BD          0xbd        /* length 2: parameter bd, echoed */
#define FRAME_PARAM_BE          0xbe        /* length 2: parameter be, echoed */
#define FRAME_NAME              0xb2        /* length 0x0d: name */
#define FRAME_CAP_STATUS        0xb0        /* length 2: capacitance status */

#define FRAME_PAYLOAD_PADS      0x40

/* Result of looking at the frame at the read index. */
enum frame_check {
    FRAME_INCOMPLETE,       /* not all of it has arrived yet */
    FRAME_BAD,              /* complete, but the checksum is wrong */
    FRAME_OK
};

static uint8_t ring_at(volatile touch_ring_t *ring, uint32_t offset)
{
    return ring->data[(ring->read + offset) & TOUCH_RING_MASK];
}

static void ring_skip(volatile touch_ring_t *ring, uint32_t count)
{
    ring->read = (ring->read + count) & TOUCH_RING_MASK;
}

/* Is there a whole frame of `size` bytes whose first `summed` bytes add up to the byte that follows
 * them? Most frames end with their checksum (summed = size - 1); the two parameter frames carry
 * one more byte behind it. */
static enum frame_check check_frame(volatile touch_ring_t *ring, uint32_t available, uint32_t size,
                                    uint32_t summed)
{
    uint8_t sum = 0;
    uint32_t i;

    if (available < size) {
        return FRAME_INCOMPLETE;
    }
    for (i = 0; i < summed; i++) {
        sum += ring_at(ring, i);
    }
    return ring_at(ring, summed) == sum ? FRAME_OK : FRAME_BAD;
}

/* The 16 bit value the two payload bytes of a short frame hold. */
static uint16_t frame_value(volatile touch_ring_t *ring)
{
    return (uint16_t)((ring_at(ring, 2) << 8) | ring_at(ring, 3));
}

/* Copy the 0x40 byte payload of the frame at the read index. */
static void copy_payload(volatile touch_ring_t *ring, volatile uint8_t *dst)
{
    uint32_t i;

    for (i = 0; i < FRAME_PAYLOAD_PADS; i++) {
        dst[i] = ring_at(ring, i + 2);
    }
}

/* Handle the frame at the read index of `port`, if one is complete. Anything that is not a frame
 * is skipped one byte at a time until a header shows up. */
void touch_parse_frame(uint32_t port)
{
    volatile touch_ring_t *ring = &touch_ring[port];
    uint32_t available = (ring->write - ring->read) & TOUCH_RING_MASK;
    uint8_t header, length;
    uint8_t bit = 1 << (port & 0xff);
    enum frame_check state;

    if (available < 5) {
        return;
    }
    header = ring_at(ring, 0);
    length = ring_at(ring, 1);

    if (header == FRAME_TOUCH_DATA && length == 0x40) {
        if (available < 0x43) {
            return;
        }
        if (port < 5) {
            if (tick_ms - touch_last_frame_tick[port] < 0x100) {
                dbt_packet.interval[port] = (uint8_t)tick_ms - (uint8_t)touch_last_frame_tick[port];
            } else {
                dbt_packet.interval[port] = 0xff;
            }
        }
        touch_last_frame_tick[port] = tick_ms;
        if (check_frame(ring, available, 0x43, 0x42) == FRAME_OK) {
            copy_payload(ring, touch_frame[port]);
            /* the high nibble of the first byte carries the "new frame" flag; that byte is the high byte of a
             * reading, 5 at most, so nothing is lost */
            touch_frame[port][0] |= bit << 4;
            ring_skip(ring, 0x43);
            return;
        }
    }
    if (header == FRAME_A0 && length == 0x40) {
        state = check_frame(ring, available, 0x43, 0x42);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            copy_payload(ring, touch_raw_a0[port]);
            ring_skip(ring, 0x43);
            return;
        }
    }
    if (header == FRAME_A1 && length == 0x40) {
        state = check_frame(ring, available, 0x43, 0x42);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            copy_payload(ring, touch_raw_a1[port]);
            ring_skip(ring, 0x43);
            return;
        }
    }
    if (header == FRAME_AA && length == 0x40) {
        state = check_frame(ring, available, 0x43, 0x42);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            copy_payload(ring, touch_raw_aa[port]);
            ring_skip(ring, 0x43);
            return;
        }
    }
    if (header == FRAME_INFO && length == 6) {
        state = check_frame(ring, available, 9, 8);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            uint32_t i;

            for (i = 1; i < 7; i++) {
                touch_info[port][i] = ring_at(ring, i + 1);
            }
            touch_ack.info_received |= bit;
            ring_skip(ring, 9);
            return;
        }
    }
    if (header == FRAME_B5 && length == 2) {
        state = check_frame(ring, available, 5, 4);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            ring_skip(ring, 5);
            return;
        }
    }
    if (header == FRAME_STATUS && length == 2) {
        state = check_frame(ring, available, 5, 4);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            if (ring_at(ring, 2) == 2 && ring_at(ring, 3) == 0) {
                touch_ack.mode_config_ack |= bit;
            } else if (ring_at(ring, 2) == 2 && ring_at(ring, 3) == 1) {
                touch_ack.mode_scan_ack |= bit;
            }
            ring_skip(ring, 5);
            return;
        }
    }
    if (header == FRAME_PARAM_BD && length == 2) {
        state = check_frame(ring, available, 6, 4);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK && ring_at(ring, 5) == touch_expected_seq[port]) {
            if (frame_value(ring) == settings.touch_param_bd[0]) {
                touch_ack.param_bd_ack |= bit;
            }
            ring_skip(ring, 6);
            return;
        }
    }
    if (header == FRAME_PARAM_BE && length == 2) {
        state = check_frame(ring, available, 6, 4);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            if (frame_value(ring) == settings.touch_param_be) {
                touch_ack.param_be_ack |= bit;
            }
            ring_skip(ring, 6);
            return;
        }
    }
    if (header == FRAME_B5 && length == 0x24) {
        state = check_frame(ring, available, 0x27, 0x26);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            uint32_t i;

            for (i = 0; i < 18; i++) {
                settings.tuning[i] = (uint16_t)((ring_at(ring, 2 + 2 * i) << 8) | ring_at(ring, 3 + 2 * i));
            }
            touch_ack.tuning_ack |= bit;
            ring_skip(ring, 0x27);
            return;
        }
    }
    if (header == FRAME_NAME && length == 0x0d) {
        state = check_frame(ring, available, 0x10, 0xf);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            uint32_t i;

            touch_ack.name_received |= bit;
            for (i = 0; i < 0x0d; i++) {
                touch_name[i] = ring_at(ring, i + 2);
            }
            ring_skip(ring, 0x10);
            return;
        }
    }
    if (header == FRAME_CAP_STATUS && length == 2) {
        state = check_frame(ring, available, 5, 4);
        if (state == FRAME_INCOMPLETE) {
            return;
        }
        if (state == FRAME_OK) {
            touch_cap_status = (uint8_t)frame_value(ring);
            touch_ack.status_received |= bit;
            ring_skip(ring, 5);
            return;
        }
    }
    ring_skip(ring, 1);
}

/* Has the controller of `port` sent its start-up banner? There are four of them, 7 bytes starting
 * with 0xdf. Returns the port's bit when it has, else 0. */
uint32_t touch_detect_boot_banner(uint32_t port)
{
    static const uint8_t banners[4][7] = {
        { 0xdf, 0xa0, 0xb1, 0xb1, 0xc1, 0xc1, 0xc1 },
        { 0xdf, 0xa0, 0xb2, 0xb2, 0xc2, 0xc2, 0xc2 },
        { 0xdf, 0xa0, 0xb3, 0xb3, 0xc3, 0xc3, 0xc3 },
        { 0xdf, 0xa0, 0xb4, 0xb4, 0xc4, 0xc4, 0xc4 },
    };
    volatile touch_ring_t *ring = &touch_ring[port];
    uint32_t i;

    for (i = 0; i < ring->write; i = (i + 1) & 0xffff) {
        if (ring->data[i] == 0xdf &&
            (memcmp((const uint8_t *)&ring->data[i], banners[0], 7) == 0 ||
             memcmp((const uint8_t *)&ring->data[i], banners[1], 7) == 0 ||
             memcmp((const uint8_t *)&ring->data[i], banners[2], 7) == 0 ||
             memcmp((const uint8_t *)&ring->data[i], banners[3], 7) == 0)) {
            return (1 << (port & 0xff)) & 0xff;
        }
    }
    return 0;
}

/* Main loop task: parse the frames of every controller. While a controller is being updated
 * (touch_update_mode 1, started by the setup tool) the bootloader replies are collected instead:
 * each is [0x01][status][length, 2 bytes][data][checksum, 2 bytes][0x17]. When all four ports
 * have one, the last is sent to the host. */
void touch_uart_task(void)
{
    uint32_t port;

    if (touch_update_mode == 0) {
        for (port = 0; port < TOUCH_PORTS; port++) {
            touch_parse_frame(port);
        }
    }
    if (touch_update_mode == 1) {
        for (port = 0; port < TOUCH_PORTS; port++) {
            volatile touch_ring_t *ring = &touch_ring[port];

            if ((int)ring->write - (int)ring->read > 5) {
                if (ring->data[ring->read] == 1) {
                    int length = ring->data[ring->read + 2] + ring->data[ring->read + 3] * 0x100;

                    if (ring->data[ring->read + length + 6] == 0x17) {
                        touch_update_replies |= 1 << (port & 0xff);
                    }
                    if (touch_update_replies == 0xf) {
                        if (length + 7U < 0x32) {
                            command_send_reply(0xa1, (const uint8_t *)&ring->data[ring->read], (length + 7U) & 0xff);
                        }
                        touch_update_replies = 0;
                        for (length = 0; length < TOUCH_PORTS; length++) {
                            touch_ring[port].read = 0;
                            touch_ring[port].write = 0;
                        }
                        return;
                    }
                } else {
                    ring->read = ring->read + 1;
                }
            }
        }
    }
}
