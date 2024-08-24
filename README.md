# Cyber Shadow extractor

This is a tool to extract data from the `Assets.dat` file of [Cyber Shadow](https://store.steampowered.com/app/861250/Cyber_Shadow/). As of the 
2nd version, it now probes the `Assets.dat` file instead of relying on hardcoded offsets, which should increase compatibility with other games
using this format (specifically I think it's games that use [Chowdren](https://mp2.dk/chowdren/) - so far I checked Petal Crash and it extracts fine).

## TODO

- Check compatibility with more games
- Allow extracting files from the `font`, `files` and `platform` sections (what does `platform` even mean?) Cyber Shadow does not use them, but other games might (Petal Crash has non-empty `fonts`).
- Support newer internal formats - notably, for Baba Is You Assets.dat the program fails to extract both images and sounds; only shaders are extracted. Images might be using a different (proprietary) compression format, as described [in a similar project](https://github.com/snickerbockers/fp-assets). As for the audio, my initial investigation suggests that it's still uncompressed, but the entry format is slightly different so it fails to find the data.
- An option to extract the raw, compressed image data instead of constructing a png might be useful for debugging

## What you need to build

- The Boost library
- The zlib library
- [meson](https://mesonbuild.com/) and [ninja](https://github.com/ninja-build/ninja/)*

\* ninja is only needed if you want to use it to build. You can apparently also generate vs and xcode projects, but I never used that. Instructions below assume ninja.

## How to build

1. Clone the repo
2. cd into the repo and run `git submodule init && git submodule update` 
3. run `meson setup builddir`
4. `cd builddir` and then run `ninja` and it should build, hopefully. If not, you're probably on your own

## How to use

Run `./cyber-shadow-extractor --help` for info. 
```
Usage: cyber-shadow-extractor [options] input.dat output-dir
Named options:
  --probe-offsets       only find offsets and exit
  --no-images           skip extracting images
  --no-audio            skip extracting audio
  --no-shaders          skip extracting shaders
  --help                print help message
```

Note that there are no filenames included in the Assets file, so files are just extracted as `image1.png`, `audio1.ogg` etc. Audio files also appear to be in a completely random order.

## File format notes

The beginning of the file used in this specific case is as follows:

```
0x0000 - 0x4aa0 - absolutely nothing, for some reason
0x4aa0 - 0xdfe0 - image_offsets
0xdfe0 - 0xe7f8 - sound_offsets
empty font_offsets
0xe7f8 - 0xe8f0 - shader_offsets
empty file_offsets
empty platform_offsets
0xe8f0 - 0xe908 - type_sizes
```

### `image_offsets`
An array of little-endian uint32_t (everything in the file is little endian) where each entry points to the image data that's further in the file. Said image data takes the following structure:
```cpp
struct AssetEntryImage {
    uint16_t x; // Width of image file
    uint16_t y; // Height of image file
    float    x2; // idk
    float    y2; // idk
    uint8_t  ex_dimension_cnt; // Count of extra dimension data
    struct {
        float x; // Idk
        float y; // Idk
    } ex_dimensions[ex_dimension_cnt];
    uint32_t size; // Size of the image data that follows
    uint8_t  image_data[size]; // zlib-compressed image data
}
```
Not valid C++ obviously but I hope you get the idea. I don't know what any of the floats do, they're probably used to change how it gets displayed ingame or something. I didn't reverse engineer that far (I mean, who'd want to dig through a bunch of inlined `stb_image` functions anyway).

### `sound_offsets`
Like `image_offsets` but they point to sound data instead (crazy I know).
```cpp
struct AssetEntrySound {
    uint32_t audio_type;   // = 0 causes early return condition, = 1 RIFF WAVE, = 2 ogg vorbis 
    uint32_t unknown1;     // Seems streaming-related (used when size > 512kb); the bigger the entry, the bigger it is, so perhaps sample count?
    uint32_t sample_rate;  // The sample rate
    uint8_t  unknown3;     // Also seems streaming-related (as above); always 1 or 2?
    char     padding[3];   // Padding
    uint32_t size;         // if <= 0x80000 (512kb) a different branch is taken in the code; my educated guess is that if it's small it fully loads it into memory (think sfx)
    uint8_t  sound_data[size]; // Raw .ogg or .wav file depending on audio_type
}
```
Because these are literally .wav and .ogg files placed inside of the Assets file, they are trivial to extract once you find them.

### `shader_offsets`
These offsets point to pairs of shaders (vertex and fragment).

```cpp
struct AssetEntryShader {
    uint32_t vertex_shader_size;
    char vertex_shader[vertex_shader_size];
    uint32_t fragment_shader_size;
    char fragment_shader[fragment_shader_size];
}
```
They are in plaintext. Not much to say here.

### `type_sizes`
This part holds info about the size of various sections. For example, if you want to copy all image data to memory, you check out the offset of the first entry from `image_offsets` and copy `type_sizes[0]` bytes from there - `type_sizes` has an entry for every offset table before it, in the same order as they appear in. In other words, it looks like this:
```cpp
struct AssetTypeSizes {
    uint32_t size_images;
    uint32_t size_sounds;
    uint32_t size_fonts; // = 0
    uint32_t size_shaders; 
    uint32_t size_files; // = 0
    uint32_t size_platform; // = 0
}
```

After `type_sizes` begins the data pointed to by the offsets from earlier.

### How the probing works

TODO: describe this in the README. The code has a bunch of comments, so for now you can check that.
