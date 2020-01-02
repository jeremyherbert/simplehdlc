//
// Created by Jeremy Herbert on 2/1/20.
//

#ifndef SIMPLEHDLC_SIMPLEHDLC_CRC32_H
#define SIMPLEHDLC_SIMPLEHDLC_CRC32_H

#include <stdint.h>
#include <stddef.h>

uint32_t simplehdlc_compute_crc32(const void *data, size_t n_bytes);

#endif //SIMPLEHDLC_SIMPLEHDLC_CRC32_H
