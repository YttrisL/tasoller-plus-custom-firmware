/* AES-128 as the official firmware does it: a byte oriented implementation with the S-boxes
 * and the expanded key kept in RAM (fixed addresses, see state.h). The state is a 4x4 byte
 * matrix s[row * 4 + column]; the routines work on 16 bytes that are first transposed from
 * the block order. */

#include "ss321.h"

extern const uint8_t aes_sbox_rom[];      /* S-box */
extern const uint8_t aes_inv_sbox_rom[];      /* inverse S-box */

static const uint8_t rcon[10] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

/* Round key expansion of a 16 byte key into `out` (176 bytes). */
void aes_expand_key(const uint8_t *key, uint8_t *out)
{
    uint8_t temp[4];
    int round, col, row, k;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            out[row * 4 + col] = key[row + col * 4];
        }
    }
    for (round = 1; round < 11; round++) {
        for (col = 0; col < 4; col++) {
            for (row = 0; row < 4; row++) {
                if (col == 0) {
                    temp[row] = out[(round - 1) * 16 + row * 4 + 3];
                } else {
                    temp[row] = out[round * 16 + row * 4 + col - 1];
                }
            }
            if (col == 0) {
                uint8_t first = temp[0];

                for (k = 0; k < 3; k++) {
                    temp[k] = aes_sbox[temp[k + 1]];
                }
                temp[3] = aes_sbox[first];
                temp[0] ^= rcon[round - 1];
            }
            for (row = 0; row < 4; row++) {
                out[round * 16 + row * 4 + col] = out[(round - 1) * 16 + row * 4 + col] ^ temp[row];
            }
        }
    }
}

/* Load the S-boxes into RAM and expand `key`. */
void aes_set_key(const uint8_t *key)
{
    uint8_t sbox[256];
    uint8_t inv_sbox[256];

    memcpy(sbox, aes_sbox_rom, 0x100);
    memcpy(inv_sbox, aes_inv_sbox_rom, 0x100);
    memcpy((void *)aes_sbox, sbox, 0x100);
    memcpy((void *)aes_inv_sbox, inv_sbox, 0x100);
    aes_expand_key(key, (uint8_t *)aes_round_keys);
}

static void add_round_key(uint8_t *state, const volatile uint8_t *round_key)
{
    int i;

    for (i = 0; i < 16; i++) {
        state[i] ^= round_key[i];
    }
}

static void sub_bytes(uint8_t *state)
{
    int i;

    for (i = 0; i < 16; i++) {
        state[i] = aes_sbox[state[i]];
    }
}

static void inv_sub_bytes(uint8_t *state)
{
    int i;

    for (i = 0; i < 16; i++) {
        state[i] = aes_inv_sbox[state[i]];
    }
}

static void shift_rows(uint8_t *state)
{
    uint8_t temp[4];
    int row, col;

    for (row = 1; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            temp[col] = state[row * 4 + (col + row) % 4];
        }
        for (col = 0; col < 4; col++) {
            state[row * 4 + col] = temp[col];
        }
    }
}

static void inv_shift_rows(uint8_t *state)
{
    uint8_t temp[4];
    int row, col;

    for (row = 1; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            temp[col] = state[row * 4 + (col - row + 4) % 4];
        }
        for (col = 0; col < 4; col++) {
            state[row * 4 + col] = temp[col];
        }
    }
}

static void mix_columns(uint8_t *state)
{
    uint8_t column[4];
    int col, row;

    for (col = 0; col < 4; col++) {
        for (row = 0; row < 4; row++) {
            column[row] = state[row * 4 + col];
        }
        for (row = 0; row < 4; row++) {
            state[row * 4 + col] = gf256_mul(2, column[row]) ^ gf256_mul(3, column[(row + 1) % 4]) ^
                                   gf256_mul(1, column[(row + 2) % 4]) ^ gf256_mul(1, column[(row + 3) % 4]);
        }
    }
}

static void inv_mix_columns(uint8_t *state)
{
    uint8_t column[4];
    int col, row;

    for (col = 0; col < 4; col++) {
        for (row = 0; row < 4; row++) {
            column[row] = state[row * 4 + col];
        }
        for (row = 0; row < 4; row++) {
            state[row * 4 + col] = gf256_mul(0xe, column[row]) ^ gf256_mul(0xb, column[(row + 1) % 4]) ^
                                   gf256_mul(0xd, column[(row + 2) % 4]) ^ gf256_mul(9, column[(row + 3) % 4]);
        }
    }
}

/* Encrypt one block in place. */
uint8_t *aes_encrypt_block(uint8_t *block)
{
    uint8_t state[16];
    int round, row, col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            state[col + row * 4] = block[row + col * 4];
        }
    }
    add_round_key(state, aes_round_keys);
    for (round = 1; round < 11; round++) {
        sub_bytes(state);
        shift_rows(state);
        if (round != 10) {
            mix_columns(state);
        }
        add_round_key(state, aes_round_keys + round * 16);
    }
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            block[row + col * 4] = state[col + row * 4];
        }
    }
    return block;
}

/* Encrypt len bytes (a multiple of 16) in place, block by block (ECB). */
uint8_t *aes_encrypt(uint8_t *data, int len)
{
    int offset;

    for (offset = 0; offset < len; offset += 16) {
        aes_encrypt_block(data + offset);
    }
    return data;
}

/* Decrypt one block in place. */
uint8_t *aes_decrypt_block(uint8_t *block)
{
    uint8_t state[16];
    int round, row, col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            state[col + row * 4] = block[row + col * 4];
        }
    }
    add_round_key(state, aes_round_keys + 160);
    for (round = 9; round >= 0; round--) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, aes_round_keys + round * 16);
        if (round != 0) {
            inv_mix_columns(state);
        }
    }
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            block[row + col * 4] = state[col + row * 4];
        }
    }
    return block;
}

/* Decrypt len bytes (a multiple of 16) in place. */
uint8_t *aes_decrypt(uint8_t *data, int len)
{
    int offset;

    for (offset = 0; offset < len; offset += 16) {
        aes_decrypt_block(data + offset);
    }
    return data;
}
