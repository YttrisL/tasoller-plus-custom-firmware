/* Sending the colours to the LED strips.
 *
 * The LEDs (WS2812 type, one wire, 800 kHz) are driven in four different ways, one per strip:
 *   slider   a timer paces a DMA transfer that sets a GPIO pin, five slots per LED bit
 *   tower A  the QSPI0 interface, three SPI bits per LED bit
 *   tower B  the SPI1 interface, same
 *   aux      the UUART0 interface, same
 * Each strip has its own buffer of already encoded bits; led_dma_task fills it (from the rgb bytes
 * led_hsv_to_rgb_task made) and starts the transfer when the previous one has finished. */

#include "ss321.h"

#define PDMA_BASE               ((int)&DSCT_CTL_PDMA_DSCT0_CTL)
#define PDMA_DSCT_CTL(channel)  (((volatile uint32_t *)0x40008000u)[(channel) * 4])
#define PDMA_CTL_ENABLE         0x80u           /* arm a channel */
#define PDMA_WIDTH_8BIT         0
#define PDMA_DST_FIXED          0xc00
#define PDMA_BURST_1            4

/* DMA channels and the peripheral requests that pace them */
#define SLIDER_CHANNEL          5
#define SLIDER_REQUEST          0x2f            /* timer 1 */
#define TOWER_A_CHANNEL         3
#define TOWER_A_REQUEST         0x14            /* QSPI0 transmit */
#define TOWER_B_CHANNEL         6
#define TOWER_B_REQUEST         0x18            /* SPI1 transmit */
#define AUX_CHANNEL             4
#define AUX_REQUEST             0x10            /* UUART0 transmit */

/* Size of the transfers in bytes */
#define SLIDER_BYTES            0x1681          /* 48 LEDs * 24 bits * 5 slots + 1 */
#define TOWER_BYTES             0xd8            /* 24 LEDs * 9 */
#define AUX_BYTES               0xfc            /* 28 LEDs * 9 */

/* The encoded bits of every strip. */
#define slider_stream           ((volatile uint8_t *)0x20019538u)
#define tower_a_stream          ((volatile uint8_t *)0x20019388u)
#define tower_b_stream          ((volatile uint8_t *)0x20019460u)
#define aux_stream              ((volatile uint8_t *)0x2001ABB9u)

/* Set once the DMA channel of a strip has been configured: tower A, tower B, aux. */
#define dma_started             ((volatile uint8_t *)0x200006A6u)

/* Timer 1 starts the slider transfer: it is armed once and its control bits are changed
 * to start and stop the stream. */
#define TIMER_CNT_RESET_BUSY    0x80000000u
#define TIMER_CTL_ENABLE        0x40000000u

/* What a LED bit looks like on the slider wire: five slots of the timer, high for one slot for a 0 and
 * for three for a 1. */
static const uint8_t slider_bit_zero[5] = { 1, 0, 0, 0, 0 };
static const uint8_t slider_bit_one[5] = { 1, 1, 1, 0, 0 };

/* The brightness of a colour byte is squared (and scaled back to 8 bits): this is the gamma
 * correction of the LEDs. */
static uint32_t gamma_correct(uint32_t value)
{
    return (uint32_t)((int)(short)value * (int)(short)value) >> 8;
}

/* Three SPI bytes for one colour byte: each of its bits becomes the three bits 1, bit, 0. */
static void encode_colour_byte(uint32_t value, volatile uint8_t *out)
{
    uint32_t level = gamma_correct(value);
    uint32_t bits = 0;
    int bit;

    for (bit = 7; bit >= 0; bit--) {
        bits = (bits << 3) | 4 | (((level >> bit) & 1) << 1);
    }
    out[0] = (uint8_t)(bits >> 16);
    out[1] = (uint8_t)(bits >> 8);
    out[2] = (uint8_t)bits;
}

/* Encode the colours of a strip into the bits to send. */
void ws2812_encode(const volatile rgb_t *rgb, int strip)
{
    const volatile uint8_t *bytes = (const volatile uint8_t *)rgb;
    volatile uint8_t *out;
    uint32_t leds;
    uint32_t led, colour;
    int byte_index, bit;

    switch (strip) {
    case LED_STRIP_SLIDER:
        for (byte_index = 0; byte_index < SLIDER_LEDS * 3; byte_index++) {
            uint32_t level = gamma_correct(bytes[byte_index]);

            for (bit = 7; bit >= 0; bit--) {
                const uint8_t *pattern = ((level >> bit) & 1) == 0 ? slider_bit_zero : slider_bit_one;
                int slot = ((7 - bit) + byte_index * 8) * 5;
                int i;

                for (i = 0; i < 5; i++) {
                    slider_stream[slot + i] = pattern[i];
                }
            }
        }
        slider_stream[SLIDER_BYTES - 1] = 0;
        return;
    case LED_STRIP_TOWER_A:
        out = tower_a_stream;
        leds = TOWER_LEDS;
        break;
    case LED_STRIP_TOWER_B:
        out = tower_b_stream;
        leds = TOWER_LEDS;
        break;
    case LED_STRIP_AUX:
        out = aux_stream;
        leds = AUX_LEDS;
        break;
    default:
        return;
    }
    for (led = 0; led < leds; led = (led + 1) & 0xffff) {
        for (colour = 0; colour < 3; colour = (colour + 1) & 0xffff) {
            encode_colour_byte(bytes[led * 3 + colour], out + colour * 3 + led * 9);
        }
    }
}

/* Set up the DMA channel of a strip that is sent through a serial interface and arm it; once it is
 * set up only the length and the request have to be given again. */
static void serial_dma_start(int channel, uint32_t channel_mask, const volatile uint8_t *stream,
                             uint32_t bytes, volatile uint32_t *data_register, uint32_t request,
                             volatile uint8_t *started)
{
    if (*started == 0) {
        *started = 1;
        PDMA_Open(PDMA_BASE, channel_mask);
        PDMA_SetTransferCnt(PDMA_BASE, channel, PDMA_WIDTH_8BIT, bytes);
        PDMA_SetTransferAddr(PDMA_BASE, channel, (uint32_t)stream, 0, (uint32_t)data_register, PDMA_DST_FIXED);
        PDMA_SetTransferMode(PDMA_BASE, channel, request, 0, 0);
        PDMA_SetBurstType(PDMA_BASE, channel, PDMA_BURST_1, 0);
        PDMA_DSCT_CTL(channel) |= PDMA_CTL_ENABLE;
    } else {
        PDMA_SetTransferCnt(PDMA_BASE, channel, PDMA_WIDTH_8BIT, bytes);
        PDMA_SetTransferMode(PDMA_BASE, channel, request, 0, 0);
    }
}

void led_tower_a_dma_start(void)
{
    serial_dma_start(TOWER_A_CHANNEL, 8, tower_a_stream, TOWER_BYTES, &QSPI0_TX, TOWER_A_REQUEST, &dma_started[0]);
}

void led_tower_b_dma_start(void)
{
    serial_dma_start(TOWER_B_CHANNEL, 0x40, tower_b_stream, TOWER_BYTES, &SPI1_TX, TOWER_B_REQUEST, &dma_started[1]);
}

void led_aux_dma_start(void)
{
    serial_dma_start(AUX_CHANNEL, 0x10, aux_stream, AUX_BYTES, &UUART0_UUART_TXDAT, AUX_REQUEST, &dma_started[2]);
}

/* The slider: the DMA channel is configured every time, then timer 1 is restarted and let go. */
void led_slider_dma_start(void)
{
    PDMA_Open(PDMA_BASE, 0x20);
    PDMA_SetTransferCnt(PDMA_BASE, SLIDER_CHANNEL, PDMA_WIDTH_8BIT, SLIDER_BYTES);
    PDMA_SetTransferAddr(PDMA_BASE, SLIDER_CHANNEL, (uint32_t)slider_stream, 0, (uint32_t)&GPIO_PB12_PDIO, PDMA_DST_FIXED);
    PDMA_SetTransferMode(PDMA_BASE, SLIDER_CHANNEL, SLIDER_REQUEST, 0, 0);
    PDMA_SetBurstType(PDMA_BASE, SLIDER_CHANNEL, PDMA_BURST_1, 0);
    PDMA_DSCT_CTL(SLIDER_CHANNEL) |= PDMA_CTL_ENABLE;
    TMR01_TIMER1_CNT = 0;
    while ((TMR01_TIMER1_CNT & TIMER_CNT_RESET_BUSY) == TIMER_CNT_RESET_BUSY) {
    }
    TMR01_TIMER1_CTL |= TIMER_CTL_ENABLE;
}

/* Main loop task: acknowledge the finished transfers, and start the next one of a strip whenever it has
 * something to send and its turn in the eight millisecond cycle comes. */
void led_dma_task(void)
{
    uint32_t status = PDMA_INTSTS;

    if ((status & 2) != 0) {
        uint32_t done = PDMA_TDSTS;

        if ((done & 0x20) == 0x20) {
            PDMA_TDSTS = 0x20;
            led_busy[LED_STRIP_SLIDER] = 0;
            TMR01_TIMER1_CTL &= ~TIMER_CTL_ENABLE;
        }
        if ((done & 8) == 8) {
            PDMA_TDSTS = 8;
            QSPI0_PDMACTL &= ~1u;
            led_busy[LED_STRIP_TOWER_A] = 0;
        }
        if ((done & 0x40) == 0x40) {
            PDMA_TDSTS = 0x40;
            SPI1_PDMACTL &= ~1u;
            led_busy[LED_STRIP_TOWER_B] = 0;
        }
        if ((done & 0x10) == 0x10) {
            PDMA_TDSTS = 0x10;
            UUART0_UUART_PDMACTL &= ~2u;
            led_busy[LED_STRIP_AUX] = 0;
        }
    }
    if ((status & 1) != 0) {
        PDMA_ABTSTS = PDMA_ABTSTS;              /* clear the abort flags */
    }
    if ((status & 0x300) != 0) {
        PDMA_INTSTS = status & 0x300;
    }
    if (led_dma_tick == tick_ms) {
        return;
    }
    led_dma_tick = tick_ms;
    if (led_busy[LED_STRIP_SLIDER] == 0 && (tick_ms & 7) == 4 && led_pending[LED_STRIP_SLIDER] != 0) {
        ws2812_encode(led_slider_rgb, LED_STRIP_SLIDER);
        led_pending[LED_STRIP_SLIDER]--;
        led_slider_dma_start();
        led_busy[LED_STRIP_SLIDER] = 1;
    }
    if (led_busy[LED_STRIP_TOWER_A] == 0 && (tick_ms & 7) == 2 && led_pending[LED_STRIP_TOWER_A] != 0) {
        led_pending[LED_STRIP_TOWER_A]--;
        ws2812_encode(led_tower_a_rgb, LED_STRIP_TOWER_A);
        led_tower_a_dma_start();
        QSPI0_PDMACTL |= 1;
        led_busy[LED_STRIP_TOWER_A] = 1;
    }
    if (led_busy[LED_STRIP_TOWER_B] == 0 && (tick_ms & 7) == 0 && led_pending[LED_STRIP_TOWER_B] != 0) {
        led_pending[LED_STRIP_TOWER_B]--;
        ws2812_encode(led_tower_b_rgb, LED_STRIP_TOWER_B);
        led_tower_b_dma_start();
        SPI1_PDMACTL |= 1;
        led_busy[LED_STRIP_TOWER_B] = 1;
    }
    if (led_busy[LED_STRIP_AUX] == 0 && (tick_ms & 7) == 6 && led_pending[LED_STRIP_AUX] != 0) {
        led_pending[LED_STRIP_AUX]--;
        ws2812_encode(led_aux_rgb, LED_STRIP_AUX);
        led_aux_dma_start();
        UUART0_UUART_PDMACTL |= 10;
        led_busy[LED_STRIP_AUX] = 1;
    }
}

/* Configure the interfaces that drive the LEDs: QSPI0 and SPI1 at 3.38 MHz, pin PB12 and timer 1 for the
 * slider. Every strip starts dark. */
void led_hardware_init(void)
{
    QSPI_Open(&QSPI0_CTL, 0, 4, 8, 3380000);
    SPI_DisableAutoSS((int)&QSPI0_CTL);
    GPIO_SetPullCtl(&GPIO_PB_MODE, 0x1000, 1);
    GPIO_SetSlewCtl(&GPIO_PB_MODE, 0x1000, 2);
    GPIO_SetMode(&GPIO_PB_MODE, 0x1000, 1);
    GPIO_PB12_PDIO = 0;
    TIMER_Open(&TMR01_TIMER1_CTL, 0x8000000, 3333333);
    TIMER_SetTriggerSource((int)&TMR01_TIMER1_CTL, 0);
    TIMER_SetTriggerTarget((int)&TMR01_TIMER1_CTL, 0x10);
    TMR01_TIMER1_CTL &= 0xdfffffff;
    SPI_Open(&SPI1_CTL, 0, 4, 8, 3380000);
    SPI_DisableAutoSS((int)&SPI1_CTL);
    slider_clear();
}
