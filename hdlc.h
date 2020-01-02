/* SPDX-License-Identifier: MIT */

#ifndef SIMPLEHDLC_HDLC_H
#define SIMPLEHDLC_HDLC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define HDLC_BOUNDARY_MARKER 0x7E
#define HDLC_ESCAPE_MARKER 0x7D

typedef struct {
    void (*rx_packet_callback)(const uint8_t *payload, uint16_t len, void *user_ptr);
    void (*tx_byte_callback)(uint8_t byte, void *user_ptr);
    void (*tx_flush_buffer_callback)(void *user_ptr);
} hdlc_callbacks_t;

typedef enum {
    HDLC_STATE_WAITING_FOR_FRAME_MARKER = 0,
    HDLC_STATE_CONSUMING_SIZE_LSB = 1,
    HDLC_STATE_CONSUMING_SIZE_MSB = 2,
    HDLC_STATE_CONSUMING_PAYLOAD = 3,
} hdlc_parser_state_t;

typedef enum {
    HDLC_OK = 0,
    HDLC_ERROR_BUFFER_TOO_SMALL = 1
} hdlc_error_code_t;

typedef struct {
    uint8_t *rx_buffer;
    size_t rx_buffer_len;
    size_t rx_count;
    uint32_t rx_crc32;

    hdlc_callbacks_t callbacks;
    void *user_ptr;

    hdlc_parser_state_t state;
    size_t expected_len;
    bool escape_next;
} hdlc_context_t;

void hdlc_init(hdlc_context_t *context, uint8_t *rx_buffer, size_t rx_buffer_len, const hdlc_callbacks_t *callbacks, void *usr_ptr);
void hdlc_parse(hdlc_context_t *context, const uint8_t *data, size_t len);
hdlc_error_code_t
hdlc_encode_to_callback(hdlc_context_t *context, const uint8_t *payload, uint16_t payload_len, bool flush);

size_t hdlc_get_encoded_size(const uint8_t *payload, uint16_t len);
hdlc_error_code_t
hdlc_encode_to_buffer(uint8_t *buffer, size_t buffer_len, size_t *encoded_size, const uint8_t *payload,
                      uint16_t payload_len);

#endif //SIMPLEHDLC_HDLC_H
