/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#include <boost/filesystem/file_status.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/errors.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/throw_exception.hpp>
#include <boost/filesystem.hpp>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <cstdio>
#include <ostream>
#include <string>
#include <zlib.h>

#include "stb/stb_image_write.h"

#define PROJECT_NAME "cyber-shadow-extractor"

// Offsets acquired by reverse engineering CyberShadow binary
constexpr const uint32_t OFFSET_IMAGES         = 0x4aa0;
constexpr const uint32_t OFFSET_IMAGES_END     = 0xdfe0;
constexpr const uint32_t OFFSET_SOUNDS         = 0xdfe0;
constexpr const uint32_t OFFSET_SOUNDS_END     = 0xe7f8;
constexpr const uint32_t OFFSET_SHADERS        = 0xe7f8;
constexpr const uint32_t OFFSET_SHADERS_END    = 0xe8f0;

const std::string extension_ogg = ".ogg";
const std::string extension_wav = ".wav";

namespace po = boost::program_options;
namespace fs = boost::filesystem;

int parse_args(po::variables_map& opts, int argc, char** argv) {
    po::options_description optdesc_named("Named options");
    optdesc_named.add_options()
        (
            "no-images",
            "skip extracting images"
        )
        (
            "no-audio",
            "skip extracting audio"
        )
        (
            "no-shaders",
            "skip extracting shaders"
        )
        (
            "help",
            "print help message"
        );
    po::options_description optdesc_positional("Positional options");
    optdesc_positional.add_options()
        (
            "input",
            "input file"
        )
        (
            "output",
            "output directory"
        );

    po::options_description optdesc("Available options");
    optdesc.add(optdesc_named).add(optdesc_positional);

    po::positional_options_description p;
    p.add("input", 1).add("output", 1);

    try {
        po::store(
            po::command_line_parser(argc, argv)
              .options(optdesc).positional(p).run(), opts);
    
        po::notify(opts);
    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    if (!opts.count("input") || !opts.count("output") || opts.count("help")) {
        std::cout << "Usage: " PROJECT_NAME " [options] input.dat output-dir" << std::endl;
        optdesc_named.print(std::cout);
        return 1;
    }

    return 0;
}

uint32_t read_little_endian_u16(uint8_t* data) {
    return *data | (*(data + 1) << 8);
}

uint32_t read_little_endian_u32(uint8_t* data) {
    return *data | (*(data + 1) << 8) | (*(data + 2) << 16) | (*(data + 3) << 24);
}

float read_little_endian_f32(uint8_t* data) {
    uint32_t tmp = (*data | (*(data + 1) << 8) | (*(data + 2) << 16) | (*(data + 3) << 24));
    return *(float*)(&tmp);
}

void extract_images(uint8_t* mmap, const std::string& output_dir_path) {
    // I don't know the size of the compressed data, so let's just get a 16mb buffer that should be large enough for everything
    constexpr const int buffer_size = 0x1000000;
    uint8_t* temp_buffer = (uint8_t*)std::malloc(buffer_size); 
    int entry_number = 0;
    for (uint32_t i=OFFSET_IMAGES; i<OFFSET_IMAGES_END; i+=4) {
        uint32_t entry_offset = read_little_endian_u32(mmap + i);
        uint16_t width        = read_little_endian_u16(mmap + entry_offset);
        uint16_t height       = read_little_endian_u16(mmap + entry_offset + 2);
        uint8_t  extra_float_count = *(mmap + entry_offset + 12);
        unsigned size_offset  = 13 + extra_float_count * 8;
        unsigned image_data_offset = size_offset + 4;
        uint32_t size         = read_little_endian_u32(mmap + entry_offset + size_offset );

        unsigned long out_size = buffer_size;
        int result = uncompress(temp_buffer, &out_size, mmap + entry_offset + image_data_offset, size);
        if (result != Z_OK) {
            std::cerr << "decompression failure" << std::endl;
        } else {
            auto filename = output_dir_path + "/image" + std::to_string(entry_number) + ".png";
            stbi_write_png(filename.c_str(), width, height, 4, temp_buffer, width * 4);
            std::cout << "Writing to " << filename << std::endl;
            ++entry_number;
        }
    };
    std::free(temp_buffer);
}

void extract_audio(uint8_t* mmap, const std::string& output_dir_path) {
    int entry_number = 0;
    for (uint32_t i=OFFSET_SOUNDS; i<OFFSET_SOUNDS_END; i+=4) {
        uint32_t entry_offset = read_little_endian_u32(mmap + i);
        uint32_t audio_type   = read_little_endian_u32(mmap + entry_offset); //,
                //  unknown1     = read_little_endian_u32(mmap + entry_offset + 4), 
                //  sample_rate  = read_little_endian_u32(mmap + entry_offset + 8);
        // uint8_t  unknown3     = *(mmap + entry_offset + 12);
        uint32_t size         = read_little_endian_u32(mmap + entry_offset + 16);
        // std::cout << "Found audio of type=" << audio_type << std::endl;

        if (audio_type == 0) {
            std::cerr << "Invalid audio type at 0x" << std::hex << entry_offset << std::endl;
        } else {
            auto& extension = audio_type == 1 ? extension_wav : extension_ogg;
            auto filename = output_dir_path + "/audio" + std::to_string(entry_number) + extension;
            std::cout << "Writing to " << filename << std::endl;
    
            FILE* out = std::fopen(filename.c_str(), "wb");
            fwrite(mmap + entry_offset + 20, size, 1, out);
            fclose(out);
        }
        ++entry_number;
    }
}

void extract_shaders(uint8_t* mmap, const std::string& output_dir_path) {
    int entry_number = 0;
    for (uint32_t i=OFFSET_SHADERS; i<OFFSET_SHADERS_END; i+=4) {
        uint32_t entry_offset_vert = read_little_endian_u32(mmap + i);
        uint32_t size_vert         = read_little_endian_u32(mmap + entry_offset_vert);
    
        auto filename_vert = output_dir_path + "/shader" + std::to_string(entry_number) + ".vert";
        std::cout << "Writing to " << filename_vert << std::endl;

        FILE* vert = std::fopen(filename_vert.c_str(), "w");
        fwrite(mmap + entry_offset_vert + 4, size_vert, 1, vert);
        fclose(vert);

        uint32_t entry_offset_frag = entry_offset_vert + 4 + size_vert;
        uint32_t size_frag         = read_little_endian_u32(mmap + entry_offset_frag);

        auto filename_frag = output_dir_path + "/shader" + std::to_string(entry_number) + ".frag";
        std::cout << "Writing to " << filename_frag << std::endl;

        FILE* frag = std::fopen(filename_frag.c_str(), "w");
        fwrite(mmap + entry_offset_frag + 4, size_frag, 1, frag);
        fclose(frag);

        ++entry_number;
    }
}

int main(int argc, char **argv) {
    po::variables_map opts;
    if (parse_args(opts, argc, argv)) {
        return 1;
    }

    auto& input_file_path = opts["input"].as<std::string>();
    auto& output_dir_path = opts["output"].as<std::string>();
    
    if (!fs::is_regular_file(input_file_path)) {
        std::cerr << input_file_path << ": not a regular file" << std::endl;
        return 1;
    }
    if (!fs::is_directory(output_dir_path)) {
        std::cerr << output_dir_path << ": directory does not exist" << std::endl;
        return 1;
    }

    FILE* file = std::fopen(input_file_path.c_str(), "rb");

    std::fseek(file, 0, SEEK_END);
    long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    uint8_t* mmap = static_cast<uint8_t*>(malloc(file_size));
    std::fread(mmap, file_size, 1, file);
    std::fclose(file);

    if (!opts.count("no-images")) {
        extract_images(mmap, output_dir_path);
    }
    
    if (!opts.count("no-audio")) {
        extract_audio(mmap, output_dir_path);
    }

    if (!opts.count("no-shaders")) {
        extract_shaders(mmap, output_dir_path);    
    }

    std::free(mmap);

    return 0;
}
