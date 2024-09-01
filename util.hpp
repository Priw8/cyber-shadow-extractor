#pragma once

#include <cstdint>

uint32_t read_little_endian_u16(const uint8_t* const data);

uint32_t read_little_endian_u32(const uint8_t* const data);

float read_little_endian_f32(const uint8_t* const data);