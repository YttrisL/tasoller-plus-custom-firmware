/* UART side of the touch controllers: four UARTs, one per controller. */

#include "ss321.h"

#define UART_FIFO           2       /* index of the FIFO control register (word offset) */
#define UART_FIFO_RFITL     0xf0u   /* receive FIFO trigger level */
#define UART_FIFO_RFITL_8   0x20u   /* interrupt when 8 bytes have arrived */
#define UART_INT_RDA        0x01u   /* the receive interrupt enable bit */

/* Interrupt status bits */
#define INTSTS_RDA_FLAG     0x001u  /* data in the receive FIFO */
#define INTSTS_RDA_INT      0x100u
#define INTSTS_RXTO_INT     0x1000u
#define FIFOSTS_ERRORS      0x71u   /* overrun, parity, frame and break errors */

/* Receive interrupt of one touch UART: move the bytes of the FIFO into the ring of its port. When
 * the ring overflows everything in it is dropped. */
#define TOUCH_UART_ISR(handler, uart, port)                                                     \
void handler(void)                                                                              \
{                                                                                               \
    volatile touch_ring_t *ring = &touch_ring[port];                                            \
    uint32_t status = uart##_INTSTS;                                                            \
                                                                                                \
    if ((status & INTSTS_RDA_INT) != 0 || (status & INTSTS_RXTO_INT) != 0) {                    \
        while ((uart##_INTSTS & INTSTS_RDA_FLAG) != 0) {                                        \
            ring->data[ring->write] = (uint8_t)uart##_DAT;                                      \
            ring->write = (ring->write + 1) & TOUCH_RING_MASK;                                  \
            if (ring->write == ring->read) {                                                    \
                ring->write = 0;                                                                \
                ring->read = 0;                                                                 \
            }                                                                                   \
        }                                                                                       \
    }                                                                                           \
    if ((uart##_FIFOSTS & FIFOSTS_ERRORS) != 0) {                                               \
        uart##_FIFOSTS = FIFOSTS_ERRORS;                                                        \
    }                                                                                           \
}

TOUCH_UART_ISR(UART5_IRQHandler, UART5, 0)
TOUCH_UART_ISR(UART2_IRQHandler, UART2, 1)
TOUCH_UART_ISR(UART3_IRQHandler, UART3, 2)
TOUCH_UART_ISR(UART4_IRQHandler, UART4, 3)

/* Send a frame to every touch controller. */
void touch_send_all(const uint8_t *data, int length)
{
    UART_Write(&UART2_DAT, data, length);
    UART_Write(&UART3_DAT, data, length);
    UART_Write(&UART4_DAT, data, length);
    UART_Write(&UART5_DAT, data, length);
}

/* Send a frame to the controller of `port` (0..3). */
void touch_send_to(int port, const uint8_t *data, int length)
{
    if (port == 3) {
        UART_Write(&UART4_DAT, data, length);
    }
    if (port == 2) {
        UART_Write(&UART3_DAT, data, length);
    }
    if (port == 1) {
        UART_Write(&UART2_DAT, data, length);
    }
    if (port == 0) {
        UART_Write(&UART5_DAT, data, length);
    }
}

/* Forget what was received (only the start of each ring is cleared). */
void touch_rx_flush(void)
{
    uint32_t port;

    for (port = 0; port < TOUCH_PORTS; port++) {
        touch_ring[port].read = 0;
        touch_ring[port].write = 0;
        memset((void *)touch_ring[port].data, 0, 0x20);
    }
}

/* Clock, reset and open the four UARTs of the touch controllers at `baud` with their receive
 * interrupt on. */
void touch_uart_init(uint32_t baud)
{
    static const struct {
        volatile uint32_t *uart;
        uint32_t clock;             /* module id of the UART clock */
        uint32_t reset;             /* module id of the UART reset */
        int irq;
    } uarts[4] = {
        { &UART2_DAT, 0x778c3c12, 0x4000012, 0x30 },
        { &UART3_DAT, 0x77ac3c93, 0x4000013, 0x31 },
        { &UART4_DAT, 0x77cc3d14, 0x4000014, 0x4a },
        { &UART5_DAT, 0x77ec3d95, 0x4000015, 0x4b },
    };
    int i;

    SYS_UnlockReg();
    for (i = 0; i < 4; i++) {
        CLK_EnableModuleClock(uarts[i].clock);
    }
    for (i = 0; i < 4; i++) {
        SYS_ResetModule(uarts[i].reset);
    }
    SYS_REGLCTL = 0;
    for (i = 0; i < 4; i++) {
        NVIC_SetPriority(uarts[i].irq, 1);
    }
    for (i = 0; i < 4; i++) {
        volatile uint32_t *uart = uarts[i].uart;

        uart[UART_FIFO] &= ~UART_FIFO_RFITL;
        uart[UART_FIFO] |= UART_FIFO_RFITL_8;
        NVIC_EnableIRQ(uarts[i].irq);
        UART_EnableInt(uart, UART_INT_RDA);
        UART_Open(uart, baud);
    }
}

/* Pass-through mode: UART0, 1, 2 and 4 are reset and reopened at `baud` with their interrupts off, and a
 * frame is sent to the touch controllers. The setup tool uses it to update the touch controllers. */
void bridge_mode_enter(uint32_t baud)
{
    static const struct {
        volatile uint32_t *uart;
        uint32_t reset;
        int irq;
    } uarts[4] = {
        { (volatile uint32_t *)&UART1_DAT, 0x4000011, 0x25 },
        { &UART2_DAT, 0x4000012, 0x30 },
        { (volatile uint32_t *)&UART0_DAT, 0x4000010, 0x24 },
        { &UART4_DAT, 0x4000014, 0x4a },
    };
    uint8_t frame[5] = { 0xde, 0x02, 0x00, 0x01, 0xe1 };
    int i;

    SYS_UnlockReg();
    for (i = 0; i < 4; i++) {
        SYS_ResetModule(uarts[i].reset);
    }
    SYS_REGLCTL = 0;
    for (i = 0; i < 4; i++) {
        UART_ClearIntFlag(uarts[i].uart, uarts[i].uart[7]);       /* INTSTS */
    }
    for (i = 0; i < 4; i++) {
        volatile uint32_t *uart = uarts[i].uart;

        uart[UART_FIFO] &= ~UART_FIFO_RFITL;
        uart[UART_FIFO] = uart[UART_FIFO];
        NVIC_DisableIRQ(uarts[i].irq);
        UART_DisableInt(uart, UART_INT_RDA);
        UART_Open(uart, baud);
    }
    touch_send_all(frame, 5);
    bridge_mode = 1;
    for (i = 0; i < 8; i++) {
        bridge_counters[i] = 0;
    }
}

/* Leave pass-through mode: reset the touch controllers and open their UARTs again. */
void bridge_mode_exit(void)
{
    uint8_t frame[5] = { 0xde, 0x02, 0x00, 0x01, 0xe1 };

    touch_send_all(frame, 5);
    bridge_mode = 0;
    touch_uart_init(0xe1000);
}
