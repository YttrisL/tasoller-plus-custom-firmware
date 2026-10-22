/* The chip dump: CMD_DUMP lets the setup tool read the memory and the special areas of the microcontroller
 * over the command channel (the dump firmware of the dump-and-restore project sends the same data over a UART).
 * The host asks for one piece at a time, so nothing has to be buffered here and the rest of the firmware keeps
 * running between two requests.
 *
 * The request is [length][CMD_DUMP][sequence][sub-command][arguments ...], numbers are little endian.
 *
 *   DUMP_HELLO                       -> [0, protocol, number of sections, number of info items]
 *   DUMP_SECTION [index]             -> [1, index, mode, address, length, name]
 *   DUMP_INFO    [index]             -> [2, index, status, value, name]    status 0 = read, 1 = refused
 *   DUMP_READ    [mode, address, n]  -> [3, status, address, n, data ..., crc16]   n is 4..48, a multiple of 4
 *
 * The modes say how a read is done:
 *   'M' plain memory reads. A bus fault is ignored, an area the chip protects reads as zeros.
 *   'I' the ISP read command of the flash controller. It reaches the special areas and refuses what it will not read.
 *   'S' the boot snapshot (see boot_snapshot_take).
 * The status of a DUMP_READ is 0, or 1 when the chip refused (no data follows). The crc16 is CRC-16/CCITT-FALSE
 * over the 4 address bytes and the data. */

#include "ss321.h"

#define DUMP_PROTOCOL           1

#define DUMP_HELLO              0
#define DUMP_SECTION            1
#define DUMP_INFO               2
#define DUMP_READ               3

#define DUMP_CHUNK              48
#define DUMP_NAME_MAX           16

#define REPLY_SEND              0       /* what command.c's enum reply says */
#define REPLY_ERROR             1

#define ISP_CMD_READ            0x00u
#define ISP_CMD_READ_UID        0x04u
#define ISP_CMD_READ_CID        0x0bu
#define ISP_CMD_READ_DID        0x0cu
#define ISP_CTL_ISPEN           0x01u
#define ISP_CTL_ISPFF           0x40u
#define ISP_LIMIT               0x00320000u
#define SCS_CCR                 (*(volatile uint32_t *)0xE000ED14u)
#define SCS_CPUID               (*(volatile uint32_t *)0xE000ED00u)
#define SCS_VTOR                (*(volatile uint32_t *)0xE000ED08u)
#define CCR_BFHFNMIGN           0x100u

typedef struct {
    const char *name;
    uint32_t address;
    uint32_t length;
    char mode;
} dump_section_t;

/* What the dump offers. The first six are the boot snapshot, small and sent first; the 512 KiB of the
 * application flash come last. */
static const dump_section_t sections[] = {
    { "SYS",       0x40000000u, 0x200u,   'S' },    /* part ID, reset status, register lock, pin multiplexing */
    { "CLK",       0x40000200u, 0x100u,   'S' },    /* clock enables, sources, dividers, PLL */
    { "GPIO",      0x40004000u, 0x200u,   'S' },    /* the 8 ports: mode, data out, pull, input */
    { "SYSTICK",   0xE000E010u, 0x10u,    'S' },
    { "NVIC_ISER", 0xE000E100u, 0x20u,    'S' },    /* which interrupts are enabled */
    { "SCB",       0xE000ED00u, 0x40u,    'S' },    /* VTOR, fault status */
    { "SRAM",      0x20000000u, 0x28000u, 'M' },    /* all 160 KiB, as the running firmware has it */
    { "CONFIG",    0x00300000u, 0x10u,    'I' },    /* user configuration words, with the lock bit */
    { "KPROM",     0x00301000u, 0x10u,    'I' },    /* key protection store */
    { "XOM",       0x00200000u, 0x40u,    'I' },    /* execute-only region configuration */
    { "OTP",       0x00310000u, 0x800u,   'I' },    /* one-time-programmable area */
    { "LDROM",     0x00100000u, 0x1000u,  'M' },    /* the loader ROM */
    { "APROM",     0x00000000u, 0x80000u, 'M' },    /* the application flash: bootloader, slots, settings */
};
#define SECTION_COUNT   (sizeof sections / sizeof sections[0])

/* Where plain memory reads may go. */
static const struct {
    uint32_t start, end;
} memory_ranges[] = {
    { 0x00000000u, 0x00080000u },   /* APROM */
    { 0x00100000u, 0x00102000u },   /* LDROM */
    { 0x20000000u, 0x20028000u },   /* SRAM */
    { 0x40000000u, 0x40004800u },   /* SYS, CLK, INT, GPIO (live) */
    { 0x4000C000u, 0x4000C100u },   /* FMC */
    { 0xE000E000u, 0xE000F000u },   /* SysTick, NVIC, SCB */
};

static const char *const info_names[] = {
    "CFG0", "CFG1", "CFG2", "CFG3", "LOCK", "CID", "PID", "UID0", "UID1", "UID2", "UCID0", "UCID1", "UCID2", "UCID3",
    "PDID", "DFBA", "ISPSTS", "XOMSTS", "XOMR0", "XOMR1", "XOMR2", "XOMR3", "CPUID", "VTOR",
    "BOOT_RSTSTS", "BOOT_VTOR", "BOOT_CLKSEL0", "BOOT_PLLCTL",
};
#define INFO_COUNT      (sizeof info_names / sizeof info_names[0])

/* ---- Reading the chip ------------------------------------------------------------------------ */

/* Bus faults of plain reads are ignored (they read as zero) instead of ending in the fault handler. The
 * flag has an effect while FAULTMASK is set, which is only for the duration of the reads. */
static void read_memory(uint32_t address, uint8_t *out, uint32_t n)
{
    uint32_t ccr = SCS_CCR;
    uint32_t i, word;

    SCS_CCR = ccr | CCR_BFHFNMIGN;
    __asm volatile("cpsid f" ::: "memory");
    for (i = 0; i < n; i += 4) {
        __asm volatile("ldr %0, [%1]" : "=r"(word) : "r"(address + i) : "memory");   /* also for address 0 */
        out[i] = (uint8_t)word;
        out[i + 1] = (uint8_t)(word >> 8);
        out[i + 2] = (uint8_t)(word >> 16);
        out[i + 3] = (uint8_t)(word >> 24);
    }
    __asm volatile("cpsie f" ::: "memory");
    SCS_CCR = ccr;
}

/* Take the registers of the boot snapshot as they are now. Reset_Handler calls this before anything else,
 * so they show how the bootloader left the chip. Reserved registers read as zero. The copy is kept above the
 * stack, where the start-up code does not clear the RAM. */
void boot_snapshot_take(void)
{
    uint32_t i, w, n = 0;
    uint32_t ccr = SCS_CCR;

    SCS_CCR = ccr | CCR_BFHFNMIGN;
    __asm volatile("cpsid f" ::: "memory");
    for (i = 0; i < SECTION_COUNT; i++) {
        if (sections[i].mode == 'S') {
            for (w = 0; w < sections[i].length; w += 4) {
                boot_snapshot[n++] = *(volatile uint32_t *)(sections[i].address + w);
            }
        }
    }
    __asm volatile("cpsie f" ::: "memory");
    SCS_CCR = ccr;
}

/* A word of the boot snapshot; false when `address` is not in it. */
static bool snapshot_lookup(uint32_t address, uint32_t *out)
{
    uint32_t i, base = 0;

    for (i = 0; i < SECTION_COUNT; i++) {
        if (sections[i].mode != 'S') {
            continue;
        }
        if (address >= sections[i].address && address < sections[i].address + sections[i].length && (address & 3) == 0) {
            *out = boot_snapshot[base + (address - sections[i].address) / 4];
            return true;
        }
        base += sections[i].length / 4;
    }
    return false;
}

/* Allow the ISP commands: the registers are unlocked and the flash controller enabled when they were not.
 * Gives back what isp_close needs to put things as they were. */
static uint32_t isp_open(void)
{
    uint32_t state = 0;

    if (SYS_REGLCTL == 0) {
        SYS_UnlockReg();
        state |= 1;
    }
    if (!(FMC_ISPCTL & ISP_CTL_ISPEN)) {
        FMC_ISPCTL |= ISP_CTL_ISPEN;
        state |= 2;
    }
    return state;
}

static void isp_close(uint32_t state)
{
    if (state & 2) {
        FMC_ISPCTL &= ~ISP_CTL_ISPEN;
    }
    if (state & 1) {
        SYS_REGLCTL = 0;
    }
}

/* One ISP command. Returns 0 and the data word, or -1 when the controller refuses. */
static int isp_command(uint32_t command, uint32_t address, uint32_t *out)
{
    uint32_t timeout;

    FMC_ISPCTL |= ISP_CTL_ISPFF;
    FMC_ISPCMD = command;
    FMC_ISPADDR = address;
    FMC_ISPDAT = 0;
    FMC_ISPTRG = 1;
    for (timeout = 1000000; FMC_ISPTRG & 1; timeout--) {
        if (timeout == 0) {
            return -1;
        }
    }
    if (FMC_ISPCTL & ISP_CTL_ISPFF) {
        FMC_ISPCTL |= ISP_CTL_ISPFF;
        return -1;
    }
    *out = FMC_ISPDAT;
    return 0;
}

static uint16_t crc16(uint16_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t i, bit;

    for (i = 0; i < length; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ---- The sub-commands ------------------------------------------------------------------------ */

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(volatile uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

/* Put `name` at data[offset] and count it in the length of the reply. */
static void put_name(uint32_t offset, const char *name)
{
    uint32_t n = 0;

    while (name[n] != 0 && n < DUMP_NAME_MAX) {
        command_reply.data[offset + n] = (uint8_t)name[n];
        n++;
    }
    command_reply.length += n;
}

static uint32_t hello_command(void)
{
    command_reply.data[0] = DUMP_HELLO;
    command_reply.data[1] = DUMP_PROTOCOL;
    command_reply.data[2] = SECTION_COUNT;
    command_reply.data[3] = INFO_COUNT;
    command_reply.length += 4;
    return REPLY_SEND;
}

static uint32_t section_command(uint32_t index)
{
    if (index >= SECTION_COUNT) {
        return REPLY_ERROR;
    }
    command_reply.data[0] = DUMP_SECTION;
    command_reply.data[1] = index;
    command_reply.data[2] = (uint8_t)sections[index].mode;
    put32(&command_reply.data[3], sections[index].address);
    put32(&command_reply.data[7], sections[index].length);
    command_reply.length += 11;
    put_name(11, sections[index].name);
    return REPLY_SEND;
}

/* The value of info item `index`; false when the chip refuses to give it. */
static bool info_value(uint32_t index, uint32_t *value)
{
    uint32_t config0;

    if (index < 4) {
        return isp_command(ISP_CMD_READ, 0x300000u + 4 * index, value) == 0;
    } else if (index == 4) {                /* CONFIG0 bit 1 is the security lock: 0 = the flash is read-protected */
        if (isp_command(ISP_CMD_READ, 0x300000u, &config0) != 0) {
            return false;
        }
        *value = (config0 >> 1) & 1;
        return true;
    } else if (index == 5) {
        return isp_command(ISP_CMD_READ_CID, 0, value) == 0;
    } else if (index == 6) {
        return isp_command(ISP_CMD_READ_DID, 4, value) == 0;
    } else if (index < 10) {
        return isp_command(ISP_CMD_READ_UID, (index - 7) * 4, value) == 0;
    } else if (index < 14) {
        return isp_command(ISP_CMD_READ_UID, 0x10u + (index - 10) * 4, value) == 0;
    }
    switch (index) {
    case 14: *value = SYS_PDID; return true;
    case 15: *value = FMC_DFBA; return true;
    case 16: *value = FMC_ISPSTS; return true;
    case 17: *value = FMC_XOMSTS; return true;
    case 18: *value = FMC_XOMR0STS; return true;
    case 19: *value = FMC_XOMR1STS; return true;
    case 20: *value = FMC_XOMR2STS; return true;
    case 21: *value = FMC_XOMR3STS; return true;
    case 22: *value = SCS_CPUID; return true;
    case 23: *value = SCS_VTOR; return true;
    case 24: return snapshot_lookup(0x40000004u, value);        /* SYS_RSTSTS */
    case 25: return snapshot_lookup(0xE000ED08u, value);        /* VTOR */
    case 26: return snapshot_lookup(0x40000210u, value);        /* CLK_CLKSEL0 */
    case 27: return snapshot_lookup(0x40000240u, value);        /* CLK_PLLCTL */
    }
    return false;
}

static uint32_t info_command(uint32_t index)
{
    uint32_t value = 0;
    uint32_t state;
    bool ok;

    if (index >= INFO_COUNT) {
        return REPLY_ERROR;
    }
    state = isp_open();
    ok = info_value(index, &value);
    isp_close(state);
    command_reply.data[0] = DUMP_INFO;
    command_reply.data[1] = index;
    command_reply.data[2] = ok ? 0 : 1;
    put32(&command_reply.data[3], ok ? value : 0);
    command_reply.length += 7;
    put_name(7, info_names[index]);
    return REPLY_SEND;
}

/* Is [address, address + n) a range that `mode` may read? */
static bool range_ok(char mode, uint32_t address, uint32_t n)
{
    uint32_t end = address + n, i;

    if (n == 0 || n > DUMP_CHUNK || (n & 3) || (address & 3) || end < address) {
        return false;
    }
    if (mode == 'I') {
        return end <= ISP_LIMIT;
    }
    if (mode == 'S') {
        for (i = 0; i < n; i += 4) {
            uint32_t word;

            if (!snapshot_lookup(address + i, &word)) {
                return false;
            }
        }
        return true;
    }
    if (mode != 'M') {
        return false;
    }
    for (i = 0; i < sizeof memory_ranges / sizeof memory_ranges[0]; i++) {
        if (address >= memory_ranges[i].start && end <= memory_ranges[i].end) {
            return true;
        }
    }
    return false;
}

static uint32_t read_command(const uint8_t *request)
{
    uint8_t data[DUMP_CHUNK];
    char mode = (char)request[4];
    uint32_t address = get32(request + 5);
    uint32_t n = request[9];
    uint32_t i, word, state;
    bool failed = false;
    uint16_t crc;

    if (!range_ok(mode, address, n)) {
        return REPLY_ERROR;
    }
    if (mode == 'M') {
        read_memory(address, data, n);
    } else {
        state = isp_open();
        for (i = 0; i < n && !failed; i += 4) {
            if (mode == 'I') {
                failed = isp_command(ISP_CMD_READ, address + i, &word) != 0;
            } else {
                failed = !snapshot_lookup(address + i, &word);
            }
            data[i] = (uint8_t)word;
            data[i + 1] = (uint8_t)(word >> 8);
            data[i + 2] = (uint8_t)(word >> 16);
            data[i + 3] = (uint8_t)(word >> 24);
        }
        isp_close(state);
    }
    command_reply.data[0] = DUMP_READ;
    command_reply.data[1] = failed ? 1 : 0;
    put32(&command_reply.data[2], address);
    command_reply.data[6] = n;
    command_reply.length += 7;
    if (!failed) {
        for (i = 0; i < n; i++) {
            command_reply.data[7 + i] = data[i];
        }
        crc = crc16(crc16(0xffff, (const uint8_t *)&address, 4), data, n);      /* the address is little endian here too */
        command_reply.data[7 + n] = (uint8_t)crc;
        command_reply.data[8 + n] = (uint8_t)(crc >> 8);
        command_reply.length += n + 2;
    }
    return REPLY_SEND;
}

/* CMD_DUMP. Returns the status command_process expects: 0 to send command_reply, 1 for an error. */
uint32_t dump_command(const uint8_t *request)
{
    switch (request[3]) {
    case DUMP_HELLO:
        return hello_command();
    case DUMP_SECTION:
        return section_command(request[4]);
    case DUMP_INFO:
        return info_command(request[4]);
    case DUMP_READ:
        return read_command(request);
    }
    return REPLY_ERROR;
}
