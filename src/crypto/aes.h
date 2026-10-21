#pragma once
/* What aes.c defines. */

void aes_expand_key(const uint8_t *key, uint8_t *out);
void aes_set_key(const uint8_t *key);
uint8_t * aes_encrypt_block(uint8_t *block);
uint8_t * aes_encrypt(uint8_t *data, int len);
uint8_t * aes_decrypt_block(uint8_t *block);
uint8_t * aes_decrypt(uint8_t *data, int len);
