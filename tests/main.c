/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../hdlc.h"
#include "../hdlc_crc32.h"


static void crc32_sanity_check(void **state) {
    uint8_t payload[5] = {1, 2, 3, 4, 5};
    uint32_t crc32 = compute_crc32(payload, 5);
    assert_true(crc32 == 0x470B99F4); // calculated using python binascii module
}

static void encode_test_too_small(void **state) {
    uint8_t buffer[7];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[] = {1};

    for (int i=0; i<7; i++) {
        assert_true(hdlc_encode_to_buffer(buffer, i, &encoded_size, payload, sizeof(payload)) == HDLC_ERROR_BUFFER_TOO_SMALL);
        assert_int_equal(encoded_size, 0xFFFF);
    }
}

static void encode_test_zero_length_payload(void **state) {
    uint8_t buffer[7];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[] = {1};

    assert_true(hdlc_encode_to_buffer(buffer, 7, &encoded_size, payload, 0) == HDLC_OK);
    assert_int_equal(encoded_size, 7);
    assert_int_equal(encoded_size, hdlc_get_encoded_size(payload, 0));
}

static void encode_sanity_check(void **state) {
    uint8_t buffer[8];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[1] = {1};

    assert_true(hdlc_encode_to_buffer(buffer, sizeof(buffer), &encoded_size, payload, sizeof(payload)) == HDLC_OK);
    assert_int_equal(encoded_size, 8);
    assert_int_equal(encoded_size, hdlc_get_encoded_size(payload, sizeof(payload)));

    uint8_t expected[] = {0x7E, 0x01, 0x00, 0x01, 0x1B, 0xDF, 0x05, 0xA5};
    assert_memory_equal(expected, buffer, encoded_size);
}

static void encode_test_escaping(void **state) {
    uint8_t buffer[11];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[2] = {0x7E, 0x7D};

    assert_true(hdlc_encode_to_buffer(buffer, sizeof(buffer), &encoded_size, payload, sizeof(payload)) == HDLC_OK);
    assert_int_equal(encoded_size, 11);
    assert_int_equal(encoded_size, hdlc_get_encoded_size(payload, sizeof(payload)));

    uint8_t expected[] = {0x7E, 0x02, 0x00,
                          0x7D, 0x7E ^ (1<<5),
                          0x7D, 0x7D ^ (1<<5),
                          0x06, 0x4B, 0xD1, 0xDE};
    assert_memory_equal(expected, buffer, encoded_size);
}

//////////////////////////////////////////////////////////////////////////////

static uint8_t callback_buffer[512];
static uint8_t callback_buffer_count;
static bool tx_flushed;

static void tx_callback(uint8_t byte, void *user_ptr) {
    callback_buffer[callback_buffer_count++] = byte;
}

static void tx_flush_callback(void *user_ptr) {
    tx_flushed = true;
}

static void encode_test_callback_noflush(void **state) {
    callback_buffer_count = 0;
    tx_flushed = false;

    uint8_t buffer[512];
    uint8_t payload[2] = {0x7E, 0x7D};

    hdlc_callbacks_t callbacks = {0};
    callbacks.tx_byte_callback = tx_callback;
    hdlc_context_t context;
    hdlc_init(&context, buffer, sizeof(buffer), &callbacks, NULL);

    assert_true(hdlc_encode_to_callback(&context, payload, sizeof(payload), false) == HDLC_OK);

    assert_int_equal(callback_buffer_count, 11);

    uint8_t expected[] = {0x7E, 0x02, 0x00,
                          0x7D, 0x7E ^ (1<<5),
                          0x7D, 0x7D ^ (1<<5),
                          0x06, 0x4B, 0xD1, 0xDE};
    assert_memory_equal(expected, callback_buffer, callback_buffer_count);

    assert_false(tx_flushed);
}

static void encode_test_callback_withflush(void **state) {
    callback_buffer_count = 0;
    tx_flushed = false;

    uint8_t buffer[512];
    uint8_t payload[2] = {0x7E, 0x7D};

    hdlc_callbacks_t callbacks = {0};
    callbacks.tx_byte_callback = tx_callback;
    callbacks.tx_flush_buffer_callback = tx_flush_callback;
    hdlc_context_t context;
    hdlc_init(&context, buffer, sizeof(buffer), &callbacks, NULL);

    assert_true(hdlc_encode_to_callback(&context, payload, sizeof(payload), true) == HDLC_OK);

    assert_int_equal(callback_buffer_count, 11);

    uint8_t expected[] = {0x7E, 0x02, 0x00,
                          0x7D, 0x7E ^ (1<<5),
                          0x7D, 0x7D ^ (1<<5),
                          0x06, 0x4B, 0xD1, 0xDE};
    assert_memory_equal(expected, callback_buffer, callback_buffer_count);

    assert_true(tx_flushed);
}


//////////////////////////////////////////////////////////////////////////////

static bool decode_success = false;
static size_t decoded_length = 0;

static void decode_success_callback(const uint8_t *payload, uint16_t payload_len, void *user_ptr) {
    assert_memory_equal(payload, user_ptr, payload_len);

    decoded_length = payload_len;
    decode_success = true;
}

static void parse_sanity_check(void **state) {
    decode_success = false;
    decoded_length = 0;

    uint8_t payload[] = {1};
    uint8_t encoded[] = {0x7E, 0x01, 0x00, 0x01, 0x1B, 0xDF, 0x05, 0xA5};

    uint8_t rx_buffer[512];
    hdlc_context_t context;

    hdlc_callbacks_t callbacks = {0};
    callbacks.rx_packet_callback = decode_success_callback;

    hdlc_init(&context, rx_buffer, sizeof(rx_buffer), &callbacks, (void *)payload);
    hdlc_parse(&context, encoded, sizeof(encoded));

    assert_true(decode_success);
    assert_int_equal(decoded_length, 1);
}

static void encode_parse_sanity_check(void **state) {
    decode_success = false;

    uint8_t buffer[512];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[256];

    for (int i=0; i<256; i++) payload[i] = i;

    assert_true(hdlc_encode_to_buffer(buffer, sizeof(buffer), &encoded_size, payload, sizeof(payload)) == HDLC_OK);
    assert_int_equal(encoded_size, hdlc_get_encoded_size(payload, sizeof(payload)));

    uint8_t rx_buffer[512];
    hdlc_context_t context;

    hdlc_callbacks_t callbacks = {0};
    callbacks.rx_packet_callback = decode_success_callback;

    hdlc_init(&context, rx_buffer, sizeof(rx_buffer), &callbacks, (void *)payload);
    hdlc_parse(&context, buffer, encoded_size);

    assert_true(decode_success);
    assert_int_equal(decoded_length, sizeof(payload));
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(crc32_sanity_check),
            cmocka_unit_test(encode_test_too_small),
            cmocka_unit_test(encode_test_zero_length_payload),
            cmocka_unit_test(encode_sanity_check),
            cmocka_unit_test(encode_test_escaping),

            cmocka_unit_test(encode_test_callback_noflush),
            cmocka_unit_test(encode_test_callback_withflush),

            cmocka_unit_test(parse_sanity_check),

            cmocka_unit_test(encode_parse_sanity_check)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}