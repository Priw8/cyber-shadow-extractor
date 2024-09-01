#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>

#include "chowimg.hpp"

namespace po = boost::program_options;

#define PROJECT_NAME "chowimg"

void print_help() {
    std::cout << "usage: " PROJECT_NAME " input.bin output.png" << std::endl;
}

int main(int argc, char** argv) {
    po::variables_map opts;

    po::options_description opt_desc;
    opt_desc.add_options()(
        "input",
        
        "input file containing raw compressed data"
    )(
        "output",
        "where to write the output"
    );

    po::positional_options_description positional_desc;
    positional_desc.add("input", 1).add("output", 1);

    try {
        po::store(
            po::command_line_parser(argc, argv).options(opt_desc).positional(positional_desc).run(),
            opts);
    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    
    if (opts.count("input") == 0 || opts.count("output") == 0) {
        print_help();
        return 1;
    }

    auto& input_name = opts["input"].as<std::string>();
    auto& output_name = opts["output"].as<std::string>();

    FILE* input = fopen(input_name.c_str(), "rb");
    if (!input) {
        std::cerr << "failed to open input file" << std::endl;
        return 1;
    }

    fseek(input, 0, SEEK_END);
    uint32_t buffer_size = ftell(input);
    fseek(input, 0, SEEK_SET);
    uint8_t* buffer = static_cast<uint8_t*>(malloc(buffer_size));
    fread(buffer, buffer_size, 1, input);
    fclose(input);

    // Initial buffer size - it will be expanded if needed
    uint32_t out_buffer_size = 0xffff;
    uint8_t* out_buffer = static_cast<uint8_t*>(malloc(out_buffer_size));

    uint32_t out_offset = 0;
    uint32_t offset = 0;

    int res = chowimg_read(out_buffer, out_buffer_size, out_offset, buffer, offset, buffer_size);
    if (res) {
        return 1;
    }

    FILE* output = fopen(output_name.c_str(), "wb");
    if (!output) {
        std::cerr << "failed to open output file" << std::endl;
        return 1;
    }

    fwrite(out_buffer, out_offset, 1, output);
    fclose(output);

    return 0;
}
