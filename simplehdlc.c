/* SPDX-License-Identifier: MIT */

#include "simplehdlc.h"
#include "simplehdlc_crc32.h"

void simplehdlc_init(simplehdlc_context_t *context, uint8_t *parse_buffer, size_t parse_buffer_len, const simplehdlc_callbacks_t *callbacks, void *user_ptr) {
    context->rx_buffer = parse_buffer;
    context->rx_buffer_len = parse_buffer_len;

    context->callbacks = *callbacks;
    context->user_ptr = user_ptr;

    context->escape_next = false;
    context->expected_len = 0;
    context->rx_count = 0;
    context->state = SIMPLEHDLC_STATE_WAITING_FOR_FRAME_MARKER;
}

void simplehdlc_parse(simplehdlc_context_t *context, const uint8_t *data, size_t len) {
    for (size_t i=0; i<len; i++) {
        uint8_t c = data[i];

        // wait for frame boundary marker
        if (c == SIMPLEHDLC_BOUNDARY_MARKER) {
            context->expected_len = 0;
            context->rx_count = 0;
            context->rx_crc32 = 0;
            context->escape_next = false;
            context->state = SIMPLEHDLC_STATE_CONSUMING_SIZE_MSB;
            continue;
        }

        if (context->state == SIMPLEHDLC_STATE_WAITING_FOR_FRAME_MARKER) {
            continue;
        }

        if (context->escape_next) {
            c ^= (1 << 5);
            context->escape_next = false;
        } else if (c == SIMPLEHDLC_ESCAPE_MARKER) {
            context->escape_next = true;
            continue;
        }

        if (context->state == SIMPLEHDLC_STATE_CONSUMING_SIZE_MSB) {
            context->expected_len |= c << 8;
            context->state = SIMPLEHDLC_STATE_CONSUMING_SIZE_LSB;

        } else if (context->state == SIMPLEHDLC_STATE_CONSUMING_SIZE_LSB) {
            context->expected_len |= c;
            context->expected_len += 4; // for the CRC32

            if (context->expected_len > (context->rx_buffer_len) || context->expected_len < 5) {
                // packet is too large or too small, so ignore it
                context->state = SIMPLEHDLC_STATE_WAITING_FOR_FRAME_MARKER;
            } else {
                context->state = SIMPLEHDLC_STATE_CONSUMING_PAYLOAD;
            }

        } else if (context->state == SIMPLEHDLC_STATE_CONSUMING_PAYLOAD) {
            if (context->rx_count < context->expected_len-4) {
                context->rx_buffer[context->rx_count++] = c;
            } else {
                context->rx_crc32 |= c;
                context->rx_count++;

                if (context->rx_count == context->expected_len) {
                    uint32_t crc32 = simplehdlc_compute_crc32(context->rx_buffer, context->rx_count - 4);

                    if (crc32 == context->rx_crc32) {
                        context->callbacks.rx_packet_callback(context->rx_buffer, context->rx_count-4, context->user_ptr);
                    }

                    context->state = SIMPLEHDLC_STATE_WAITING_FOR_FRAME_MARKER;
                } else {
                    context->rx_crc32 <<= 8;
                }
            }
        }
    }
}

static void escape_and_add_to_buffer(uint8_t byte, uint8_t *buffer, size_t *index) {
    if (byte == SIMPLEHDLC_BOUNDARY_MARKER || byte == SIMPLEHDLC_ESCAPE_MARKER) {
        buffer[(*index)++] = SIMPLEHDLC_ESCAPE_MARKER;
        buffer[(*index)++] = byte ^ (1 << 5);
    } else {
        buffer[(*index)++] = byte;
    }
}

static size_t get_escaped_size(const uint8_t *bytes, size_t len) {
    size_t escaped_size = 0;
    for (size_t i=0; i<len; i++) {
        if (bytes[i] == SIMPLEHDLC_BOUNDARY_MARKER || bytes[i] == SIMPLEHDLC_ESCAPE_MARKER) {
            escaped_size += 2;
        } else {
            escaped_size += 1;
        }
    }

    return escaped_size;
}

size_t simplehdlc_get_encoded_size(const uint8_t *payload, uint16_t len) {
    uint32_t crc32 = simplehdlc_compute_crc32(payload, len);

    return 1 + get_escaped_size((uint8_t *)&len, sizeof(len)) +
           get_escaped_size(payload, len) +
           get_escaped_size((uint8_t *)&crc32, sizeof(crc32));
}

simplehdlc_error_code_t
simplehdlc_encode_to_buffer(uint8_t *buffer, size_t buffer_len, size_t *encoded_size, const uint8_t *payload,
                            uint16_t payload_len) {

    size_t expected_size = simplehdlc_get_encoded_size(payload, payload_len);
    if (buffer_len < 7 || expected_size > buffer_len) return SIMPLEHDLC_ERROR_BUFFER_TOO_SMALL;

    size_t output_index = 0;

    buffer[output_index++] = SIMPLEHDLC_BOUNDARY_MARKER;

    escape_and_add_to_buffer((payload_len & 0xFF00) >> 8, buffer, &output_index);
    escape_and_add_to_buffer(payload_len & 0xFF, buffer, &output_index);

    for (uint16_t i=0; i<payload_len; i++) {
        escape_and_add_to_buffer(payload[i], buffer, &output_index);
    }

    uint32_t crc32 = simplehdlc_compute_crc32(payload, payload_len);

    escape_and_add_to_buffer((crc32 & 0xFF000000) >> 24, buffer, &output_index);
    escape_and_add_to_buffer((crc32 & 0xFF0000) >> 16, buffer, &output_index);
    escape_and_add_to_buffer((crc32 & 0xFF00) >> 8, buffer, &output_index);
    escape_and_add_to_buffer(crc32 & 0xFF, buffer, &output_index);

    if (output_index != expected_size) return SIMPLEHDLC_ERROR_INTERNAL_ENCODE_LENGTH_MISMATCH;

    *encoded_size = output_index;

    return SIMPLEHDLC_OK;
}

static void escape_and_send_to_callback(simplehdlc_context_t *context, uint8_t byte) {
    if (byte == SIMPLEHDLC_BOUNDARY_MARKER || byte == SIMPLEHDLC_ESCAPE_MARKER) {
        context->callbacks.tx_byte_callback(SIMPLEHDLC_ESCAPE_MARKER, context->user_ptr);
        context->callbacks.tx_byte_callback(byte ^ (1 << 5), context->user_ptr);
    } else {
        context->callbacks.tx_byte_callback(byte, context->user_ptr);
    }
}

simplehdlc_error_code_t
simplehdlc_encode_to_callback(simplehdlc_context_t *context, const uint8_t *payload, uint16_t payload_len, bool flush) {
    if (context->callbacks.tx_byte_callback == NULL) return SIMPLEHDLC_ERROR_CALLBACK_MISSING;

    context->callbacks.tx_byte_callback(SIMPLEHDLC_BOUNDARY_MARKER, context->user_ptr);

    escape_and_send_to_callback(context, (payload_len & 0xFF00) >> 8);
    escape_and_send_to_callback(context, payload_len & 0xFF);

    for (uint16_t i=0; i<payload_len; i++) {
        escape_and_send_to_callback(context, payload[i]);
    }

    uint32_t crc32 = simplehdlc_compute_crc32(payload, payload_len);
    escape_and_send_to_callback(context, (crc32 & 0xFF000000) >> 24);
    escape_and_send_to_callback(context, (crc32 & 0xFF0000) >> 16);
    escape_and_send_to_callback(context, (crc32 & 0xFF00) >> 8);
    escape_and_send_to_callback(context, crc32 & 0xFF);

    if (flush) {
        if (context->callbacks.tx_flush_buffer_callback != NULL) {
            context->callbacks.tx_flush_buffer_callback(context->user_ptr);
        } else {
            return SIMPLEHDLC_ERROR_CALLBACK_MISSING;
        }
    }

    return SIMPLEHDLC_OK;
}