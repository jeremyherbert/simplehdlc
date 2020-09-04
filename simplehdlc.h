/* SPDX-License-Identifier: MIT */

#ifndef SIMPLEHDLC_SIMPLEHDLC_H
#define SIMPLEHDLC_SIMPLEHDLC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SIMPLEHDLC_BOUNDARY_MARKER 0x7E
#define SIMPLEHDLC_ESCAPE_MARKER 0x7D

typedef struct {
    void (*rx_packet_callback)(const uint8_t *payload, uint16_t len, void *user_ptr);
    void (*tx_byte_callback)(uint8_t byte, void *user_ptr);
    void (*tx_flush_buffer_callback)(void *user_ptr);
} simplehdlc_callbacks_t;

typedef enum {
    SIMPLEHDLC_STATE_WAITING_FOR_FRAME_MARKER = 0,
    SIMPLEHDLC_STATE_CONSUMING_SIZE_MSB = 1,
    SIMPLEHDLC_STATE_CONSUMING_SIZE_LSB = 2,
    SIMPLEHDLC_STATE_CONSUMING_PAYLOAD = 3,
} hdlc_parser_state_t;

typedef enum {
    SIMPLEHDLC_OK = 0,
    SIMPLEHDLC_ERROR_BUFFER_TOO_SMALL = 1,
    SIMPLEHDLC_ERROR_CALLBACK_MISSING = 2,
    SIMPLEHDLC_ERROR_INTERNAL_ENCODE_LENGTH_MISMATCH = 3
} simplehdlc_error_code_t;

typedef struct {
    uint8_t *rx_buffer;
    size_t rx_buffer_len;
    size_t rx_count;
    uint32_t rx_crc32;

    simplehdlc_callbacks_t callbacks;
    void *user_ptr;

    hdlc_parser_state_t state;
    size_t expected_len;
    bool escape_next;
} simplehdlc_context_t;

void simplehdlc_init(simplehdlc_context_t *context, uint8_t *parse_buffer, size_t parse_buffer_len, const simplehdlc_callbacks_t *callbacks, void *user_ptr);
void simplehdlc_parse(simplehdlc_context_t *context, const uint8_t *data, size_t len);
simplehdlc_error_code_t
simplehdlc_encode_to_callback(simplehdlc_context_t *context, const uint8_t *payload, uint16_t payload_len, bool flush);

size_t simplehdlc_get_encoded_size(const uint8_t *payload, uint16_t len);
simplehdlc_error_code_t
simplehdlc_encode_to_buffer(uint8_t *buffer, size_t buffer_len, size_t *encoded_size, const uint8_t *payload,
                            uint16_t payload_len);

#ifdef __cplusplus
}
#endif
#endif //SIMPLEHDLC_SIMPLEHDLC_H
