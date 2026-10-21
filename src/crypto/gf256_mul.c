/* Multiplication in GF(2^8) with the AES polynomial x^8 + x^4 + x^3 + x + 1: x times the
 * small constant whose bits are in `factor` (0..15). */
uint8_t gf256_mul(uint32_t factor, uint8_t x)
{
    uint8_t product = 0;
    uint32_t bit;

    for (bit = 0; bit < 4; bit++) {
        if ((factor >> bit) & 1) {
            product ^= x;
        }
        x = (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
    }
    return product;
}
