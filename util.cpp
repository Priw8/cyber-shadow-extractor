#include "util.hpp"
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>

uint16_t read_little_endian_u16(const uint8_t* const data) {
    return *data | (*(data + 1) << 8);
}

uint32_t read_little_endian_u32(const uint8_t* const data) {
    return *data | (*(data + 1) << 8) | (*(data + 2) << 16) | (*(data + 3) << 24);
}

float read_little_endian_f32(const uint8_t* const data) {
    uint32_t tmp = (*data | (*(data + 1) << 8) | (*(data + 2) << 16) | (*(data + 3) << 24));
    return *(float*)(&tmp);
}

void write_little_endian_u16(uint8_t* data, uint16_t val) {
    *data = val & 0xff;
    *(data + 1) = val >> 8;
}

void write_little_endian_u32(uint8_t *data, uint32_t val) {
    *data = val & 0xff;
    *(data + 1) = (val >> 8) & 0xff;
    *(data + 2) = (val >> 16) & 0xff;
    *(data + 3) = (val >> 24) & 0xff;
}

void write_little_endian_f32(uint8_t *data, float val) {
    uint32_t tmp = *(uint32_t*)(&val);
    *data = tmp & 0xff;
    *(data + 1) = (tmp >> 8) & 0xff;
    *(data + 2) = (tmp >> 16) & 0xff;
    *(data + 3) = (tmp >> 24) & 0xff;
}

Buffer::Buffer(uint32_t size) {
    this->size = size;
    this->buffer = static_cast<uint8_t*>(malloc(size));
}

Buffer::Buffer(FILE* file) {
    long prev_seek = ftell(file);
    fseek(file, 0, SEEK_END);
    this->size = ftell(file);
    this->buffer = static_cast<uint8_t*>(malloc(this->size));
    fseek(file, 0, SEEK_SET);
    fread(this->buffer, this->size, 1, file);
    fseek(file, prev_seek, SEEK_SET);
}

Buffer::~Buffer() {
    free(this->buffer);
}

void Buffer::seek(uint32_t offset, Buffer::Whence whence) {
    switch (whence) {
        case Buffer::Whence::SET:
            this->offset = offset;
            break;
        case Buffer::Whence::END:
            this->offset = this->size - 1 - offset;
            break;
        case Buffer::Whence::CURR:
            this->offset += offset;
            break;
        default:
            throw new std::invalid_argument("Buffer::seek: invalid whence");
    }
}

void Buffer::reserve(uint32_t size) {
    reserve(size, 0);
}

void Buffer::reserve(uint32_t size, uint32_t extra_alloc) {
    if (this->size < size) {
        this->buffer = static_cast<uint8_t*>(realloc(buffer, size + extra_alloc));
        this->size = size;
    }
}

// Made for cases where destination and source overlap and memcpy can't be used
void Buffer::copy_from_self(uint32_t from, uint32_t count) {
    bounds_check(count);
    uint32_t max = from + count;
    for (uint32_t i=from; i < max; ++i) {
        this->buffer[this->offset] = this->buffer[i];
        ++this->offset;
    }
}
