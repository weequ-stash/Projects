#include <iostream>
#include <utility>
#include <string>
#include <cstring>
#include <fstream>
#include <bit>
#include <array>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cmath>
#include <iomanip>

bool DEBUG = false;

constexpr uint32_t CHUNK_IHDR = 0x49484452;
constexpr uint32_t CHUNK_IDAT = 0x49444154;
constexpr uint32_t CHUNK_IEND = 0x49454E44;

void debug_impl(std::string_view message) {
    std::cerr << message << '\n';
}

#define debug(msg)           \
    do {                     \
        if(DEBUG) {          \
            debug_impl(msg); \
        }                    \
    } while(0)

template <typename T>
T toBigEndian(T& value) {
    if constexpr(std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    return value;
}

template <typename T>
T reverseEndianFix(T& value) { // used only in debug
    if constexpr(std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    return value;
}

std::string chunkTypeToStr(uint32_t chunk_type) {
    return std::string(std::bit_cast<std::array<char, 4>>(reverseEndianFix(chunk_type)).data(), 4);
}

/* ---------- Part 1: Decoding the png ---------- */

std::streampos getFileSize(std::ifstream& file) {
    auto pre = file.tellg();
    file.seekg(0, std::ios::end);
    auto return_value = file.tellg();
    file.seekg(pre);
    return return_value;
}

class BitReader {
public:
    BitReader(const uint8_t* data, std::size_t size)
        : data(data), size(size) {}

    void fillBuffer() {
        while(bitsInBuffer <= 56 && bytePos < size) {
            bitBuffer |= (static_cast<uint64_t>(data[bytePos++]) << bitsInBuffer);
            bitsInBuffer += 8;
        }
    }

    uint32_t peekBits(unsigned int count) {
        fillBuffer();
        return bitBuffer & ((1ULL << count) - 1);
    }

    void dropBits(unsigned int count) {
        if(bitsInBuffer < count) {
            throw std::runtime_error("Unexpected end of stream\n");
        }
        bitBuffer >>= count;
        bitsInBuffer -= count;
    }

    uint32_t readBits(unsigned int count) {
        uint32_t value = peekBits(count);
        dropBits(count);
        return value;
    }

    uint8_t readByte() {
        return static_cast<uint8_t>(readBits(8));
    }

    void alignToByte() {
        int discard = bitsInBuffer % 8;
        dropBits(discard);
    }

private:
    const uint8_t* data;
    std::size_t size;
    std::size_t bytePos = 0;

    uint64_t bitBuffer = 0;
    unsigned int bitsInBuffer = 0;
};

class HuffmanTree {
public:
    std::vector<int> lengths;

    void build(const std::vector<int>& lengths_) {
        lengths = lengths_;
        maxBits = 0;

        for(int l : lengths)
            if(l > maxBits) maxBits = l;

        if(maxBits == 0) return;

        std::vector<int> bl_count(maxBits + 1, 0);
        for(int l : lengths) {
            if(l > 0) bl_count[l]++;
        }
        std::vector<int> next_code(maxBits + 1, 0);
        int code = 0;
        for(int bits = 1; bits <= maxBits; bits++) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }

        lut.assign(1 << maxBits, -1);

        for(size_t i = 0; i < lengths.size(); i++) {
            int len = lengths[i];
            if(len != 0) {
                int c = next_code[len]++;

                int reversed_c = 0;
                for(int b = 0; b < len; b++) {
                    reversed_c |= ((c >> (len - 1 - b)) & 1) << b;
                }

                int fill_count = 1 << (maxBits - len);
                for(int j = 0; j < fill_count; j++) {
                    int lut_idx = reversed_c | (j << len);
                    lut[lut_idx] = static_cast<int>(i);
                }
            }
        }
    }

    int decode(BitReader& r) const {
        if(maxBits == 0) {
            throw std::runtime_error("Empty Huffman tree\n");
        }

        uint32_t peeked = r.peekBits(maxBits);
        int sym = lut[peeked];

        if(sym == -1) {
            throw std::runtime_error("Invalid Huffman code\n");
        }

        int len = lengths[sym];
        r.dropBits(len);
        return sym;
    }

private:
    std::vector<int> lut;
    int maxBits = 0;
};

void buildFixedTrees(HuffmanTree& lit, HuffmanTree& dist) {
    std::vector<int> litLen(288);
    std::vector<int> distLen(32);

    for(int i = 0; i <= 143; i++) litLen[i] = 8;
    for(int i = 144; i <= 255; i++) litLen[i] = 9;
    for(int i = 256; i <= 279; i++) litLen[i] = 7;
    for(int i = 280; i <= 287; i++) litLen[i] = 8;

    for(int i = 0; i < 32; i++) distLen[i] = 5;

    lit.build(litLen);
    dist.build(distLen);
}

int decodeLength(BitReader& r, int sym) {
    static const int length_base[] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

    static const int length_extra[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

    int idx = sym - 257;
    int len = length_base[idx];

    if(length_extra[idx] > 0) {
        len += r.readBits(length_extra[idx]);
    }
    return len;
}

void copyMatch(std::vector<uint8_t>& out, int len, int dist) {
    int start = static_cast<int>(out.size()) - dist;

    if(start < 0) {
        throw std::runtime_error("Invalid backward distance in DEFLATE match\n");
    }

    for(int i = 0; i < len; i++) {
        out.push_back(out[start + i]);
    }
}

void decodeBlockData(BitReader& r, std::vector<uint8_t>& out, HuffmanTree& litTree, HuffmanTree& distTree) {
    while(true) {
        int sym = litTree.decode(r);

        if(sym < 256) {
            out.push_back(sym);
        }
        else if(sym == 256) {
            break;
        }
        else {
            int len = decodeLength(r, sym);
            int distSym = distTree.decode(r);

            static const int dist_base[] = {
                1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                4097, 6145, 8193, 12289, 16385, 24577};

            static const int dist_extra[] = {
                0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

            int dist = dist_base[distSym];

            if(dist_extra[distSym] > 0)
                dist += r.readBits(dist_extra[distSym]);

            copyMatch(out, len, dist);
        }
    }
}

void decodeCodeLengths(BitReader& r, HuffmanTree& clTree, std::vector<int>& out, int total) {
    int i = 0;
    while(i < total) {
        int sym = clTree.decode(r);

        if(sym <= 15) {
            out.push_back(sym);
            i++;
        }
        else if(sym == 16) {
            int repeat = r.readBits(2) + 3;

            if(out.empty()) {
                throw std::runtime_error("Invalid repeat code in DEFLATE tree\n");
            }

            int last = out.back();

            for(int j = 0; j < repeat; j++) {
                out.push_back(last);
                i++;
            }
        }
        else if(sym == 17) {
            int repeat = r.readBits(3) + 3;
            for(int j = 0; j < repeat; j++) {
                out.push_back(0);
                i++;
            }
        }
        else if(sym == 18) {
            int repeat = r.readBits(7) + 11;
            for(int j = 0; j < repeat; j++) {
                out.push_back(0);
                i++;
            }
        }
    }
}

void decompressStoredBlock(BitReader& r, std::vector<uint8_t>& out) {
    r.alignToByte();

    uint16_t len = r.readBits(16);
    uint16_t nlen = r.readBits(16);

    if((len ^ 0xFFFF) != nlen) {
        throw std::runtime_error("Corrupted stored block\n");
    }

    for(uint16_t i = 0; i < len; ++i) {
        out.push_back(r.readByte());
    }
}

void decompressDynamicBlock(BitReader& r, std::vector<uint8_t>& out) {
    int HLIT = r.readBits(5) + 257;
    int HDIST = r.readBits(5) + 1;
    int HCLEN = r.readBits(4) + 4;

    int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    std::vector<int> clLengths(19, 0);

    for(int i = 0; i < HCLEN; i++) {
        clLengths[order[i]] = r.readBits(3);
    }

    HuffmanTree clTree;
    clTree.build(clLengths);

    std::vector<int> litLen;
    std::vector<int> distLen;

    decodeCodeLengths(r, clTree, litLen, HLIT);
    decodeCodeLengths(r, clTree, distLen, HDIST);

    HuffmanTree litTree;
    HuffmanTree distTree;

    litTree.build(litLen);
    distTree.build(distLen);

    decodeBlockData(r, out, litTree, distTree);
}

std::vector<uint8_t> decodeZlibStream(const std::vector<uint8_t>& byte_array, size_t expected_size = 0) {
    if(byte_array.size() < 6) { // underflow
        throw std::runtime_error("IDAT payload too small for Zlib header\n");
    }

    uint8_t cmf = byte_array[0];
    uint8_t compressionMethod = cmf & 0x0F;
    // uint8_t cinfo = cmf >> 4; // not used

    if(compressionMethod != 8) {
        throw std::runtime_error("Unsupported compression method\n");
    }

    BitReader reader(byte_array.data() + 2, byte_array.size() - 6);
    std::vector<uint8_t> out;

    if(expected_size > 0) {
        out.reserve(expected_size);
    }

    bool final_block = false;

    while(!final_block) {
        final_block = reader.readBits(1);
        uint8_t block_type = reader.readBits(2);

        switch(block_type) {
        case 0:
            decompressStoredBlock(reader, out);
            break;

        case 1: {
            HuffmanTree lit, dist;
            buildFixedTrees(lit, dist);
            decodeBlockData(reader, out, lit, dist);
            break;
        }

        case 2:
            decompressDynamicBlock(reader, out);
            break;

        default:
            throw std::runtime_error("Invalid DEFLATE block");
        }
    }

    return out;
}

uint8_t paethPredictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if(pa <= pb && pa <= pc) return a;
    if(pb <= pc) return b;
    return c;
}

void unfilterRow(uint8_t filter_type, uint8_t* row, const uint8_t* prev_row, size_t row_bytes, uint32_t bpp) {
    switch(filter_type) {
    case 0: // None
        break;
    case 1: // Sub
        for(size_t i = bpp; i < row_bytes; ++i) {
            row[i] = row[i] + row[i - bpp];
        }
        break;
    case 2: // Up
        if(prev_row) {
            for(size_t i = 0; i < row_bytes; ++i) {
                row[i] = row[i] + prev_row[i];
            }
        }
        break;
    case 3: // Average
        for(size_t i = 0; i < row_bytes; ++i) {
            uint8_t left = (i >= bpp) ? row[i - bpp] : 0;
            uint8_t up = prev_row ? prev_row[i] : 0;
            row[i] = row[i] + (left + up) / 2;
        }
        break;
    case 4: // Paeth
        for(size_t i = 0; i < row_bytes; ++i) {
            uint8_t left = (i >= bpp) ? row[i - bpp] : 0;
            uint8_t up = prev_row ? prev_row[i] : 0;
            uint8_t up_left = (i >= bpp && prev_row) ? prev_row[i - bpp] : 0;
            row[i] = row[i] + paethPredictor(left, up, up_left);
        }
        break;
    default:
        throw std::runtime_error("Image unfiltering: Invalid filter type\n");
    }
}

bool parsePngInfo(const std::string& path,
                  std::vector<uint8_t>& out_pixels,
                  uint32_t* width = nullptr,
                  uint32_t* height = nullptr,
                  uint8_t* bit_depth = nullptr,
                  uint8_t* color_type = nullptr,
                  uint8_t* compression = nullptr,
                  uint8_t* filter = nullptr,
                  uint8_t* interlace = nullptr) {
    constexpr std::array<uint8_t, 8> PNG_SIGNATURE = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    std::ifstream file(path, std::ios::binary);

    std::array<uint8_t, 8> sig;
    file.read(reinterpret_cast<char*>(sig.data()), 8);

    if(!file || sig != PNG_SIGNATURE) return false;

    uint32_t local_w = 0, local_h = 0;
    uint8_t local_bd = 0, local_ct = 0, local_il = 0;

    uint32_t chunk_len, chunk_type;
    std::streampos file_size = getFileSize(file);

    debug(std::format("file size in bytes: {}", static_cast<long long>(file_size)));

    std::vector<uint8_t> byte_array(0);
    bool EOF_REACHED = false;

    while(file.tellg() < file_size && !EOF_REACHED) {
        file.read(reinterpret_cast<char*>(&chunk_len), 4);
        file.read(reinterpret_cast<char*>(&chunk_type), 4);

        toBigEndian(chunk_len);
        toBigEndian(chunk_type);

        switch(chunk_type) {
        case CHUNK_IHDR:
            debug(std::format("found chunk: {}, length: {}", chunkTypeToStr(chunk_type), chunk_len));

            file.read(reinterpret_cast<char*>(&local_w), 4);
            file.read(reinterpret_cast<char*>(&local_h), 4);
            file.read(reinterpret_cast<char*>(&local_bd), 1);
            file.read(reinterpret_cast<char*>(&local_ct), 1);
            if(compression != nullptr) file.read(reinterpret_cast<char*>(compression), 1);
            else file.seekg(1, std::ios::cur);
            if(filter != nullptr) file.read(reinterpret_cast<char*>(filter), 1);
            else file.seekg(1, std::ios::cur);
            file.read(reinterpret_cast<char*>(&local_il), 1);

            toBigEndian(local_w);
            toBigEndian(local_h);

            if(width) *width = local_w;
            if(height) *height = local_h;
            if(bit_depth) *bit_depth = local_bd;
            if(color_type) *color_type = local_ct;
            if(interlace) *interlace = local_il;
            break;

        case CHUNK_IDAT: {
            debug(std::format("found chunk: {}, length: {}", chunkTypeToStr(chunk_type), chunk_len));

            std::size_t byte_array_size = byte_array.size();
            byte_array.resize(byte_array_size + chunk_len);
            file.read(reinterpret_cast<char*>(byte_array.data() + byte_array_size), chunk_len);
            break;
        }

        case CHUNK_IEND:
            debug(std::format("found chunk: {}, length: {}", chunkTypeToStr(chunk_type), chunk_len));

            file.close();
            EOF_REACHED = true;
            break;

        default:
            debug(std::format("found chunk: {}, length: {} -> skipping", chunkTypeToStr(chunk_type), chunk_len));

            file.seekg(chunk_len, std::ios::cur);
            break;
        }

        if(!EOF_REACHED) file.seekg(4, std::ios::cur);
    }

    uint32_t channels = 1;
    if(local_ct == 2) channels = 3;
    else if(local_ct == 4) channels = 2;
    else if(local_ct == 6) channels = 4;

    uint32_t bpp = std::max(1u, (channels * local_bd + 7) / 8); // bytes per pixel

    debug(std::format("channels: {}, bytes per pixel: {}", channels, bpp));

    try {
        debug("Trying to decode image");

        std::vector<uint8_t> decoded_image = decodeZlibStream(byte_array, 0);

        debug("Successfully decoded image!");

        out_pixels.assign(local_w * local_h * bpp, 0);
        size_t offset = 0;

        if(local_il == 0) { // Non-interlaced
            size_t row_bytes = (local_w * channels * local_bd + 7) / 8;
            std::vector<uint8_t> prev_row(row_bytes, 0);

            for(uint32_t y = 0; y < local_h; ++y) {
                if(offset >= decoded_image.size()) break;
                uint8_t filter_type = decoded_image[offset++];
                uint8_t* row_ptr = &decoded_image[offset];

                unfilterRow(filter_type, row_ptr, prev_row.data(), row_bytes, bpp);

                std::memcpy(&out_pixels[y * local_w * bpp], row_ptr, row_bytes);
                std::memcpy(prev_row.data(), row_ptr, row_bytes);
                offset += row_bytes;
            }
        }
        else if(local_il == 1) { // Adam7 interlace
            constexpr int a7_x_start[7] = {0, 4, 0, 2, 0, 1, 0};
            constexpr int a7_y_start[7] = {0, 0, 4, 0, 2, 0, 1};
            constexpr int a7_x_step[7] = {8, 8, 4, 4, 2, 2, 1};
            constexpr int a7_y_step[7] = {8, 8, 8, 4, 4, 2, 2};

            for(int pass = 0; pass < 7; ++pass) {
                uint32_t pass_w = (local_w > a7_x_start[pass]) ? (local_w - a7_x_start[pass] + a7_x_step[pass] - 1) / a7_x_step[pass] : 0;
                uint32_t pass_h = (local_h > a7_y_start[pass]) ? (local_h - a7_y_start[pass] + a7_y_step[pass] - 1) / a7_y_step[pass] : 0;

                if(pass_w == 0 || pass_h == 0) continue;

                size_t row_bytes = (pass_w * channels * local_bd + 7) / 8;
                std::vector<uint8_t> prev_row(row_bytes, 0);

                for(uint32_t py = 0; py < pass_h; ++py) {
                    if(offset >= decoded_image.size()) break;

                    uint8_t filter_type = decoded_image[offset++];
                    uint8_t* row_ptr = &decoded_image[offset];

                    unfilterRow(filter_type, row_ptr, prev_row.data(), row_bytes, bpp);

                    uint32_t y = a7_y_start[pass] + py * a7_y_step[pass];

                    // applying pos
                    for(uint32_t px = 0; px < pass_w; ++px) {
                        uint32_t x = a7_x_start[pass] + px * a7_x_step[pass];

                        // for rendering in the terminal we work with bytes (8-bit or more)
                        if(local_bd >= 8) {
                            for(uint32_t c = 0; c < bpp; ++c) {
                                out_pixels[(y * local_w + x) * bpp + c] = row_ptr[px * bpp + c];
                            }
                        }
                        else {
                            throw std::runtime_error("Adam7 images with bit depth < 8 are not supported.");
                        }
                    }

                    std::memcpy(prev_row.data(), row_ptr, row_bytes);
                    offset += row_bytes;
                }
            }
        }
    }
    catch(const std::exception& e) {
        throw std::runtime_error(std::format("Failed to decode PNG: {}", e.what()));
    }

    return true;
}

/* ---------- Part 2: converting to ascii ---------- */

double DISPLAY_MULTIPLIER = 1, SMOOTHING_MULTIPLIER = 1;
uint32_t DISPLAY_WIDTH = -1, DISPLAY_HEIGHT = -1;
bool PIXEL_AVERAGING_TYPE = 0; // 0 - exact value, 1 - neighbor average

using Palette = std::array<std::pair<uint8_t, char>, 10>;
// {upper intensity limit, char}
constexpr std::array<Palette, 10> ASCII_PALETTES = {
    Palette({{5, ' '},
             {20, '.'},
             {30, ':'},
             {40, '-'},
             {50, '='},
             {60, '+'},
             {70, '*'},
             {80, '#'},
             {90, '%'},
             {100, '@'}}),
    Palette({{5, '@'},
             {20, '%'},
             {30, '#'},
             {40, '*'},
             {50, '='},
             {60, '+'},
             {70, '='},
             {80, '-'},
             {90, '.'},
             {100, ' '}}),
    Palette({{5, ' '},
             {20, '.'},
             {30, '_'},
             {40, ':'},
             {50, ','},
             {60, '^'},
             {70, 'i'},
             {80, 'I'},
             {90, '&'},
             {100, 'W'}}),
    Palette({{5, 'W'},
             {20, '&'},
             {30, 'I'},
             {40, 'i'},
             {50, '^'},
             {60, ','},
             {70, ':'},
             {80, '_'},
             {90, '.'},
             {100, ' '}}),
    Palette({{5, ' '},
             {20, '['},
             {30, '1'},
             {40, '0'},
             {50, '/'},
             {60, 'x'},
             {70, 'X'},
             {80, '#'},
             {90, 'W'},
             {100, '@'}}),
    Palette({{5, '@'},
             {20, 'W'},
             {30, '#'},
             {40, 'X'},
             {50, 'x'},
             {60, '/'},
             {70, '0'},
             {80, '1'},
             {90, '['},
             {100, ' '}}),
    Palette({{5, ' '},
             {20, '.'},
             {30, 'c'},
             {40, 'o'},
             {50, 'O'},
             {60, 'm'},
             {70, 'M'},
             {80, 'W'},
             {90, '8'},
             {100, 'B'}}),
    Palette({{5, 'B'},
             {20, '8'},
             {30, 'W'},
             {40, 'M'},
             {50, 'm'},
             {60, 'O'},
             {70, 'o'},
             {80, 'c'},
             {90, '.'},
             {100, ' '}}),
    Palette({{5, ' '},
             {20, '.'},
             {30, ','},
             {40, '/'},
             {50, '\\'},
             {60, '|'},
             {70, '('},
             {80, 't'},
             {90, 'X'},
             {100, '#'}}),
    Palette({{5, '#'},
             {20, 'X'},
             {30, 't'},
             {40, '('},
             {50, '|'},
             {60, '\\'},
             {70, '/'},
             {80, ','},
             {90, '.'},
             {100, ' '}}),
};

std::vector<uint32_t> USED_ASCII_PALETTES = {0};

uint8_t convertToGrayscale(uint8_t R, uint8_t G, uint8_t B) {
    return static_cast<uint8_t>(0.299 * R + 0.587 * G + 0.114 * B);
}

uint8_t averageNeighbors(const std::vector<uint8_t>& pixels,
                         uint32_t width, uint32_t height,
                         uint32_t X, uint32_t Y,
                         uint32_t span_x, uint32_t span_y) {
    uint32_t result = 0;
    uint32_t cnt = 0;

    uint32_t start_y = (Y > span_y) ? (Y - span_y) : 0;
    uint32_t end_y = std::min(Y + span_y, height - 1);

    uint32_t start_x = (X > span_x) ? (X - span_x) : 0;
    uint32_t end_x = std::min(X + span_x, width - 1);

    for(uint32_t y = start_y; y <= end_y; ++y) {
        for(uint32_t x = start_x; x <= end_x; ++x) {
            uint32_t idx = y * width + x;
            result += pixels[idx];
            ++cnt;
        }
    }

    return (cnt > 0) ? static_cast<uint8_t>(result / cnt) : 0;
}

void renderTerminalImage(const std::string& filepath) {
    uint32_t WIDTH = 0, HEIGHT = 0;
    uint8_t BIT_DEPTH = 0, COLOR_TYPE = 0, INTERLACE = 0;
    std::vector<uint8_t> pixels;

    parsePngInfo(filepath,
                 pixels,
                 &WIDTH,
                 &HEIGHT,
                 &BIT_DEPTH,
                 &COLOR_TYPE,
                 nullptr,
                 nullptr,
                 &INTERLACE);

    if(DISPLAY_WIDTH == -1) {
        if(DISPLAY_HEIGHT == -1) {
            debug("Display resolution NOT SPECIFIED: Using native resolution.");
            DISPLAY_WIDTH = WIDTH;
            DISPLAY_HEIGHT = HEIGHT;
        }
        else {
            debug("DISPLAY_HEIGHT specified: Calculating DISPLAY_WIDTH.");
            DISPLAY_WIDTH = static_cast<uint32_t>(static_cast<double>(WIDTH) / HEIGHT * DISPLAY_HEIGHT);
        }
    }
    else {
        if(DISPLAY_HEIGHT == -1) {
            debug("DISPLAY_WIDTH specified: Calculating DISPLAY_HEIGHT.");
            DISPLAY_HEIGHT = static_cast<uint32_t>(static_cast<double>(HEIGHT) / WIDTH * DISPLAY_WIDTH);
        }
        else {
            debug("Display resolution SPECIFIED: Using user's resolution.");
        }
    }

    DISPLAY_WIDTH = static_cast<uint32_t>(static_cast<double>(DISPLAY_WIDTH) * DISPLAY_MULTIPLIER);
    DISPLAY_HEIGHT = static_cast<uint32_t>(static_cast<double>(DISPLAY_HEIGHT) * DISPLAY_MULTIPLIER);

    debug(std::format("Image decoded successfully: {} ({}x{})", filepath, WIDTH, HEIGHT));
    debug(INTERLACE == 1 ? "[Adam7 Interlaced]" : "[No Interlace]");

    debug(std::format("displayed image resolution: {}x{}", 2 * DISPLAY_WIDTH, DISPLAY_HEIGHT));

    uint32_t channels = 1;                 // default grayscale (COLOR_TYPE == 0)
    if(COLOR_TYPE == 2) channels = 3;      // RGB
    else if(COLOR_TYPE == 4) channels = 2; // grayscale + Alpha
    else if(COLOR_TYPE == 6) channels = 4; // RGBA

    // COLOR_TYPE == 3
    else throw std::runtime_error("Image to ascii conversion: Color type is set to 3 which is not supported (as well as PLTE chunks)\n");

    int intensity;
    char ascii_char;
    uint8_t gray;
    uint32_t idx;
    std::vector<uint8_t> gray_pixels(WIDTH * HEIGHT);

    for(uint32_t y = 0; y < HEIGHT; ++y) {
        for(uint32_t x = 0; x < WIDTH; ++x) {
            idx = (y * WIDTH + x) * channels;
            gray = 0;

            if(channels >= 3) { // r, g, b
                gray = convertToGrayscale(pixels[idx], pixels[idx + 1], pixels[idx + 2]);
            }
            else { // already grayscale
                gray = pixels[idx];
            }

            gray_pixels[y * WIDTH + x] = gray;
        }
    }

    double scale_x = static_cast<double>(WIDTH) / DISPLAY_WIDTH, scale_y = static_cast<double>(HEIGHT) / DISPLAY_HEIGHT;
    uint32_t cur_y, cur_x;

    std::vector<uint8_t> scaled_intensity_pixels;
    scaled_intensity_pixels.reserve(DISPLAY_WIDTH * DISPLAY_HEIGHT);

    for(double y = 0; y < HEIGHT; y += scale_y) {
        for(double x = 0; x < WIDTH; x += scale_x) {
            // prevent possible segfault
            cur_y = std::min(static_cast<uint32_t>(y), HEIGHT);
            cur_x = std::min(static_cast<uint32_t>(x), WIDTH);

            idx = cur_y * WIDTH + cur_x;

            gray = gray_pixels[idx];

            if(PIXEL_AVERAGING_TYPE == 1) {
                gray = averageNeighbors(gray_pixels, WIDTH, HEIGHT, cur_x, cur_y, static_cast<uint32_t>(scale_x / 2 * SMOOTHING_MULTIPLIER), static_cast<uint32_t>(scale_y / 2 * SMOOTHING_MULTIPLIER));
            }

            // Mapping values 0-255 -> brightness 0-100.
            // inversion: 0 (black) = 100% "intensity", 255 (white) = 0%
            scaled_intensity_pixels.push_back(100 - ((gray * 100) / 255));
        }
    }

    // output images for specified palettes
    for(const auto& palette_idx : USED_ASCII_PALETTES) {
        for(uint32_t y = 0; y < DISPLAY_HEIGHT; ++y) {
            for(uint32_t x = 0; x < DISPLAY_WIDTH; ++x) {
                idx = (y * DISPLAY_WIDTH + x);
                ascii_char = ASCII_PALETTES[palette_idx].back().second; // fallback

                for(const auto& pair : ASCII_PALETTES[palette_idx]) {
                    if(scaled_intensity_pixels[idx] <= pair.first) {
                        ascii_char = pair.second;
                        break;
                    }
                }

                // TODO optional: better aspect ratio (now its 2:1)
                std::cout << ascii_char << ascii_char;
            }
            std::cout << '\n';
        }
        std::cout << '\n';
    }
}

/* ---------- Misc: debug and help ---------- */

void printHelpMessage() {
    constexpr int FLAG_WIDTH = 12;

    auto printFlag = [](const std::string& flag, const std::string& desc) {
        std::cout << "  "
                  << std::left << std::setw(FLAG_WIDTH) << flag
                  << desc << '\n';
    };

    std::cout << "DESCRIPTION:\n"
              << "Terminal Image Ascii Viewer - A tool for converting PNG images to ASCII text with various options.\n\n";

    std::cout << "FLAGS:\n";

    printFlag("--help", "Show this help message.");
    std::cout << '\n';

    printFlag("-f", "Specify the path to a file or list of files.");
    std::cout << "              Note that other \"option\"-type flags must precede\n"
              << "              the files (\"-f\" argument) they should apply to.\n"
              << "              See EXAMPLE USAGE below.\n\n";

    printFlag("-w", "\"option\"-type flag. Specify output width (integer).");
    std::cout << "              The specified <width> corresponds to 2 * <width> output characters.\n"
              << "              Example: -w 20 -> output width will be 40 characters.\n\n";

    printFlag("-h", "\"option\"-type flag. Specify output height (integer).");
    std::cout << '\n';

    printFlag("-m", "\"option\"-type flag. Specify output resolution multiplier.");
    std::cout << '\n';

    printFlag("--debug", "Print debug messages.");
    std::cout << '\n';

    printFlag("--smooth", "\"option\"-type flag. Use averaging instead of nearest-neighbor scaling.");
    std::cout << "              Optional: specify smoothing multiplier (default: 1).\n\n";

    printFlag("--plte", "\"option\"-type flag. Specify predefined palette(s).");
    std::cout << "              Can be a single number or a list of numbers (default: 0).\n"
              << "              See EXAMPLE USAGE below.\n\n";

    std::cout
        << "EXAMPLE USAGE:\n"
        << "  ./tiav -f path_to_image/image.png\n\n"
        << "  ./tiav -f path_to_image1/image1.png path_to_image2/image2.png\n\n"
        << "  ./tiav -m 2 --plte 1 2 -f path_to_image1/image1.png path_to_image2/image2.png\n\n"
        << "    Note: This example will:\n"
        << "      - set output resolution to 2x the first image\n"
        << "      - print both images using palette 1\n"
        << "      - print both images using palette 2\n"
        << "      - restore default options after processing\n\n"
        << "  ./tiav -w 20 -f path_to_image/image.png -f path_to_image/image.png\n\n"
        << "    Note: This example will:\n"
        << "      - set output width to 20 (40 terminal characters)\n"
        << "      - calculate height from the image aspect ratio\n"
        << "      - print the first image\n"
        << "      - restore default resolution settings\n"
        << "      - print the second image using native resolution\n";
}

int main(int argc, char* argv[]) {
    if(argc == 1) {
        std::cerr << "Not enough arguments provided. See './tiav --help' for usage.\n";
        return 1;
    }

    std::string arg;

    for(int i = 1; i < argc; ++i) {
        arg = argv[i];

        if(arg == "--help") {
            printHelpMessage();
            return 0;
        }
        else if(arg == "-w") {
            if(i + 1 < argc) {
                try {
                    DISPLAY_WIDTH = std::stoul(argv[i + 1]);
                    ++i;
                }
                catch(const std::exception& e) {
                    std::cerr << "Error: " << e.what() << '\n';
                    return 1;
                }
            }
            else {
                std::cerr << "Not enough arguments provided. See './tiav --help' for usage.\n";
                return 1;
            }
        }
        else if(arg == "-h") {
            if(i + 1 < argc) {
                try {
                    DISPLAY_HEIGHT = std::stoul(argv[i + 1]);
                    ++i;
                }
                catch(const std::exception& e) {
                    std::cerr << "Error: " << e.what() << '\n';
                    return 1;
                }
            }
            else {
                std::cerr << "Not enough arguments provided. See './tiav --help' for usage.\n";
                return 1;
            }
        }
        else if(arg == "-m") {
            if(i + 1 < argc) {
                try {
                    DISPLAY_MULTIPLIER = std::stod(argv[i + 1]);
                    ++i;
                }
                catch(const std::exception& e) {
                    std::cerr << "Error: " << e.what() << '\n';
                    return 1;
                }
            }
            else {
                std::cerr << "Not enough arguments provided. See './tiav --help' for usage.\n";
                return 1;
            }
        }
        else if(arg == "--smooth") {
            PIXEL_AVERAGING_TYPE = 1;

            if(i + 1 < argc && argv[i + 1][0] != '-') {
                try {
                    SMOOTHING_MULTIPLIER = std::stod(argv[i + 1]);
                    ++i;
                }
                catch(const std::exception&) {
                    std::cerr << "Warning: invalid smoothing multiplier. Using default value (1).\n";
                }
            }
        }
        else if(arg == "--plte") {
            if(i + 1 < argc) {
                if(std::string(argv[i + 1]) == "all") {
                    USED_ASCII_PALETTES.clear();
                    USED_ASCII_PALETTES.resize(ASCII_PALETTES.size());
                    for(size_t j = 0; j < ASCII_PALETTES.size(); ++j) {
                        USED_ASCII_PALETTES[j] = j;
                    }
                    ++i;
                }
                else {
                    USED_ASCII_PALETTES.clear();
                    while(i + 1 < argc && argv[i + 1][0] != '-') {
                        ++i;
                        try {
                            USED_ASCII_PALETTES.push_back(std::stoul(argv[i]));
                        }
                        catch(const std::exception& e) {
                            std::cerr << "Error: " << e.what() << '\n';
                            return 1;
                        }
                    }

                    if(USED_ASCII_PALETTES.empty()) {
                        std::cerr << "Not enough arguments provided. See './tiav --help' for usage.\n";
                        return 1;
                    }
                }
            }
            else {
                std::cerr << "Not enough arguments provided. See './tiav --help' for usage.\n";
                return 1;
            }
        }
        else if(arg == "--debug") {
            DEBUG = true;
            debug("DEBUG mode is on");
        }
        else if(arg == "-f") {
            while(i + 1 < argc && argv[i + 1][0] != '-') {
                ++i;
                try {
                    renderTerminalImage(argv[i]);
                    std::cout << '\n';
                }
                catch(const std::exception& e) {
                    std::cerr << "Error: " << e.what() << '\n';
                    return 1;
                }
            }

            // set some values to default (so that the settings that follow current group of files will apply exactly how they should)
            DISPLAY_MULTIPLIER = 1;
            DISPLAY_WIDTH = -1;
            DISPLAY_HEIGHT = -1;
        }
        else {
            std::cerr << "Unknown flag encountered '" << arg << "'. See './tiav --help' for usage.\n";
            return 1;
        }
    }
    return 0;
}