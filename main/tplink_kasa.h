/**
 * @file Constants and functions for communicating with TP-Link Kasa IoT smart devices
 */

#ifndef INTELLILIGHT_TPLINK_KASA_H
#define INTELLILIGHT_TPLINK_KASA_H


/* system includes */
#include <string.h>
#include <unistd.h>

/* local includes */
#include "cJSON.h"
#include "wifi.h"


/**
 * @brief Process a received buffer of encrypted data
 * @param raw_buffer Buffer to decrypt, interpret and respond to
 * @param buffer_len Length of input buffer
 * @param include_header True if buffers contain a header
 * @return Length of encrypted reply
 */
int tplink_kasa_process_buffer(char * raw_buffer, const int buffer_len, const bool include_header);

/**
 * @brief Decrypt using XOR Autokey Cipher with starting key of 171
 * @param encrypted_payload Input payload to decrypt
 * @param encrypted_len Length of input buffer
 * @param decrypted_payload Output decrypted payload
 * @param include_header True if the encrypted payload contains a header
 * @return length of decrypted data
 */
int tplink_kasa_decrypt(const char * encrypted_payload, const int encrypted_len, char * decrypted_payload, const bool include_header);

/**
 * @brief Encrypt using XOR Autokey Cipher with starting key of 171
 * @param payload Input payload to encrypt as cJSON object
 * @param encrypted_payload Output encrypted payload
 * @param include_header True to prepend the packet with a header
 * @return length of encrypted data
 */
int tplink_kasa_encrypt(const cJSON * payload, char * encypted_payload, const bool include_header);

#endif
