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

#include <algorithm>
#include <boost/filesystem/file_status.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/errors.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/throw_exception.hpp>
#include <boost/filesystem.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <ios>
#include <iostream>
#include <cstdio>
#include <ostream>
#include <stdexcept>
#include <string>
#include <zlib.h>

#include "stb/stb_image_write.h"
#include "util.hpp"
#include "chowimg.hpp"

#define PROJECT_NAME "cyber-shadow-extractor"
#define INVALID_OFFSET 0xffffffff

const std::string extension_ogg = ".ogg";
const std::string extension_wav = ".wav";

namespace po = boost::program_options;
namespace fs = boost::filesystem;

struct asset_offsets {
    uint32_t images;
    uint32_t sounds;
    uint32_t fonts;
    uint32_t shaders;
    uint32_t files;
    uint32_t platform;
    uint32_t sizes;
};

enum class image_format {
    INVALID,
    ZLIB,
    RAW,
    CHOWIMG
};

// This refers to the format of entries in the archive,
// not the format in the underlying audio container
enum class sound_format {
    INVALID,
    LONG,       // Contains a bunch of extra metadata
    SHORT       // Only contains underlying container type and size
};

image_format get_image_format(const std::string& name) {
    if (name == "zlib") {
        return image_format::ZLIB;
    } else if (name == "chowimg") {
        return image_format::CHOWIMG;
    } else if (name == "raw") {
        return image_format::RAW;
    } else {
        return image_format::INVALID;
    }
}

sound_format get_sound_format(const std::string& name) {
    if (name == "long") {
        return sound_format::LONG;
    } else if (name == "short") {
        return sound_format::SHORT;
    } else {
        return sound_format::INVALID;
    }
}

struct sound_offsets {
    uint32_t size;
    uint32_t data;
};

const sound_offsets& get_sound_offsets(sound_format format) {
    static const sound_offsets offsets_long =  {16, 20};
    static const sound_offsets offsets_short = {4, 8};

    if (format == sound_format::LONG)   return offsets_long;
    if (format == sound_format::SHORT)  return offsets_short;

    throw std::invalid_argument("get_sound_offsets: received invalid sound format");
}

int parse_args(po::variables_map& opts, int argc, char** argv) {
    po::options_description optdesc_named("Named options");
    optdesc_named.add_options()
        (
            "probe-offsets",
            "only find offsets and exit"
        )
        (
            "image-format",
            po::value<std::string>()->default_value("zlib"),
            "how to handle image data in the archive:\n"
            "- zlib (decompress with zlib)\n"
            "- chowimg (decompress using custom algorhitm)\n"
            "- raw (extract raw data without decompression)"
        )
        (
            "sound-format",
            po::value<std::string>()->default_value("long"),
            "type of sound entries in the archive:\n"
            "- long\n"
            "- short\n"
            "if you're unsure what format your archive has, try both and see which works"
        )
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

void extract_images(asset_offsets& offsets, Buffer& buffer, const std::string& output_dir_path, image_format format) {
    if (offsets.images == INVALID_OFFSET) {
        std::cerr << "failed to find image offsets";
        return;
    }
    
    // I don't know the size of the compressed data, so let's just get a 16mb buffer that should be large enough for everything
    constexpr const int buffer_size = 0x1000000;
    Buffer temp_buffer(buffer_size);

    int entry_number = 0;
    int extracted_number = 0;
    for (uint32_t i=offsets.images; i<offsets.sounds; i+=4) {
        buffer.seek(i, Buffer::SET);
        uint32_t entry_offset = buffer.read_u32();
        buffer.seek(entry_offset, Buffer::SET);
        
        uint16_t width        = buffer.read_u16();
        uint16_t height       = buffer.read_u16();

        buffer.seek(entry_offset + 12, Buffer::SET);
        uint8_t  extra_float_count = buffer.read_u8();
        uint32_t size_offset  = 13 + extra_float_count * 8;
        uint32_t image_data_offset = size_offset + 4;

        buffer.seek(entry_offset + size_offset, Buffer::SET);
        uint32_t size         = buffer.read_u32();
        uint8_t* image_data   = buffer.at(entry_offset + image_data_offset);

        unsigned long out_size = buffer_size;
        if (format == image_format::ZLIB || format == image_format::CHOWIMG) {
            bool decompression_success = false;
            if (format == image_format::ZLIB) {
                int result = uncompress(temp_buffer.at(0), &out_size, image_data, size);
                if (result != Z_OK) {
                    std::cerr << "zlib decompression failure" << std::endl;
                } else {
                    decompression_success = true;
                }
            } else if (format == image_format::CHOWIMG) {
                temp_buffer.seek(0, Buffer::SET);
                buffer.seek(entry_offset + image_data_offset, Buffer::SET);
                int result = chowimg_read(temp_buffer, buffer, 
                    entry_offset + image_data_offset + size);

                if (result) {
                    std::cerr << "chowimg decompression failure for image" << entry_number 
                      << " (" << width << "x" << height << "), image_offset=0x"
                      << std::hex << entry_offset + image_data_offset << ", entry_offset=0x" 
                      << entry_offset << std::dec << std::endl;
                } else {
                    decompression_success = true;
                }
            }
            if (decompression_success) {
                auto filename = output_dir_path + "/image" + std::to_string(entry_number) + ".png";
                stbi_write_png(filename.c_str(), width, height, 4, temp_buffer.at(0), width * 4);
                ++extracted_number;
            }
        } else if (format == image_format::RAW) {
            auto filename = output_dir_path + "/image" + std::to_string(entry_number) + "-" 
              + std::to_string(width) + "x" + std::to_string(height) + ".bin";
            FILE* out = fopen(filename.c_str(), "wb");
            fwrite(image_data, size, 1, out);
            fclose(out);
            ++extracted_number;
        }
        ++entry_number;
    };
    std::cout << "Wrote " << extracted_number << " images" << std::endl;
}

void extract_audio(
  asset_offsets& offsets, Buffer& buffer, 
  const std::string& output_dir_path, sound_format format
) {
    if (offsets.sounds == INVALID_OFFSET) {
        std::cerr << "failed to find sound offsets";
        return;
    }

    const sound_offsets& sound_offsets = get_sound_offsets(format);

    int entry_number = 0;
    for (uint32_t i=offsets.sounds; i<offsets.fonts; i+=4) {
        buffer.seek(i, Buffer::SET);
        uint32_t entry_offset = buffer.read_u32();

        buffer.seek(entry_offset, Buffer::SET);
        uint32_t audio_type = buffer.read_u32();

        buffer.seek(entry_number + sound_offsets.size, Buffer::SET);
        uint32_t size = buffer.read_u32();

        if (audio_type == 0) {
            std::cerr << "Invalid audio type at 0x" << std::hex << entry_offset << std::endl;
        } else {
            auto& extension = audio_type == 1 ? extension_wav : extension_ogg;
            auto filename = output_dir_path + "/audio" + std::to_string(entry_number) + extension;
    
            FILE* out = std::fopen(filename.c_str(), "wb");
            fwrite(buffer.at(entry_offset + sound_offsets.data), size, 1, out);
            fclose(out);
        }
        ++entry_number;
    }
    std::cout << "Wrote " << entry_number << " audio files" << std::endl;
}

void extract_shaders(asset_offsets& offsets, Buffer& buffer, const std::string& output_dir_path) {
    if (offsets.shaders == INVALID_OFFSET) {
        std::cerr << "failed to find shader offsets";
        return;
    }
    
    int entry_number = 0;
    for (uint32_t i=offsets.shaders; i<offsets.files; i+=4) {
        buffer.seek(i, Buffer::SET);
        uint32_t entry_offset_vert = buffer.read_u32();

        buffer.seek(entry_offset_vert, Buffer::SET);
        uint32_t size_vert = buffer.read_u32();
    
        auto filename_vert = output_dir_path + "/shader" + std::to_string(entry_number) + ".vert";

        FILE* vert = std::fopen(filename_vert.c_str(), "w");
        fwrite(buffer.at(entry_offset_vert + 4), size_vert, 1, vert);
        fclose(vert);

        uint32_t entry_offset_frag = entry_offset_vert + 4 + size_vert;
        buffer.seek(entry_offset_frag, Buffer::SET);
        uint32_t size_frag = buffer.read_u32();

        auto filename_frag = output_dir_path + "/shader" + std::to_string(entry_number) + ".frag";

        FILE* frag = std::fopen(filename_frag.c_str(), "w");
        fwrite(buffer.at(entry_offset_frag + 4), size_frag, 1, frag);
        fclose(frag);

        ++entry_number;
    }
    std::cout << "Wrote " << entry_number << " shader pairs" << std::endl;
}

uint32_t find_shader_code_offset(uint8_t* mmap, uint32_t file_size) {
    constexpr const char void_main[] = {'v', 'o', 'i', 'd', ' ', 'm', 'a', 'i', 'n'};
    for (uint32_t i=0; i<file_size; ++i) {
        if (std::memcmp(mmap + i, void_main, sizeof(void_main)) == 0) {
            return i;
        }
    }    
    return INVALID_OFFSET;
}


// Shader size is stored in a little-endian dword. We're going to assume
// that shaders are not large enough for the last byte of that dword to be set.
// We can not simply seek until we find a non-printable character, because the last
// byte of the shader size could happen to be printable by chance.
uint32_t shader_seek_backwards(uint8_t* mmap, uint32_t curr_offset) {
    while(mmap[curr_offset] && curr_offset>0) --curr_offset;
    if (curr_offset == 0) {
        // Something is horribly wrong.
        return INVALID_OFFSET;
    }
    // At this point we are (hopefully) in the size dword, but we don't know which byte exactly!
    // Fortunately, as the opengl wiki states:
    //   The #version directive must appear before anything else in a shader, save for whitespace and comments. 
    //   If a #version directive does not appear at the top, then it assumes 1.10, which is almost certainly not what you want.
    // We should be able to find the beginning of the shader easily from where we are now. Cool!
    uint32_t somewhere_in_size_dword = curr_offset;
    constexpr const char version[] = {'#', 'v', 'e', 'r', 's', 'i', 'o', 'n'};
    while(std::memcmp(mmap + curr_offset, version, sizeof(version)) != 0) ++curr_offset;

    // For now, I'll assume no whitespace or comments before the #version directive, for my own sanity.
    // Proper handling of that would require actually parsing the shader code to see at which point it becomes valid
    // (remember - the size dword can still contain printable characters that we can't tell apart from shader code without parsing it!)
    // Conveniently, this is also an exit condition for when the previous shader was the last one.
    if (curr_offset - somewhere_in_size_dword > 4) {
        return INVALID_OFFSET;
    }

    uint32_t size_dword = curr_offset - 4;
    // An extra sanity check could be added here, comparing the shader size to the shader dword, but that's annoying to do because the shaders
    // are not NULL-terminated.
    return size_dword;
}

bool is_valid_glsl(uint8_t c) {
    return (c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t';
}

uint32_t shader_seek_forwards(uint8_t* mmap, uint32_t curr_offset, uint32_t file_size) {
    // This is a bit easier than seeking backwards.
    // We know we're at a size dword, so we read it into n and then see if n characters after it are printable.
    // If they are, this is a valid shader entry.
    if (curr_offset + 4 >= file_size) {
        return INVALID_OFFSET;
    }

    uint32_t size = read_little_endian_u32(mmap + curr_offset);
    if (size == 0) {
        return INVALID_OFFSET;
    }

    uint32_t max_offset = std::min(file_size, curr_offset + size + 4);
    for (curr_offset=curr_offset+4; curr_offset<max_offset; ++curr_offset) {
        if (!is_valid_glsl(mmap[curr_offset])) {
            return INVALID_OFFSET;
        }
    }
    return curr_offset;
}

uint32_t find_first_offset(uint8_t* mmap, uint32_t file_size) {
    for (uint32_t i=0; i<file_size; ++i) {
        if (mmap[i]) return i;
    }
    // Turns out the file is all 0. How did we get here?
    return INVALID_OFFSET;
}

uint32_t find_type_sizes(uint8_t* mmap, uint32_t shader_size, uint32_t curr_offset) {
    for (;curr_offset>=12; curr_offset-=4) {
        uint32_t v = read_little_endian_u32(mmap + curr_offset);
        if (v == shader_size) {
            return curr_offset - 12; // shader_size is the 4th entry in the table and we want to return the beginning
        }
    }
    return INVALID_OFFSET;
}

uint32_t find_u32(uint8_t* mmap, uint32_t val, uint32_t file_size, uint32_t default_val) {
    for (uint32_t i=0; i<file_size; i+=4) {
        if (read_little_endian_u32(mmap + i) == val) return i;
    }
    return default_val;
}

uint32_t find_asset_offsets(asset_offsets& offsets, Buffer& buffer) {
    // For these operations in particular, I think working with
    // the raw uint8_t* makes things more convenient.
    uint8_t* mmap = buffer.at(0);
    uint32_t file_size = buffer.get_size();

    // First, we need to find some data that we can easily identify; since shaders
    // are stored in plaintext, it'll be easiest to look for them. In particular,
    // we'll look for a `void main` string, since that should be present somewhere.
    uint32_t shader_offset = find_shader_code_offset(mmap, file_size);

    if (shader_offset == INVALID_OFFSET) {
        return 1;
    }

    // We now need to measure the total size of the shader data - we're going to use that
    // in order to find the data_sizes segment of the Assets file.
    
    // First, go backwards.
    uint32_t curr_offset = shader_offset;
    while(curr_offset != INVALID_OFFSET) {
        shader_offset = curr_offset;
        curr_offset = shader_seek_backwards(mmap, curr_offset - 1);
    }
    uint32_t shaders_start = shader_offset;

    // Now, go forwards!
    curr_offset = shaders_start;
    while(curr_offset != INVALID_OFFSET) {
        shader_offset = curr_offset;
        curr_offset = shader_seek_forwards(mmap, curr_offset, file_size);
    }
    uint32_t shaders_end = shader_offset;

    // std::cout << std::hex << "shaders: from 0x" << shaders_start << " to 0x" << shaders_end << std::endl;
    uint32_t size_shaders = shaders_end - shaders_start;

    // Now that we know the shader size, we can attempt to locate data_sizes struct.
    // In order to do that, we're going to find the first offset in the file (remember that it starts with a bunch of 0s for some reason),
    // follow it and then seek backwards.
    uint32_t first_offset_location = find_first_offset(mmap, file_size);
    if (first_offset_location == INVALID_OFFSET) {
        return INVALID_OFFSET;
    }
    
    uint32_t first_offset = read_little_endian_u32(mmap + first_offset_location);

    uint32_t type_sizes_offset = find_type_sizes(mmap, size_shaders, first_offset - 4);
    
    uint32_t size_images    = read_little_endian_u32(mmap + type_sizes_offset);
    uint32_t size_sounds    = read_little_endian_u32(mmap + type_sizes_offset + 4);
    uint32_t size_fonts     = read_little_endian_u32(mmap + type_sizes_offset + 8);
    // size_shaders already set
    uint32_t size_files     = read_little_endian_u32(mmap + type_sizes_offset + 16);
    uint32_t size_platform  = read_little_endian_u32(mmap + type_sizes_offset + 20);

    uint32_t data_platform = file_size - size_platform;
    uint32_t data_files    = data_platform - size_files;
    uint32_t data_shaders  = data_files - size_shaders;
    uint32_t data_fonts    = data_shaders - size_fonts;
    uint32_t data_sounds   = data_fonts - size_sounds;
    uint32_t data_images   = data_sounds - size_images;

    uint32_t max_search_offset = type_sizes_offset;

    offsets.images = find_u32(mmap, data_images, max_search_offset, type_sizes_offset);
    offsets.sounds = find_u32(mmap, data_sounds, max_search_offset, type_sizes_offset);
    offsets.fonts = find_u32(mmap, data_fonts, max_search_offset, type_sizes_offset);
    offsets.shaders = find_u32(mmap, data_shaders, max_search_offset, type_sizes_offset);
    offsets.files = find_u32(mmap, data_files, max_search_offset, type_sizes_offset);
    offsets.platform = find_u32(mmap, data_platform, max_search_offset, type_sizes_offset);
    offsets.sizes = type_sizes_offset;

    return 0;
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

    Buffer input_buffer(file);
    
    std::fclose(file);

    asset_offsets offsets;
    if (find_asset_offsets(offsets, input_buffer)) {
        std::cerr << "failed to find asset_offsets" << std::endl;
        return 1;
    }

    std::cout 
      << "Determined following offsets:" << std::endl << std::hex
      << "  - images:     0x" << offsets.images << std::endl
      << "  - sounds:     0x" << offsets.sounds << std::endl
      << "  - fonts:      0x" << offsets.fonts << std::endl
      << "  - files:      0x" << offsets.files << std::endl
      << "  - platform:   0x" << offsets.platform << std::endl
      << "  - type_sizes: 0x" << offsets.sizes << std::endl << std::dec;

    if (opts.count("probe-offsets")) {
        return 0;
    }

    if (!opts.count("no-images")) {
        image_format format = get_image_format(opts["image-format"].as<std::string>());
        if (format != image_format::INVALID) {
            extract_images(offsets, input_buffer, output_dir_path, format);
        } else {
            std::cerr << "passed invalid image-format, not extracing images" << std::endl;
        }
    }
    
    if (!opts.count("no-audio")) {
        sound_format format = get_sound_format(opts["sound-format"].as<std::string>());
        extract_audio(offsets, input_buffer, output_dir_path, format);
    }

    if (!opts.count("no-shaders")) {
        extract_shaders(offsets, input_buffer, output_dir_path);    
    }

    return 0;
}
