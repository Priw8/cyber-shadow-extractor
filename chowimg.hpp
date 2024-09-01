#pragma once

#include <cstdint>

int chowimg_read(uint8_t*& out_buffer, uint32_t& out_buffer_size, uint32_t& out_offset, const uint8_t *const buffer, uint32_t& offset, uint32_t max_offset);
