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
    uint16_t payload_len = 1;

    for (int i=0; i<7; i++) {
        assert_true(hdlc_encode_to_buffer(buffer, i, &encoded_size, payload, payload_len) == HDLC_ERROR_BUFFER_TOO_SMALL);
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

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(crc32_sanity_check),
            cmocka_unit_test(encode_test_too_small),
            cmocka_unit_test(encode_test_zero_length_payload),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}