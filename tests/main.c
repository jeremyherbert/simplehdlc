/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <setjmp.h>
#include <cmocka.h>

#include "simplehdlc.h"
#include "simplehdlc_crc32.h"


static void crc32_sanity_check(void **state) {
    uint8_t payload[5] = {1, 2, 3, 4, 5};
    uint32_t crc32 = simplehdlc_compute_crc32(payload, 5);
    assert_true(crc32 == 0x470B99F4); // calculated using python binascii module
}

//////////////////////////////////////////////////////////////////////////////

static void encode_test_too_small(void **state) {
    uint8_t buffer[7];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[] = {1};

    for (int i=0; i<7; i++) {
        assert_true(simplehdlc_encode_to_buffer(buffer, i, &encoded_size, payload, sizeof(payload)) == SIMPLEHDLC_ERROR_BUFFER_TOO_SMALL);
        assert_int_equal(encoded_size, 0xFFFF);
    }
}

static void encode_test_zero_length_payload(void **state) {
    uint8_t buffer[7];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[] = {1};

    assert_true(simplehdlc_encode_to_buffer(buffer, 7, &encoded_size, payload, 0) == SIMPLEHDLC_OK);
    assert_int_equal(encoded_size, 7);
    assert_int_equal(encoded_size, simplehdlc_get_encoded_size(payload, 0));
}

static void encode_sanity_check(void **state) {
    uint8_t buffer[8];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[1] = {1};

    assert_true(simplehdlc_encode_to_buffer(buffer, sizeof(buffer), &encoded_size, payload, sizeof(payload)) == SIMPLEHDLC_OK);
    assert_int_equal(encoded_size, 8);
    assert_int_equal(encoded_size, simplehdlc_get_encoded_size(payload, sizeof(payload)));

    uint8_t expected[] = {0x7E, 0x00, 0x01, 0x01, 0xA5, 0x05, 0xDF, 0x1B};
    assert_memory_equal(expected, buffer, encoded_size);
}

static void encode_test_escaping(void **state) {
    uint8_t buffer[11];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[2] = {0x7E, 0x7D};

    assert_true(simplehdlc_encode_to_buffer(buffer, sizeof(buffer), &encoded_size, payload, sizeof(payload)) == SIMPLEHDLC_OK);
    assert_int_equal(encoded_size, 11);
    assert_int_equal(encoded_size, simplehdlc_get_encoded_size(payload, sizeof(payload)));

    uint8_t expected[] = {0x7E, 0x00, 0x02,
                          0x7D, 0x7E ^ (1<<5),
                          0x7D, 0x7D ^ (1<<5),
                          0xDE, 0xD1, 0x4B, 0x06};
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

    uint8_t payload[2] = {0x7E, 0x7D};

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.tx_byte_callback = tx_callback;
    simplehdlc_context_t context;
    simplehdlc_init(&context, NULL, 0, &callbacks, NULL);

    assert_true(simplehdlc_encode_to_callback(&context, payload, sizeof(payload), false) == SIMPLEHDLC_OK);

    assert_int_equal(callback_buffer_count, 11);

    uint8_t expected[] = {0x7E, 0x00, 0x02,
                          0x7D, 0x7E ^ (1<<5),
                          0x7D, 0x7D ^ (1<<5),
                          0xDE, 0xD1, 0x4B, 0x06};
    assert_memory_equal(expected, callback_buffer, callback_buffer_count);

    assert_false(tx_flushed);
}

static void encode_test_callback_withflush(void **state) {
    callback_buffer_count = 0;
    tx_flushed = false;

    uint8_t payload[2] = {0x7E, 0x7D};

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.tx_byte_callback = tx_callback;
    callbacks.tx_flush_buffer_callback = tx_flush_callback;
    simplehdlc_context_t context;
    simplehdlc_init(&context, NULL, 0, &callbacks, NULL);

    assert_true(simplehdlc_encode_to_callback(&context, payload, sizeof(payload), true) == SIMPLEHDLC_OK);

    assert_int_equal(callback_buffer_count, 11);

    uint8_t expected[] = {0x7E, 0x00, 0x02,
                          0x7D, 0x7E ^ (1<<5),
                          0x7D, 0x7D ^ (1<<5),
                          0xDE, 0xD1, 0x4B, 0x06};
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
    uint8_t encoded[] = {0x7E, 0x00, 0x01, 0x01, 0xA5, 0x05, 0xDF, 0x1B};

    uint8_t rx_buffer[1];
    simplehdlc_context_t context;

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.rx_packet_callback = decode_success_callback;

    simplehdlc_init(&context, rx_buffer, sizeof(rx_buffer), &callbacks, (void *) payload);
    simplehdlc_parse(&context, encoded, sizeof(encoded));

    assert_true(decode_success);
    assert_int_equal(decoded_length, 1);
}

static void parse_test_buffer_too_small(void **state) {
    decode_success = false;
    decoded_length = 0;

    uint8_t payload[] = {0x7E, 0x7D};
    uint8_t encoded[] = {0x7E, 0x00, 0x02,
                         0x7D, 0x7E ^ (1<<5),
                         0x7D, 0x7D ^ (1<<5),
                         0xDE, 0xD1, 0x4B, 0x06};

    uint8_t rx_buffer[1];
    simplehdlc_context_t context;

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.rx_packet_callback = decode_success_callback;

    simplehdlc_init(&context, rx_buffer, sizeof(rx_buffer), &callbacks,  (void *) payload);
    simplehdlc_parse(&context, encoded, sizeof(encoded));

    assert_false(decode_success);
    assert_int_equal(decoded_length, 0);

    uint8_t rx_buffer2[2];
    simplehdlc_context_t context2;

    simplehdlc_init(&context2, rx_buffer2, sizeof(rx_buffer2), &callbacks,  (void *) payload);
    simplehdlc_parse(&context2, encoded, sizeof(encoded));

    assert_true(decode_success);
    assert_int_equal(decoded_length, 2);
}

static void encode_parse_sanity_check(void **state) {
    decode_success = false;
    decoded_length = 0;

    uint8_t buffer[512];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[256];

    for (int i=0; i<256; i++) payload[i] = i;

    assert_true(simplehdlc_encode_to_buffer(buffer, sizeof(buffer), &encoded_size, payload, sizeof(payload)) == SIMPLEHDLC_OK);
    assert_int_equal(encoded_size, simplehdlc_get_encoded_size(payload, sizeof(payload)));

    uint8_t rx_buffer[256];
    simplehdlc_context_t context;

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.rx_packet_callback = decode_success_callback;

    simplehdlc_init(&context, rx_buffer, sizeof(rx_buffer), &callbacks, (void *) payload);
    simplehdlc_parse(&context, buffer, encoded_size);

    assert_true(decode_success);
    assert_int_equal(decoded_length, sizeof(payload));
}

static void encode_parse_test_zero_length_packet(void **state) {
    decode_success = false;
    decoded_length = 0xFFFF;

    uint8_t buffer[7];
    size_t encoded_size = 0xFFFF;
    uint8_t payload[] = {1};

    assert_true(simplehdlc_encode_to_buffer(buffer, 7, &encoded_size, payload, 0) == SIMPLEHDLC_OK);
    assert_int_equal(encoded_size, 7);

    uint8_t rx_buffer[256];
    simplehdlc_context_t context;

    simplehdlc_callbacks_t callbacks = {0};
    callbacks.rx_packet_callback = decode_success_callback;

    simplehdlc_init(&context, rx_buffer, sizeof(rx_buffer), &callbacks, (void *) payload);
    simplehdlc_parse(&context, buffer, encoded_size);

    assert_true(decode_success);
    assert_int_equal(decoded_length, 0);
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
            cmocka_unit_test(parse_test_buffer_too_small),

            cmocka_unit_test(encode_parse_sanity_check),
            cmocka_unit_test(encode_parse_test_zero_length_packet)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}