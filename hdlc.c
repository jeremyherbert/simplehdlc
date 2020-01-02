/* SPDX-License-Identifier: MIT */

#include "hdlc.h"
#include "hdlc_crc32.h"

void hdlc_init(hdlc_context_t *context, uint8_t *rx_buffer, size_t rx_buffer_len, const hdlc_callbacks_t *callbacks, void *usr_ptr) {
    context->rx_buffer = rx_buffer;
    context->rx_buffer_len = rx_buffer_len;

    context->callbacks = *callbacks;
    context->user_ptr = usr_ptr;

    context->escape_next = false;
    context->expected_len = 0;
    context->rx_count = 0;
    context->state = HDLC_STATE_WAITING_FOR_FRAME_MARKER;
}

void hdlc_parse(hdlc_context_t *context, const uint8_t *data, size_t len) {
    for (size_t i=0; i<len; i++) {
        uint8_t c = data[i];

        // wait for frame boundary marker
        if (c == HDLC_BOUNDARY_MARKER) {
            context->expected_len = 0;
            context->rx_count = 0;
            context->rx_crc32 = 0;
            context->escape_next = false;
            context->state = HDLC_STATE_CONSUMING_SIZE_LSB;
            continue;
        }

        if (context->state == HDLC_STATE_WAITING_FOR_FRAME_MARKER) {
            continue;
        }

        if (context->escape_next) {
            c ^= (1 << 5);
            context->escape_next = false;
        } else if (c == HDLC_ESCAPE_MARKER) {
            context->escape_next = true;
            continue;
        }

        if (context->state == HDLC_STATE_CONSUMING_SIZE_LSB) {
            context->expected_len |= c;
            context->state = HDLC_STATE_CONSUMING_SIZE_MSB;

        } else if (context->state == HDLC_STATE_CONSUMING_SIZE_MSB) {
            context->expected_len |= (c << 8);
            context->expected_len += 4; // for the CRC32

            if (context->expected_len > (context->rx_buffer_len) || context->expected_len < 5) {
                // packet is too large or too small, so ignore it
                context->state = HDLC_STATE_WAITING_FOR_FRAME_MARKER;
            } else {
                context->state = HDLC_STATE_CONSUMING_PAYLOAD;
            }

        } else if (context->state == HDLC_STATE_CONSUMING_PAYLOAD) {
            if (context->rx_count < context->expected_len-4) {
                context->rx_buffer[context->rx_count++] = c;
            } else {
                context->rx_crc32 |= c << 24;
                context->rx_count++;

                if (context->rx_count == context->expected_len) {
                    uint32_t crc32 = compute_crc32(context->rx_buffer, context->rx_count-4);

                    if (crc32 == context->rx_crc32) {
                        context->callbacks.rx_packet_callback(context->rx_buffer, context->rx_count-4, context->user_ptr);
                    }

                    context->state = HDLC_STATE_WAITING_FOR_FRAME_MARKER;
                } else {
                    context->rx_crc32 >>= 8;
                }
            }
        }
    }
}

static void escape_and_add_to_buffer(uint8_t byte, uint8_t *buffer, size_t *index) {
    if (byte == HDLC_BOUNDARY_MARKER || byte == HDLC_ESCAPE_MARKER) {
        buffer[(*index)++] = HDLC_ESCAPE_MARKER;
        buffer[(*index)++] = byte ^ (1 << 5);
    } else {
        buffer[(*index)++] = byte;
    }
}

static size_t get_escaped_size(const uint8_t *bytes, size_t len) {
    size_t escaped_size = 0;
    for (size_t i=0; i<len; i++) {
        if (bytes[i] == HDLC_BOUNDARY_MARKER || bytes[i] == HDLC_ESCAPE_MARKER) {
            escaped_size += 2;
        } else {
            escaped_size += 1;
        }
    }

    return escaped_size;
}

size_t hdlc_get_encoded_size(const uint8_t *payload, uint16_t len) {
    uint32_t crc32 = compute_crc32(payload, len);

    return 1 + get_escaped_size((uint8_t *)&len, sizeof(len)) +
           get_escaped_size(payload, len) +
           get_escaped_size((uint8_t *)&crc32, sizeof(crc32));
}

hdlc_error_code_t
hdlc_encode_to_buffer(uint8_t *buffer, size_t buffer_len, size_t *encoded_size, const uint8_t *payload,
                      uint16_t payload_len) {

    if (buffer_len < 7 || hdlc_get_encoded_size(payload, payload_len) > buffer_len) return HDLC_ERROR_BUFFER_TOO_SMALL;

    size_t output_index = 0;

    buffer[output_index++] = HDLC_BOUNDARY_MARKER;

    escape_and_add_to_buffer(payload_len & 0xFF, buffer, &output_index);
    escape_and_add_to_buffer((payload_len & 0xFF00) >> 8, buffer, &output_index);

    for (uint16_t i=0; i<payload_len; i++) {
        escape_and_add_to_buffer(payload[i], buffer, &output_index);
    }

    uint32_t crc32 = compute_crc32(payload, payload_len);
    escape_and_add_to_buffer(crc32 & 0xFF, buffer, &output_index);
    escape_and_add_to_buffer((crc32 & 0xFF00) >> 8, buffer, &output_index);
    escape_and_add_to_buffer((crc32 & 0xFF0000) >> 16, buffer, &output_index);
    escape_and_add_to_buffer((crc32 & 0xFF000000) >> 24, buffer, &output_index);

    *encoded_size = output_index;

    return HDLC_OK;
}

static void escape_and_send_to_callback(hdlc_context_t *context, uint8_t byte) {
    if (byte == HDLC_BOUNDARY_MARKER || byte == HDLC_ESCAPE_MARKER) {
        context->callbacks.tx_byte_callback(HDLC_ESCAPE_MARKER, context->user_ptr);
        context->callbacks.tx_byte_callback(byte ^ (1 << 5), context->user_ptr);
    } else {
        context->callbacks.tx_byte_callback(byte, context->user_ptr);
    }
}

hdlc_error_code_t
hdlc_encode_to_callback(hdlc_context_t *context, const uint8_t *payload, uint16_t payload_len, bool flush) {
    context->callbacks.tx_byte_callback(HDLC_BOUNDARY_MARKER, context->user_ptr);

    escape_and_send_to_callback(context, payload_len & 0xFF);
    escape_and_send_to_callback(context, (payload_len & 0xFF00) >> 8);

    for (uint16_t i=0; i<payload_len; i++) {
        escape_and_send_to_callback(context, payload[i]);
    }

    uint32_t crc32 = compute_crc32(payload, payload_len);
    escape_and_send_to_callback(context, crc32 & 0xFF);
    escape_and_send_to_callback(context, (crc32 & 0xFF00) >> 8);
    escape_and_send_to_callback(context, (crc32 & 0xFF0000) >> 16);
    escape_and_send_to_callback(context, (crc32 & 0xFF000000) >> 24);

    if (flush) {
        context->callbacks.tx_flush_buffer_callback(context->user_ptr);
    }

    return HDLC_OK;
}