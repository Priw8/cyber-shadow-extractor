#include <cstdint>

uint32_t read_little_endian_u16(const uint8_t* const data) {
    return *data | (*(data + 1) << 8);
}

uint32_t read_little_endian_u32(const uint8_t* const data) {
    return *data | (*(data + 1) << 8) | (*(data + 2) << 16) | (*(data + 3) << 24);
}

float read_little_endian_f32(const uint8_t* const data) {
    uint32_t tmp = (*data | (*(data + 1) << 8) | (*(data + 2) << 16) | (*(data + 3) << 24));
    return *(float*)(&tmp);
}
