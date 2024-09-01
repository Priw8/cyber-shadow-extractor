#include "chowimg.hpp"
#include "util.hpp"
#include <boost/container/container_fwd.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

// TODO: prevent out-of-bounds reads

void ensure_buffer_size(uint8_t*& buffer, uint32_t& current_size, uint32_t required_size, uint32_t extra_alloc_size) {
    if (current_size < required_size) {
        uint32_t new_size = required_size + extra_alloc_size;
        buffer = static_cast<uint8_t*>(realloc(buffer, new_size));
        current_size = new_size;
    }
}

void ensure_buffer_writable_space(
  uint8_t*& buffer, uint32_t& current_size, uint32_t offset, uint32_t required_space, uint32_t extra_alloc_size) {
    ensure_buffer_size(buffer, current_size, offset + required_space, extra_alloc_size);  
}

uint32_t read_variable_length_size(const uint8_t* const buffer, uint32_t& offset, uint8_t nibble) {
    uint32_t len = nibble;
    if (nibble == 0xf) {
        uint8_t byte;
        do {
            byte = buffer[offset];
            offset += 1;
            len += byte;
        } while(byte == 0xff);
    }
    return len;
}

int read_hunk(uint8_t*& out_buffer, uint32_t out_buffer_size, uint32_t& out_offset, const uint8_t* const buffer, uint32_t& offset) {
    uint32_t hunk_compressed_size = read_little_endian_u32(buffer + offset);
    offset += 4;
    uint32_t hunk_decompressed_size = 0;
    uint32_t max_offset = offset + hunk_compressed_size;
    ensure_buffer_writable_space(out_buffer, out_buffer_size, offset, hunk_compressed_size, 0);
    while(offset < max_offset) {
        uint8_t control_byte = buffer[offset];
        offset += 1;

        uint8_t first_nibble = control_byte >> 4;
        uint8_t second_nibble = control_byte & 0xf;

        uint32_t bytes_to_copy_count = read_variable_length_size(buffer, offset, first_nibble);
        ensure_buffer_writable_space(out_buffer, out_buffer_size, out_offset, bytes_to_copy_count, 0);
        memccpy(out_buffer + out_offset, buffer + offset, 1, bytes_to_copy_count);
        out_offset += bytes_to_copy_count;
        offset += bytes_to_copy_count;
        hunk_decompressed_size += bytes_to_copy_count;

        if (offset >= max_offset) {
            break;
        }

        uint16_t rewind_distance = read_little_endian_u16(buffer + offset);
        offset += 2;

        uint32_t rewind_start = hunk_decompressed_size - rewind_distance;

        if (rewind_distance > hunk_decompressed_size) {
            std::cerr << "read_hunk: rewind distance underflows the hunk (dist=" 
                << rewind_distance << ", hunk_decompressed_size=" << hunk_decompressed_size
                << ")"  << std::endl;
            return 1;
        }

        uint32_t rewind_byte_count = read_variable_length_size(
            buffer, offset, second_nibble) + 4;
        
        ensure_buffer_writable_space(out_buffer, out_buffer_size, out_offset, rewind_byte_count, 0);
        // memcpy must not be used here, as the source and destination can overlap! we must copy byte-by-byte...
        for (uint32_t i=0; i<rewind_byte_count; ++i, ++out_offset) {
            out_buffer[out_offset] = out_buffer[rewind_start + i];
        }
        std::cout << "copy from " << rewind_start << " to " << out_offset << " x" << rewind_byte_count << std::endl; 
        hunk_decompressed_size += rewind_byte_count;
    }
    return 0;
}

int chowimg_read(uint8_t*& out_buffer, uint32_t& out_buffer_size, uint32_t& out_offset, const uint8_t *const buffer, uint32_t& offset, uint32_t max_offset) {
    while (offset < max_offset) {
        int res = read_hunk(out_buffer, out_buffer_size, out_offset, buffer, offset);
        if (res) {
            return 1;
        }
    }
    return 0;
}