/* Flash memory controller (ISP) helpers. */

#include "ss321.h"

#define ISPCMD_READ         0x00u
#define ISPCMD_PROGRAM      0x21u
#define ISPCMD_PAGE_ERASE   0x22u
#define ISPCMD_BANK_ERASE   0x23u
#define ISPCMD_BLOCK_ERASE  0x25u
#define ISPCTL_ISPEN        0x01u
#define ISPCTL_ISPFF        0x40u
#define SPROM_BASE          0x200000u

static void isp_wait(void)
{
    while (FMC_ISPTRG & 1) {
    }
}

void FMC_Open(void)       /* enable ISP */
{
    FMC_ISPCTL |= ISPCTL_ISPEN;
}

void FMC_Close(void)       /* disable ISP */
{
    FMC_ISPCTL &= ~ISPCTL_ISPEN;
}

/* Read one word of flash. */
uint32_t FMC_Read(uint32_t address)
{
    FMC_ISPCMD = ISPCMD_READ;
    FMC_ISPADDR = address;
    FMC_ISPTRG = 1;
    isp_wait();
    return FMC_ISPDAT;
}

/* Program one word of flash. */
void FMC_Write(uint32_t address, uint32_t data)
{
    FMC_ISPCMD = ISPCMD_PROGRAM;
    FMC_ISPADDR = address;
    FMC_ISPDAT = data;
    FMC_ISPTRG = 1;
    isp_wait();
}

/* Erase the secure ROM (the key written is the one the controller wants for it). */
uint32_t FMC_EraseSPROM(void)
{
    uint32_t result = 0;

    FMC_ISPCMD = ISPCMD_PAGE_ERASE;
    FMC_ISPADDR = SPROM_BASE;
    FMC_ISPDAT = 0x55aa03;
    FMC_ISPTRG = 1;
    isp_wait();
    if (FMC_ISPCTL & ISPCTL_ISPFF) {
        FMC_ISPCTL |= ISPCTL_ISPFF;
        result = 0xffffffff;
    }
    return result;
}

/* Erase the page at `address`. Returns 0 or -1 when the controller reports a failure. */
int32_t FMC_Erase(uint32_t address)
{
    int32_t result = 0;

    if (address == SPROM_BASE) {
        result = (int32_t)FMC_EraseSPROM();
    }
    if (result == 0) {
        FMC_ISPCMD = ISPCMD_PAGE_ERASE;
        FMC_ISPADDR = address;
        FMC_ISPTRG = 1;
        isp_wait();
        if (FMC_ISPCTL & ISPCTL_ISPFF) {
            FMC_ISPCTL |= ISPCTL_ISPFF;
            result = -1;
        }
    }
    return result;
}

/* Erase size bytes from address, using the largest erase unit that fits (256 KiB, 16 KiB
 * or 4 KiB). Returns 0, or -1 on a flash error. */
int32_t FMC_EraseRange(uint32_t address, uint32_t size)
{
    while (size != 0) {
        uint32_t command, unit;

        if (size >= 0x40000 && (address & 0x3ffff) == 0) {
            command = ISPCMD_BANK_ERASE;
            unit = 0x40000;
        } else if (size >= 0x4000 && (address & 0x3fff) == 0) {
            command = ISPCMD_BLOCK_ERASE;
            unit = 0x4000;
        } else {
            command = ISPCMD_PAGE_ERASE;
            unit = 0x1000;
        }
        FMC_ISPCMD = command;
        FMC_ISPADDR = address;
        FMC_ISPTRG = 1;
        __asm volatile("isb" ::: "memory");
        isp_wait();
        if (FMC_ISPCTL & ISPCTL_ISPFF) {
            FMC_ISPCTL |= ISPCTL_ISPFF;
            return -1;
        }
        address += unit;
        size -= unit;
    }
    return 0;
}

/* Run an ISP command over a range: read (0), program (0x21) or page erase (0x22), one word
 * (or page) at a time; the data goes to/from buf. Returns 0, or -1 on a flash error. */
int32_t FMC_IspRange(uint32_t command, uint32_t address, uint32_t end, uint32_t *buf)
{
    while (address < end) {
        uint32_t status;

        FMC_ISPCMD = command;
        FMC_ISPADDR = address;
        if (command == ISPCMD_PROGRAM) {
            FMC_ISPDAT = *buf;
        }
        FMC_ISPTRG = 1;
        __asm volatile("isb" ::: "memory");
        isp_wait();
        status = FMC_ISPCTL;
        if (status & ISPCTL_ISPFF) {
            FMC_ISPCTL = status;
            return -1;
        }
        if (command == ISPCMD_READ) {
            *buf = FMC_ISPDAT;
        }
        address += (command == ISPCMD_PAGE_ERASE) ? 0x1000 : 4;
        buf++;
    }
    return 0;
}

/* Read the flash words from start to end into buf. */
void FMC_ReadRange(uint32_t start, uint32_t end, uint32_t *buf)
{
    FMC_IspRange(ISPCMD_READ, start, end, buf);
}

/* Program the words of `data` to flash from start to end. */
void FMC_WriteRange(uint32_t start, uint32_t end, uint32_t *data)
{
  FMC_IspRange(0x21, start, end, data);
  return;
}
