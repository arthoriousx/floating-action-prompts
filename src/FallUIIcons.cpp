#include "FallUIIcons.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace FallUIIcons
{
    namespace
    {
        inline constexpr std::size_t kMaxSwfFileBytes = 16u * 1024u * 1024u;
        inline constexpr std::size_t kMaxXmlFileBytes = 4u * 1024u * 1024u;
        inline constexpr std::size_t kMaxShapesPerSwf = 4096u;
        inline constexpr std::size_t kMaxEdgesPerShape = 16384u;
        inline constexpr std::size_t kMaxAddons = 64u;

        [[nodiscard]] std::string ToLowerAscii(std::string_view a_sv)
        {
            std::string out;
            out.reserve(a_sv.size());
            for (const unsigned char ch : a_sv) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            }
            return out;
        }

        [[nodiscard]] std::string TrimSv(std::string_view a_sv)
        {
            const auto first = a_sv.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = a_sv.find_last_not_of(" \t\r\n");
            return std::string(a_sv.substr(first, last - first + 1));
        }

        [[nodiscard]] std::string NormalizeSlashStr(std::string a_s)
        {
            std::replace(a_s.begin(), a_s.end(), '\\', '/');
            while (!a_s.empty() && a_s.front() == '/') {
                a_s.erase(0, 1);
            }
            if (a_s.size() >= 10 && ToLowerAscii(std::string_view(a_s).substr(0, 10)) == "interface/") {
                a_s.erase(0, 10);
            }
            return a_s;
        }

        [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& a_p)
        {
            const std::u8string u8 = a_p.u8string();
            return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        }

        [[nodiscard]] std::string NormalizeSymbolKey(std::string_view a_name)
        {
            std::string trimmed = TrimSv(a_name);
            if (trimmed.size() >= 2 && (trimmed[0] == 'm' || trimmed[0] == 'M') && trimmed[1] == '_') {
                trimmed.erase(0, 2);
            }
            return ToLowerAscii(trimmed);
        }

        [[nodiscard]] bool ReadWholeFileBytes(
            const std::filesystem::path& a_path,
            std::size_t a_maxBytes,
            std::vector<std::uint8_t>& a_out)
        {
            a_out.clear();
            std::error_code ec;
            if (!std::filesystem::exists(a_path, ec) || !std::filesystem::is_regular_file(a_path, ec)) {
                return false;
            }
            const auto fileSize = std::filesystem::file_size(a_path, ec);
            if (ec || fileSize == 0 || fileSize > a_maxBytes) {
                return false;
            }

            std::ifstream ifs(a_path, std::ios::binary);
            if (!ifs.is_open()) {
                return false;
            }

            a_out.resize(static_cast<std::size_t>(fileSize));
            if (!ifs.read(reinterpret_cast<char*>(a_out.data()), static_cast<std::streamsize>(fileSize))) {
                a_out.clear();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadWholeFileText(
            const std::filesystem::path& a_path,
            std::size_t a_maxBytes,
            std::string& a_out)
        {
            std::vector<std::uint8_t> bytes;
            if (!ReadWholeFileBytes(a_path, a_maxBytes, bytes)) {
                a_out.clear();
                return false;
            }
            a_out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }

        [[nodiscard]] std::string StripBracketWrapper(std::string_view a_raw)
        {
            std::string s = TrimSv(a_raw);
            while (s.size() >= 2 &&
                   ((s.front() == '[' && s.back() == ']') ||
                    (s.front() == '{' && s.back() == '}') ||
                    (s.front() == '|' && s.back() == '|'))) {
                s = TrimSv(std::string_view(s).substr(1, s.size() - 2));
            }
            return s;
        }

        // ---- Self-Contained RFC 1950 / RFC 1951 DEFLATE Inflate ----
        struct DeflateBitReader
        {
            const std::uint8_t* data{ nullptr };
            std::size_t size{ 0 };
            std::size_t bytePos{ 0 };
            int bitPos{ 0 };
            bool error{ false };

            [[nodiscard]] int ReadBits(int a_count) noexcept
            {
                if (error || a_count < 0 || a_count > 24) {
                    error = true;
                    return 0;
                }
                int val = 0;
                for (int i = 0; i < a_count; ++i) {
                    if (bytePos >= size) {
                        error = true;
                        return 0;
                    }
                    const int bit = (data[bytePos] >> bitPos) & 1;
                    val |= (bit << i);
                    if (++bitPos == 8) {
                        bitPos = 0;
                        ++bytePos;
                    }
                }
                return val;
            }

            void AlignToByte() noexcept
            {
                if (bitPos != 0) {
                    bitPos = 0;
                    ++bytePos;
                }
            }

            [[nodiscard]] std::uint16_t ReadU16LEAligned() noexcept
            {
                if (bytePos + 2 > size) {
                    error = true;
                    return 0;
                }
                const std::uint16_t v = static_cast<std::uint16_t>(data[bytePos]) |
                                        (static_cast<std::uint16_t>(data[bytePos + 1]) << 8);
                bytePos += 2;
                return v;
            }
        };

        struct HuffmanTable
        {
            static constexpr int kMaxBits = 15;
            std::array<int, kMaxBits + 1> counts{};
            std::array<int, kMaxBits + 1> firstCode{};
            std::array<int, kMaxBits + 1> firstSym{};
            std::vector<int> symbols;
            bool valid{ false };

            bool Build(const int* a_lengths, int a_numSyms, bool a_allowEmpty)
            {
                counts.fill(0);
                int nonZero = 0;
                for (int i = 0; i < a_numSyms; ++i) {
                    const int len = a_lengths[i];
                    if (len < 0 || len > kMaxBits) {
                        return false;
                    }
                    if (len > 0) {
                        ++counts[len];
                        ++nonZero;
                    }
                }
                if (nonZero == 0) {
                    symbols.clear();
                    firstCode.fill(0);
                    firstSym.fill(0);
                    valid = a_allowEmpty;
                    return a_allowEmpty;
                }

                int code = 0;
                int symIdx = 0;
                int left = 1;
                for (int len = 1; len <= kMaxBits; ++len) {
                    left <<= 1;
                    left -= counts[len];
                    if (left < 0) {
                        return false;
                    }
                    firstCode[len] = code;
                    firstSym[len] = symIdx;
                    code = (code + counts[len]) << 1;
                    symIdx += counts[len];
                }

                symbols.assign(static_cast<std::size_t>(symIdx), 0);
                std::array<int, kMaxBits + 1> offsets = firstSym;
                for (int sym = 0; sym < a_numSyms; ++sym) {
                    const int len = a_lengths[sym];
                    if (len > 0) {
                        symbols[static_cast<std::size_t>(offsets[len]++)] = sym;
                    }
                }
                valid = true;
                return true;
            }

            [[nodiscard]] int Decode(DeflateBitReader& a_br) const noexcept
            {
                if (!valid) {
                    a_br.error = true;
                    return -1;
                }
                int code = 0;
                for (int len = 1; len <= kMaxBits; ++len) {
                    const int bit = a_br.ReadBits(1);
                    if (a_br.error) {
                        return -1;
                    }
                    code = (code << 1) | bit;
                    if (counts[len] > 0) {
                        const int diff = code - firstCode[len];
                        if (diff >= 0 && diff < counts[len]) {
                            return symbols[static_cast<std::size_t>(firstSym[len] + diff)];
                        }
                    }
                }
                a_br.error = true;
                return -1;
            }
        };

        void BuildFixedTables(HuffmanTable& a_litLen, HuffmanTable& a_dist)
        {
            std::array<int, 288> litLens{};
            for (int i = 0; i <= 143; ++i) litLens[static_cast<std::size_t>(i)] = 8;
            for (int i = 144; i <= 255; ++i) litLens[static_cast<std::size_t>(i)] = 9;
            for (int i = 256; i <= 279; ++i) litLens[static_cast<std::size_t>(i)] = 7;
            for (int i = 280; i <= 287; ++i) litLens[static_cast<std::size_t>(i)] = 8;
            (void)a_litLen.Build(litLens.data(), 288, false);

            std::array<int, 32> distLens{};
            distLens.fill(5);
            (void)a_dist.Build(distLens.data(), 32, false);
        }

        bool DecodeHuffmanBlock(
            DeflateBitReader& a_br,
            const HuffmanTable& a_litLen,
            const HuffmanTable& a_dist,
            std::vector<std::uint8_t>& a_out,
            std::size_t a_maxOutBytes)
        {
            static constexpr std::array<int, 29> kLenBase{
                3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
            };
            static constexpr std::array<int, 29> kLenExtra{
                0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
            };
            static constexpr std::array<int, 30> kDistBase{
                1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                6145, 8193, 12289, 16385, 24577
            };
            static constexpr std::array<int, 30> kDistExtra{
                0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
            };

            while (true) {
                const int sym = a_litLen.Decode(a_br);
                if (sym < 0 || a_br.error) {
                    return false;
                }
                if (sym < 256) {
                    if (a_out.size() >= a_maxOutBytes) {
                        return false;
                    }
                    a_out.push_back(static_cast<std::uint8_t>(sym));
                } else if (sym == 256) {
                    return true;
                } else {
                    const int lenIdx = sym - 257;
                    if (lenIdx < 0 || lenIdx >= 29) {
                        return false;
                    }
                    const int extraBits = kLenExtra[static_cast<std::size_t>(lenIdx)];
                    const int len = kLenBase[static_cast<std::size_t>(lenIdx)] +
                                    (extraBits > 0 ? a_br.ReadBits(extraBits) : 0);
                    if (a_br.error || len <= 0) {
                        return false;
                    }

                    const int distSym = a_dist.Decode(a_br);
                    if (distSym < 0 || distSym >= 30 || a_br.error) {
                        return false;
                    }
                    const int distExtra = kDistExtra[static_cast<std::size_t>(distSym)];
                    const int dist = kDistBase[static_cast<std::size_t>(distSym)] +
                                     (distExtra > 0 ? a_br.ReadBits(distExtra) : 0);
                    if (a_br.error || dist <= 0 || static_cast<std::size_t>(dist) > a_out.size()) {
                        return false;
                    }
                    if (static_cast<std::size_t>(len) > (a_maxOutBytes - a_out.size())) {
                        return false;
                    }

                    const std::size_t distSz = static_cast<std::size_t>(dist);
                    for (int k = 0; k < len; ++k) {
                        const std::uint8_t b = a_out[a_out.size() - distSz];
                        a_out.push_back(b);
                    }
                }
            }
        }

        // ---- SWF Big-Endian BitReader + Little-Endian Byte Reader ----
        struct SwfBitReader
        {
            const std::uint8_t* data{ nullptr };
            std::size_t size{ 0 };
            std::size_t bytePos{ 0 };
            int bitPos{ 0 };
            bool error{ false };

            void Align() noexcept
            {
                if (bitPos != 0) {
                    bitPos = 0;
                    ++bytePos;
                }
            }

            [[nodiscard]] std::uint32_t ReadUBits(int a_count) noexcept
            {
                if (error || a_count < 0 || a_count > 31) {
                    error = true;
                    return 0;
                }
                std::uint32_t v = 0;
                for (int i = 0; i < a_count; ++i) {
                    if (bytePos >= size) {
                        error = true;
                        return 0;
                    }
                    const std::uint32_t bit = static_cast<std::uint32_t>((data[bytePos] >> (7 - bitPos)) & 1);
                    v = (v << 1) | bit;
                    if (++bitPos == 8) {
                        bitPos = 0;
                        ++bytePos;
                    }
                }
                return v;
            }

            [[nodiscard]] std::int32_t ReadSBits(int a_count) noexcept
            {
                if (a_count == 0) {
                    return 0;
                }
                const std::uint32_t u = ReadUBits(a_count);
                if (error) {
                    return 0;
                }
                const std::uint32_t signBit = 1u << (a_count - 1);
                if ((u & signBit) != 0u) {
                    const std::uint32_t ext = u | (~0u << a_count);
                    return static_cast<std::int32_t>(ext);
                }
                return static_cast<std::int32_t>(u);
            }

            [[nodiscard]] std::uint8_t ReadU8() noexcept
            {
                Align();
                if (error || bytePos >= size) {
                    error = true;
                    return 0;
                }
                return data[bytePos++];
            }

            [[nodiscard]] std::uint16_t ReadU16() noexcept
            {
                Align();
                if (error || bytePos + 2 > size) {
                    error = true;
                    return 0;
                }
                const std::uint16_t v = static_cast<std::uint16_t>(data[bytePos]) |
                                        (static_cast<std::uint16_t>(data[bytePos + 1]) << 8);
                bytePos += 2;
                return v;
            }

            [[nodiscard]] std::uint32_t ReadU32() noexcept
            {
                Align();
                if (error || bytePos + 4 > size) {
                    error = true;
                    return 0;
                }
                const std::uint32_t v = static_cast<std::uint32_t>(data[bytePos]) |
                                        (static_cast<std::uint32_t>(data[bytePos + 1]) << 8) |
                                        (static_cast<std::uint32_t>(data[bytePos + 2]) << 16) |
                                        (static_cast<std::uint32_t>(data[bytePos + 3]) << 24);
                bytePos += 4;
                return v;
            }

            void SkipBytes(std::size_t a_count) noexcept
            {
                Align();
                if (error || a_count > (size - std::min(bytePos, size))) {
                    error = true;
                    return;
                }
                bytePos += a_count;
            }

            [[nodiscard]] std::string ReadCString(std::size_t a_maxLen = 512)
            {
                Align();
                std::string out;
                while (bytePos < size && out.size() < a_maxLen) {
                    const char ch = static_cast<char>(data[bytePos++]);
                    if (ch == '\0') {
                        return out;
                    }
                    out.push_back(ch);
                }
                error = true;
                return {};
            }

            void SkipRect() noexcept
            {
                Align();
                const int nBits = static_cast<int>(ReadUBits(5));
                if (error) {
                    return;
                }
                for (int i = 0; i < 4; ++i) {
                    (void)ReadSBits(nBits);
                    if (error) {
                        return;
                    }
                }
                Align();
            }

            [[nodiscard]] SwfMatrix ReadMatrix() noexcept
            {
                Align();
                SwfMatrix m{};
                if (ReadUBits(1) != 0) {
                    const int nBits = static_cast<int>(ReadUBits(5));
                    m.sx = static_cast<float>(ReadSBits(nBits)) / 65536.0f;
                    m.sy = static_cast<float>(ReadSBits(nBits)) / 65536.0f;
                }
                if (ReadUBits(1) != 0) {
                    const int nBits = static_cast<int>(ReadUBits(5));
                    m.r0 = static_cast<float>(ReadSBits(nBits)) / 65536.0f;
                    m.r1 = static_cast<float>(ReadSBits(nBits)) / 65536.0f;
                }
                const int nTransBits = static_cast<int>(ReadUBits(5));
                m.tx = static_cast<float>(ReadSBits(nTransBits));
                m.ty = static_cast<float>(ReadSBits(nTransBits));
                Align();
                return m;
            }
        };

        [[nodiscard]] bool SkipSwfFillStyle(
            SwfBitReader& a_r,
            int a_colorBytes,
            bool& a_outHadBitmap) noexcept
        {
            const std::uint8_t fillType = a_r.ReadU8();
            if (a_r.error) {
                return false;
            }
            if (fillType == 0x00) {
                a_r.SkipBytes(static_cast<std::size_t>(a_colorBytes));
                return !a_r.error;
            }
            if (fillType == 0x10 || fillType == 0x12 || fillType == 0x13) {
                (void)a_r.ReadMatrix();
                const std::uint8_t gradHeader = a_r.ReadU8();
                const int numStops = static_cast<int>(gradHeader & 0x0Fu);
                a_r.SkipBytes(static_cast<std::size_t>(numStops) * (1u + static_cast<std::size_t>(a_colorBytes)));
                if (fillType == 0x13) {
                    a_r.SkipBytes(2);
                }
                return !a_r.error;
            }
            if (fillType == 0x40 || fillType == 0x41 || fillType == 0x42 || fillType == 0x43) {
                a_outHadBitmap = true;
                a_r.SkipBytes(2);
                (void)a_r.ReadMatrix();
                return !a_r.error;
            }
            return false;
        }

        [[nodiscard]] bool SkipFillStyleArray(
            SwfBitReader& a_r,
            std::uint16_t a_tagId,
            bool& a_outHadBitmap) noexcept
        {
            const int colorBytes = (a_tagId >= 32) ? 4 : 3;
            std::uint16_t count = a_r.ReadU8();
            if (count == 0xFFu && a_tagId != 2) {
                count = a_r.ReadU16();
            }
            if (a_r.error || count > 4096u) {
                return false;
            }
            for (std::uint16_t i = 0; i < count; ++i) {
                if (!SkipSwfFillStyle(a_r, colorBytes, a_outHadBitmap)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SkipLineStyleArray(SwfBitReader& a_r, std::uint16_t a_tagId) noexcept
        {
            const int colorBytes = (a_tagId >= 32) ? 4 : 3;
            std::uint16_t count = a_r.ReadU8();
            if (count == 0xFFu && a_tagId != 2) {
                count = a_r.ReadU16();
            }
            if (a_r.error || count > 4096u) {
                return false;
            }
            bool dummyBitmap = false;
            for (std::uint16_t i = 0; i < count; ++i) {
                a_r.SkipBytes(2); // Width
                if (a_tagId == 83) {
                    const std::uint16_t flags = a_r.ReadU16();
                    if ((flags & 0x0030u) == 0x0020u) {
                        a_r.SkipBytes(2); // MiterLimitFactor when JoinStyle == 2
                    }
                    if ((flags & 0x0008u) != 0u) {
                        if (!SkipSwfFillStyle(a_r, 4, dummyBitmap)) {
                            return false;
                        }
                    } else {
                        a_r.SkipBytes(4);
                    }
                } else {
                    a_r.SkipBytes(static_cast<std::size_t>(colorBytes));
                }
                if (a_r.error) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::uint32_t ComputeAdler32(const std::vector<std::uint8_t>& a_data) noexcept
        {
            static constexpr std::uint32_t kModAdler = 65521u;
            std::uint32_t s1 = 1u;
            std::uint32_t s2 = 0u;
            for (const std::uint8_t b : a_data) {
                s1 = (s1 + b) % kModAdler;
                s2 = (s2 + s1) % kModAdler;
            }
            return (s2 << 16) | s1;
        }

        [[nodiscard]] std::size_t FindXmlTagEnd(std::string_view a_xml, std::size_t a_openLt) noexcept
        {
            char activeQuote = '\0';
            for (std::size_t i = a_openLt + 1; i < a_xml.size(); ++i) {
                const char ch = a_xml[i];
                if (activeQuote != '\0') {
                    if (ch == activeQuote) {
                        activeQuote = '\0';
                    }
                } else {
                    if (ch == '"' || ch == '\'') {
                        activeQuote = ch;
                    } else if (ch == '>') {
                        return i;
                    }
                }
            }
            return std::string_view::npos;
        }

        [[nodiscard]] SwfMatrix CombineMatrix(const SwfMatrix& a_p, const SwfMatrix& a_c) noexcept
        {
            SwfMatrix out;
            out.sx = a_p.sx * a_c.sx + a_p.r1 * a_c.r0;
            out.r1 = a_p.sx * a_c.r1 + a_p.r1 * a_c.sy;
            out.tx = a_p.sx * a_c.tx + a_p.r1 * a_c.ty + a_p.tx;
            out.r0 = a_p.r0 * a_c.sx + a_p.sy * a_c.r0;
            out.sy = a_p.r0 * a_c.r1 + a_p.sy * a_c.sy;
            out.ty = a_p.r0 * a_c.tx + a_p.sy * a_c.ty + a_p.ty;
            return out;
        }

        void TransformPoint(const SwfMatrix& a_m, float a_x, float a_y, float& a_outX, float& a_outY) noexcept
        {
            a_outX = a_m.sx * a_x + a_m.r1 * a_y + a_m.tx;
            a_outY = a_m.r0 * a_x + a_m.sy * a_y + a_m.ty;
        }

        [[nodiscard]] std::string StripCommentsXml(std::string_view a_xml)
        {
            std::string out;
            out.reserve(a_xml.size());
            std::size_t i = 0;
            while (i < a_xml.size()) {
                if (i + 4 <= a_xml.size() && a_xml.substr(i, 4) == "<!--") {
                    const auto end = a_xml.find("-->", i + 4);
                    if (end == std::string_view::npos) {
                        break;
                    }
                    i = end + 3;
                    continue;
                }
                out.push_back(a_xml[i]);
                ++i;
            }
            return out;
        }

        void ParseXmlAttributes(
            std::string_view a_tagBody,
            std::unordered_map<std::string, std::string>& a_outAttrs)
        {
            a_outAttrs.clear();
            std::size_t pos = 0;
            while (pos < a_tagBody.size()) {
                while (pos < a_tagBody.size() && std::isspace(static_cast<unsigned char>(a_tagBody[pos]))) {
                    ++pos;
                }
                if (pos >= a_tagBody.size()) {
                    break;
                }
                const std::size_t nameStart = pos;
                while (pos < a_tagBody.size() &&
                       a_tagBody[pos] != '=' &&
                       !std::isspace(static_cast<unsigned char>(a_tagBody[pos]))) {
                    ++pos;
                }
                if (pos == nameStart) {
                    ++pos;
                    continue;
                }
                const std::string attrName = ToLowerAscii(a_tagBody.substr(nameStart, pos - nameStart));
                while (pos < a_tagBody.size() && std::isspace(static_cast<unsigned char>(a_tagBody[pos]))) {
                    ++pos;
                }
                if (pos >= a_tagBody.size() || a_tagBody[pos] != '=') {
                    continue;
                }
                ++pos;
                while (pos < a_tagBody.size() && std::isspace(static_cast<unsigned char>(a_tagBody[pos]))) {
                    ++pos;
                }
                if (pos >= a_tagBody.size()) {
                    break;
                }
                const char quote = a_tagBody[pos];
                if (quote != '"' && quote != '\'') {
                    const std::size_t valStart = pos;
                    while (pos < a_tagBody.size() && !std::isspace(static_cast<unsigned char>(a_tagBody[pos]))) {
                        ++pos;
                    }
                    a_outAttrs[attrName] = std::string(a_tagBody.substr(valStart, pos - valStart));
                    continue;
                }
                ++pos;
                const std::size_t valStart = pos;
                const std::size_t valEnd = a_tagBody.find(quote, pos);
                if (valEnd == std::string_view::npos) {
                    break;
                }
                a_outAttrs[attrName] = std::string(a_tagBody.substr(valStart, valEnd - valStart));
                pos = valEnd + 1;
            }
        }

        [[nodiscard]] bool TryParseHexRgb(std::string_view a_raw, std::uint32_t& a_outRgb)
        {
            std::string s = TrimSv(a_raw);
            if (!s.empty() && s.front() == '#') {
                s.erase(0, 1);
            } else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                s.erase(0, 2);
            }
            if (s.empty() || s.size() > 8) {
                return false;
            }
            for (const unsigned char ch : s) {
                if (!std::isxdigit(ch)) {
                    return false;
                }
            }
            try {
                a_outRgb = static_cast<std::uint32_t>(std::stoul(s, nullptr, 16)) & 0x00FFFFFFu;
                return true;
            } catch (...) {
                return false;
            }
        }

        [[nodiscard]] bool TryParseRgbTriplet(std::string_view a_raw, std::uint32_t& a_outRgb)
        {
            std::vector<int> comps;
            std::size_t pos = 0;
            while (pos < a_raw.size()) {
                const auto comma = a_raw.find(',', pos);
                const std::string part = TrimSv(comma == std::string_view::npos
                    ? a_raw.substr(pos)
                    : a_raw.substr(pos, comma - pos));
                if (!part.empty()) {
                    try {
                        const int v = std::clamp(std::stoi(part), 0, 255);
                        comps.push_back(v);
                    } catch (...) {
                        return false;
                    }
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                pos = comma + 1;
            }
            if (comps.size() != 3) {
                return false;
            }
            a_outRgb = (static_cast<std::uint32_t>(comps[0]) << 16) |
                       (static_cast<std::uint32_t>(comps[1]) << 8) |
                       static_cast<std::uint32_t>(comps[2]);
            return true;
        }
    }

    bool DecompressZlib(
        const std::uint8_t* a_src,
        std::size_t a_srcSize,
        std::vector<std::uint8_t>& a_out,
        std::size_t a_maxOutBytes)
    {
        a_out.clear();
        if (!a_src || a_srcSize < 6 || a_maxOutBytes == 0) {
            return false;
        }

        const std::uint8_t cmf = a_src[0];
        const std::uint8_t flg = a_src[1];
        if ((cmf & 0x0Fu) != 8u || ((cmf >> 4) > 7u)) {
            return false;
        }
        if (((static_cast<std::uint16_t>(cmf) * 256u + flg) % 31u) != 0u) {
            return false;
        }
        if ((flg & 0x20u) != 0u) {
            return false; // Preset dictionary not supported
        }

        DeflateBitReader br{ a_src + 2, a_srcSize - 2, 0, 0, false };
        HuffmanTable fixedLitLen;
        HuffmanTable fixedDist;
        bool fixedBuilt = false;

        int bfinal = 0;
        do {
            bfinal = br.ReadBits(1);
            const int btype = br.ReadBits(2);
            if (br.error) {
                a_out.clear();
                return false;
            }

            if (btype == 0) {
                br.AlignToByte();
                const std::uint16_t len = br.ReadU16LEAligned();
                const std::uint16_t nlen = br.ReadU16LEAligned();
                if (br.error || static_cast<std::uint16_t>(len ^ 0xFFFFu) != nlen) {
                    a_out.clear();
                    return false;
                }
                if (len > (br.size - br.bytePos) || static_cast<std::size_t>(len) > (a_maxOutBytes - a_out.size())) {
                    a_out.clear();
                    return false;
                }
                a_out.insert(a_out.end(), br.data + br.bytePos, br.data + br.bytePos + len);
                br.bytePos += len;
            } else if (btype == 1) {
                if (!fixedBuilt) {
                    BuildFixedTables(fixedLitLen, fixedDist);
                    fixedBuilt = true;
                }
                if (!DecodeHuffmanBlock(br, fixedLitLen, fixedDist, a_out, a_maxOutBytes)) {
                    a_out.clear();
                    return false;
                }
            } else if (btype == 2) {
                const int hlit = br.ReadBits(5) + 257;
                const int hdist = br.ReadBits(5) + 1;
                const int hclen = br.ReadBits(4) + 4;
                if (br.error || hlit > 286 || hdist > 32 || hclen > 19) {
                    a_out.clear();
                    return false;
                }

                static constexpr std::array<int, 19> kOrder{
                    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
                };
                std::array<int, 19> clLens{};
                for (int i = 0; i < hclen; ++i) {
                    clLens[static_cast<std::size_t>(kOrder[static_cast<std::size_t>(i)])] = br.ReadBits(3);
                }
                if (br.error) {
                    a_out.clear();
                    return false;
                }

                HuffmanTable clTable;
                if (!clTable.Build(clLens.data(), 19, false)) {
                    a_out.clear();
                    return false;
                }

                const int totalCodes = hlit + hdist;
                std::vector<int> lengths(static_cast<std::size_t>(totalCodes), 0);
                int idx = 0;
                while (idx < totalCodes) {
                    const int sym = clTable.Decode(br);
                    if (sym < 0 || br.error) {
                        a_out.clear();
                        return false;
                    }
                    if (sym < 16) {
                        lengths[static_cast<std::size_t>(idx++)] = sym;
                    } else if (sym == 16) {
                        if (idx == 0) {
                            a_out.clear();
                            return false;
                        }
                        const int prev = lengths[static_cast<std::size_t>(idx - 1)];
                        const int rep = 3 + br.ReadBits(2);
                        if (br.error || idx + rep > totalCodes) {
                            a_out.clear();
                            return false;
                        }
                        for (int r = 0; r < rep; ++r) {
                            lengths[static_cast<std::size_t>(idx++)] = prev;
                        }
                    } else {
                        const int rep = (sym == 17) ? (3 + br.ReadBits(3)) : (11 + br.ReadBits(7));
                        if (br.error || idx + rep > totalCodes) {
                            a_out.clear();
                            return false;
                        }
                        for (int r = 0; r < rep; ++r) {
                            lengths[static_cast<std::size_t>(idx++)] = 0;
                        }
                    }
                }

                HuffmanTable litTable;
                HuffmanTable distTable;
                if (!litTable.Build(lengths.data(), hlit, false) ||
                    !distTable.Build(lengths.data() + hlit, hdist, true)) {
                    a_out.clear();
                    return false;
                }
                if (!DecodeHuffmanBlock(br, litTable, distTable, a_out, a_maxOutBytes)) {
                    a_out.clear();
                    return false;
                }
            } else {
                a_out.clear();
                return false;
            }
        } while (bfinal == 0);

        br.AlignToByte();
        if (br.bytePos + 4 > br.size) {
            a_out.clear();
            return false;
        }
        const std::uint32_t expectedAdler =
            (static_cast<std::uint32_t>(br.data[br.bytePos]) << 24) |
            (static_cast<std::uint32_t>(br.data[br.bytePos + 1]) << 16) |
            (static_cast<std::uint32_t>(br.data[br.bytePos + 2]) << 8) |
            static_cast<std::uint32_t>(br.data[br.bytePos + 3]);
        if (ComputeAdler32(a_out) != expectedAdler) {
            a_out.clear();
            return false;
        }

        return true;
    }

    std::string ExtractLeadingSorterTag(std::string_view a_rawName)
    {
        if (a_rawName.empty()) {
            return {};
        }

        // Skip leading simple HTML tags (<font...>) and FIS subtitle markers (|+)
        std::string_view cur = a_rawName;
        while (true) {
            const auto first = cur.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return {};
            }
            cur.remove_prefix(first);
            if (cur.front() == '<') {
                const auto close = cur.find('>');
                if (close == std::string_view::npos) {
                    break;
                }
                cur.remove_prefix(close + 1);
                continue;
            }
            if (cur.starts_with("|+")) {
                cur.remove_prefix(2);
                continue;
            }
            break;
        }

        if (cur.empty()) {
            return {};
        }

        std::size_t closePos = std::string_view::npos;
        if (cur.front() == '[') {
            int depth = 0;
            const std::size_t limit = std::min<std::size_t>(cur.size(), 48);
            for (std::size_t i = 0; i < limit; ++i) {
                if (cur[i] == '[') {
                    ++depth;
                } else if (cur[i] == ']') {
                    if (--depth == 0) {
                        closePos = i;
                        break;
                    }
                }
            }
        } else if (cur.front() == '{') {
            int depth = 0;
            const std::size_t limit = std::min<std::size_t>(cur.size(), 48);
            for (std::size_t i = 0; i < limit; ++i) {
                if (cur[i] == '{') {
                    ++depth;
                } else if (cur[i] == '}') {
                    if (--depth == 0) {
                        closePos = i;
                        break;
                    }
                }
            }
        } else if (cur.front() == '|') {
            const auto pos = cur.find('|', 1);
            if (pos != std::string_view::npos && pos < 48) {
                closePos = pos;
            }
        }

        if (closePos == std::string_view::npos) {
            return {};
        }
        return ToLowerAscii(StripBracketWrapper(cur.substr(0, closePos + 1)));
    }

    void SwfIconFile::Clear() noexcept
    {
        _loaded = false;
        _shapes.clear();
        _sprites.clear();
        _symbols.clear();
    }

    bool SwfIconFile::LoadFromFile(const std::filesystem::path& a_path)
    {
        std::vector<std::uint8_t> bytes;
        if (!ReadWholeFileBytes(a_path, kMaxSwfFileBytes, bytes)) {
            Clear();
            return false;
        }
        return LoadFromMemory(bytes.data(), bytes.size());
    }

    bool SwfIconFile::LoadFromMemory(const std::uint8_t* a_data, std::size_t a_size)
    {
        Clear();
        if (!a_data || a_size < 16 || a_size > kMaxSwfFileBytes) {
            return false;
        }

        const bool compressed = (a_data[0] == 'C' && a_data[1] == 'W' && a_data[2] == 'S');
        const bool uncompressed = (a_data[0] == 'F' && a_data[1] == 'W' && a_data[2] == 'S');
        if (!compressed && !uncompressed) {
            return false;
        }

        std::vector<std::uint8_t> body;
        const std::uint8_t* bodyPtr = nullptr;
        std::size_t bodySize = 0;
        if (compressed) {
            if (!DecompressZlib(a_data + 8, a_size - 8, body, kMaxSwfFileBytes)) {
                return false;
            }
            bodyPtr = body.data();
            bodySize = body.size();
        } else {
            bodyPtr = a_data + 8;
            bodySize = a_size - 8;
        }

        if (bodySize < 6) {
            return false;
        }

        SwfBitReader root{ bodyPtr, bodySize, 0, 0, false };
        root.SkipRect();
        (void)root.ReadU16(); // FrameRate
        (void)root.ReadU16(); // FrameCount
        if (root.error) {
            return false;
        }

        int tagGuard = 0;
        while (root.bytePos + 2 <= root.size && tagGuard++ < 32768) {
            const std::uint16_t tc = root.ReadU16();
            if (root.error) {
                Clear();
                return false;
            }
            const std::uint16_t tagId = static_cast<std::uint16_t>(tc >> 6);
            std::uint32_t tagLen = static_cast<std::uint32_t>(tc & 0x3Fu);
            if (tagLen == 0x3Fu) {
                tagLen = root.ReadU32();
                if (root.error) {
                    Clear();
                    return false;
                }
            }
            if (tagLen > (root.size - root.bytePos)) {
                Clear();
                return false;
            }

            const std::uint8_t* pl = root.data + root.bytePos;
            const std::size_t plSize = static_cast<std::size_t>(tagLen);
            root.bytePos += plSize;

            if (tagId == 0) {
                break;
            }

            if (tagId == 2 || tagId == 22 || tagId == 32 || tagId == 83) {
                if (_shapes.size() >= kMaxShapesPerSwf) {
                    continue;
                }
                SwfBitReader sr{ pl, plSize, 0, 0, false };
                const std::uint16_t shapeId = sr.ReadU16();
                sr.SkipRect();
                if (tagId == 83) {
                    sr.SkipRect();
                    (void)sr.ReadU8();
                }
                bool hadBitmapFill = false;
                if (sr.error || !SkipFillStyleArray(sr, tagId, hadBitmapFill) || !SkipLineStyleArray(sr, tagId)) {
                    continue;
                }

                sr.Align();
                int numFillBits = static_cast<int>(sr.ReadUBits(4));
                int numLineBits = static_cast<int>(sr.ReadUBits(4));
                if (sr.error || numFillBits > 16 || numLineBits > 16) {
                    continue;
                }

                SwfShape shape;
                std::int64_t curX = 0;
                std::int64_t curY = 0;
                std::uint32_t fill0 = 0;
                std::uint32_t fill1 = 0;
                int recGuard = 0;

                while (!sr.error && recGuard++ < 65536) {
                    const std::uint32_t typeFlag = sr.ReadUBits(1);
                    if (sr.error) {
                        break;
                    }
                    if (typeFlag == 0u) {
                        const std::uint32_t flags = sr.ReadUBits(5);
                        if (sr.error || flags == 0u) {
                            break;
                        }
                        if ((flags & 0x01u) != 0u) {
                            const int nBits = static_cast<int>(sr.ReadUBits(5));
                            curX = sr.ReadSBits(nBits);
                            curY = sr.ReadSBits(nBits);
                        }
                        if ((flags & 0x02u) != 0u) {
                            fill0 = sr.ReadUBits(numFillBits);
                        }
                        if ((flags & 0x04u) != 0u) {
                            fill1 = sr.ReadUBits(numFillBits);
                        }
                        if ((flags & 0x08u) != 0u) {
                            (void)sr.ReadUBits(numLineBits);
                        }
                        if ((flags & 0x10u) != 0u) {
                            sr.Align();
                            if (!SkipFillStyleArray(sr, tagId, hadBitmapFill) || !SkipLineStyleArray(sr, tagId)) {
                                sr.error = true;
                                break;
                            }
                            sr.Align();
                            numFillBits = static_cast<int>(sr.ReadUBits(4));
                            numLineBits = static_cast<int>(sr.ReadUBits(4));
                            if (numFillBits > 16 || numLineBits > 16) {
                                sr.error = true;
                                break;
                            }
                        }
                    } else {
                        const std::uint32_t straight = sr.ReadUBits(1);
                        const int nBits = static_cast<int>(sr.ReadUBits(4)) + 2;
                        if (sr.error) {
                            break;
                        }
                        const bool keepBoundary = (fill0 > 0u) != (fill1 > 0u);
                        if (straight != 0u) {
                            std::int64_t dx = 0;
                            std::int64_t dy = 0;
                            if (sr.ReadUBits(1) != 0u) {
                                dx = sr.ReadSBits(nBits);
                                dy = sr.ReadSBits(nBits);
                            } else {
                                if (sr.ReadUBits(1) != 0u) {
                                    dy = sr.ReadSBits(nBits);
                                } else {
                                    dx = sr.ReadSBits(nBits);
                                }
                            }
                            if (!sr.error) {
                                const std::int64_t nx = curX + dx;
                                const std::int64_t ny = curY + dy;
                                if (keepBoundary) {
                                    if (shape.edges.size() >= kMaxEdgesPerShape) {
                                        sr.error = true;
                                        break;
                                    }
                                    shape.edges.push_back({
                                        static_cast<float>(curX),
                                        static_cast<float>(curY),
                                        0.0f,
                                        0.0f,
                                        static_cast<float>(nx),
                                        static_cast<float>(ny),
                                        false
                                    });
                                }
                                curX = nx;
                                curY = ny;
                            }
                        } else {
                            const std::int64_t cdx = sr.ReadSBits(nBits);
                            const std::int64_t cdy = sr.ReadSBits(nBits);
                            const std::int64_t adx = sr.ReadSBits(nBits);
                            const std::int64_t ady = sr.ReadSBits(nBits);
                            if (!sr.error) {
                                const std::int64_t cx = curX + cdx;
                                const std::int64_t cy = curY + cdy;
                                const std::int64_t nx = cx + adx;
                                const std::int64_t ny = cy + ady;
                                if (keepBoundary) {
                                    if (shape.edges.size() >= kMaxEdgesPerShape) {
                                        sr.error = true;
                                        break;
                                    }
                                    shape.edges.push_back({
                                        static_cast<float>(curX),
                                        static_cast<float>(curY),
                                        static_cast<float>(cx),
                                        static_cast<float>(cy),
                                        static_cast<float>(nx),
                                        static_cast<float>(ny),
                                        true
                                    });
                                }
                                curX = nx;
                                curY = ny;
                            }
                        }
                    }
                }
                if (recGuard >= 65536) {
                    sr.error = true;
                }

                if (!sr.error && !hadBitmapFill && !shape.edges.empty()) {
                    _shapes[shapeId] = std::move(shape);
                }
            } else if (tagId == 39) {
                SwfBitReader sp{ pl, plSize, 0, 0, false };
                const std::uint16_t spriteId = sp.ReadU16();
                (void)sp.ReadU16(); // FrameCount
                if (sp.error) {
                    continue;
                }
                SwfSprite sprite;
                int subGuard = 0;
                while (sp.bytePos + 2 <= sp.size && subGuard++ < 4096) {
                    const std::uint16_t stc = sp.ReadU16();
                    if (sp.error) {
                        break;
                    }
                    const std::uint16_t stid = static_cast<std::uint16_t>(stc >> 6);
                    std::uint32_t stlen = static_cast<std::uint32_t>(stc & 0x3Fu);
                    if (stlen == 0x3Fu) {
                        stlen = sp.ReadU32();
                        if (sp.error) {
                            break;
                        }
                    }
                    if (stlen > (sp.size - sp.bytePos)) {
                        break;
                    }
                    const std::uint8_t* spl = sp.data + sp.bytePos;
                    sp.bytePos += stlen;
                    if (stid == 0) {
                        break;
                    }
                    if (stid == 26 || stid == 70) {
                        SwfBitReader pr{ spl, stlen, 0, 0, false };
                        const std::uint8_t flags = pr.ReadU8();
                        const std::uint8_t flags2 = (stid == 70) ? pr.ReadU8() : 0u;
                        (void)pr.ReadU16(); // Depth
                        if (stid == 70 &&
                            (((flags2 & 0x04u) != 0u) ||
                             (((flags2 & 0x08u) != 0u) && ((flags & 0x02u) != 0u)))) {
                            (void)pr.ReadCString();
                        }
                        if ((flags & 0x02u) != 0u) {
                            const std::uint16_t charId = pr.ReadU16();
                            SwfMatrix mat{};
                            if ((flags & 0x04u) != 0u) {
                                mat = pr.ReadMatrix();
                            }
                            if (!pr.error && sprite.placements.size() < 256u) {
                                sprite.placements.push_back({ charId, mat });
                            }
                        }
                    }
                }
                if (!sprite.placements.empty()) {
                    _sprites[spriteId] = std::move(sprite);
                }
            } else if (tagId == 76) {
                SwfBitReader sy{ pl, plSize, 0, 0, false };
                const std::uint16_t count = sy.ReadU16();
                if (sy.error || count > 8192u) {
                    continue;
                }
                for (std::uint16_t i = 0; i < count; ++i) {
                    const std::uint16_t charId = sy.ReadU16();
                    const std::string symName = sy.ReadCString();
                    if (sy.error) {
                        break;
                    }
                    const std::string keyNorm = NormalizeSymbolKey(symName);
                    if (!keyNorm.empty()) {
                        _symbols[keyNorm] = charId;
                    }
                    const std::string keyExact = ToLowerAscii(TrimSv(symName));
                    if (!keyExact.empty() && keyExact != keyNorm) {
                        _symbols.try_emplace(keyExact, charId);
                    }
                }
            }
        }

        _loaded = !_symbols.empty() && (!_shapes.empty() || !_sprites.empty());
        return _loaded;
    }

    bool SwfIconFile::HasSymbol(std::string_view a_symbolName) const
    {
        const std::string key = NormalizeSymbolKey(a_symbolName);
        if (!key.empty() && _symbols.contains(key)) {
            return true;
        }
        return _symbols.contains(ToLowerAscii(TrimSv(a_symbolName)));
    }

    void SwfIconFile::CollectCharacterSegments(
        std::uint16_t a_characterId,
        const SwfMatrix& a_parentMatrix,
        int a_depth,
        std::vector<Segment>& a_outSegs) const
    {
        if (a_depth > 4 || a_outSegs.size() >= 32768u) {
            return;
        }

        if (const auto spIt = _sprites.find(a_characterId); spIt != _sprites.end()) {
            for (const auto& place : spIt->second.placements) {
                const SwfMatrix combined = CombineMatrix(a_parentMatrix, place.matrix);
                CollectCharacterSegments(place.characterId, combined, a_depth + 1, a_outSegs);
            }
            return;
        }

        if (const auto shIt = _shapes.find(a_characterId); shIt != _shapes.end()) {
            for (const auto& e : shIt->second.edges) {
                if (a_outSegs.size() >= 32768u) {
                    break;
                }
                if (!e.curved) {
                    float px0 = 0.0f;
                    float py0 = 0.0f;
                    float px1 = 0.0f;
                    float py1 = 0.0f;
                    TransformPoint(a_parentMatrix, e.x0, e.y0, px0, py0);
                    TransformPoint(a_parentMatrix, e.x1, e.y1, px1, py1);
                    a_outSegs.push_back({ px0, py0, px1, py1 });
                } else {
                    static constexpr int kCurveSteps = 6;
                    float prevX = 0.0f;
                    float prevY = 0.0f;
                    TransformPoint(a_parentMatrix, e.x0, e.y0, prevX, prevY);
                    for (int s = 1; s <= kCurveSteps; ++s) {
                        const float t = static_cast<float>(s) / static_cast<float>(kCurveSteps);
                        const float u = 1.0f - t;
                        const float bx = u * u * e.x0 + 2.0f * u * t * e.cx + t * t * e.x1;
                        const float by = u * u * e.y0 + 2.0f * u * t * e.cy + t * t * e.y1;
                        float tx = 0.0f;
                        float ty = 0.0f;
                        TransformPoint(a_parentMatrix, bx, by, tx, ty);
                        a_outSegs.push_back({ prevX, prevY, tx, ty });
                        prevX = tx;
                        prevY = ty;
                    }
                }
            }
        }
    }

    bool SwfIconFile::RasterizeSymbol(
        std::string_view a_symbolName,
        int a_tileSize,
        std::vector<std::uint8_t>& a_outRgba) const
    {
        a_outRgba.clear();
        if (!_loaded || a_tileSize <= 4 || a_tileSize > 256) {
            return false;
        }

        auto symIt = _symbols.find(NormalizeSymbolKey(a_symbolName));
        if (symIt == _symbols.end()) {
            symIt = _symbols.find(ToLowerAscii(TrimSv(a_symbolName)));
        }
        if (symIt == _symbols.end()) {
            return false;
        }

        std::vector<Segment> segs;
        segs.reserve(256);
        CollectCharacterSegments(symIt->second, SwfMatrix{}, 0, segs);
        std::erase_if(segs, [](const Segment& a_s) {
            return !std::isfinite(a_s.x0) || !std::isfinite(a_s.y0) ||
                   !std::isfinite(a_s.x1) || !std::isfinite(a_s.y1);
        });
        if (segs.empty()) {
            return false;
        }

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        for (const auto& s : segs) {
            minX = std::min({ minX, s.x0, s.x1 });
            minY = std::min({ minY, s.y0, s.y1 });
            maxX = std::max({ maxX, s.x0, s.x1 });
            maxY = std::max({ maxY, s.y0, s.y1 });
        }

        const float width = maxX - minX;
        const float height = maxY - minY;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 1e-3f || height <= 1e-3f) {
            return false;
        }

        const float pad = std::max(1.0f, static_cast<float>(a_tileSize) * 0.0625f);
        const float avail = static_cast<float>(a_tileSize) - pad * 2.0f;
        if (avail <= 1.0f) {
            return false;
        }

        const float scale = avail / std::max(width, height);
        const float drawnW = width * scale;
        const float drawnH = height * scale;
        const float offX = (static_cast<float>(a_tileSize) - drawnW) * 0.5f;
        const float offY = (static_cast<float>(a_tileSize) - drawnH) * 0.5f;

        for (auto& s : segs) {
            s.x0 = offX + (s.x0 - minX) * scale;
            s.y0 = offY + (s.y0 - minY) * scale;
            s.x1 = offX + (s.x1 - minX) * scale;
            s.y1 = offY + (s.y1 - minY) * scale;
        }

        static constexpr int kSupersample = 4;
        const int subW = a_tileSize * kSupersample;
        std::vector<std::uint8_t> coverage(static_cast<std::size_t>(subW) * static_cast<std::size_t>(subW), 0);
        std::vector<float> intersections;
        intersections.reserve(64);

        for (int sy = 0; sy < subW; ++sy) {
            const float sampleY = (static_cast<float>(sy) + 0.5f) / static_cast<float>(kSupersample);
            intersections.clear();
            for (const auto& s : segs) {
                const bool crosses =
                    ((s.y0 <= sampleY && s.y1 > sampleY) || (s.y1 <= sampleY && s.y0 > sampleY));
                if (!crosses) {
                    continue;
                }
                const float dy = s.y1 - s.y0;
                if (dy == 0.0f) {
                    continue;
                }
                const float ix = s.x0 + (sampleY - s.y0) * (s.x1 - s.x0) / dy;
                if (std::isfinite(ix)) {
                    intersections.push_back(ix);
                }
            }
            if (intersections.size() < 2) {
                continue;
            }
            std::sort(intersections.begin(), intersections.end());
            std::uint8_t* rowPtr = coverage.data() + static_cast<std::size_t>(sy) * static_cast<std::size_t>(subW);
            for (std::size_t i = 0; i + 1 < intersections.size(); i += 2) {
                const int sx0 = std::clamp(
                    static_cast<int>(std::ceil(intersections[i] * static_cast<float>(kSupersample) - 0.5f)),
                    0, subW);
                const int sx1 = std::clamp(
                    static_cast<int>(std::ceil(intersections[i + 1] * static_cast<float>(kSupersample) - 0.5f)),
                    0, subW);
                for (int sx = sx0; sx < sx1; ++sx) {
                    rowPtr[sx] = 1;
                }
            }
        }

        a_outRgba.resize(static_cast<std::size_t>(a_tileSize) * static_cast<std::size_t>(a_tileSize) * 4u);
        bool anyNonZero = false;
        for (int py = 0; py < a_tileSize; ++py) {
            for (int px = 0; px < a_tileSize; ++px) {
                int hits = 0;
                for (int dy = 0; dy < kSupersample; ++dy) {
                    const std::uint8_t* srcRow = coverage.data() +
                        static_cast<std::size_t>(py * kSupersample + dy) * static_cast<std::size_t>(subW) +
                        static_cast<std::size_t>(px * kSupersample);
                    for (int dx = 0; dx < kSupersample; ++dx) {
                        hits += srcRow[dx];
                    }
                }
                const std::uint8_t alpha = static_cast<std::uint8_t>(
                    std::clamp((hits * 255 + 8) / (kSupersample * kSupersample), 0, 255));
                if (alpha > 0) {
                    anyNonZero = true;
                }
                const std::size_t outIdx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(a_tileSize) +
                                            static_cast<std::size_t>(px)) * 4u;
                a_outRgba[outIdx + 0] = 255u;
                a_outRgba[outIdx + 1] = 255u;
                a_outRgba[outIdx + 2] = 255u;
                a_outRgba[outIdx + 3] = alpha;
            }
        }

        return anyNonZero;
    }

    std::uint32_t AdjustColorBrightnessSaturation(
        std::uint32_t a_rgb,
        int a_brightnessPct,
        int a_saturationPct) noexcept
    {
        const std::uint32_t base = a_rgb & 0x00FFFFFFu;
        if (a_brightnessPct == 100 && a_saturationPct == 100) {
            return base;
        }
        const float briMul = static_cast<float>(std::clamp(a_brightnessPct, 10, 200)) / 100.0f;
        const float satMul = static_cast<float>(std::clamp(a_saturationPct, 0, 200)) / 100.0f;

        const float r = static_cast<float>((base >> 16) & 0xFFu) / 255.0f;
        const float g = static_cast<float>((base >> 8) & 0xFFu) / 255.0f;
        const float b = static_cast<float>(base & 0xFFu) / 255.0f;

        const float mx = std::max({ r, g, b });
        const float mn = std::min({ r, g, b });
        const float d = mx - mn;

        float h = 0.0f;
        if (d > 1e-6f) {
            if (mx == r) {
                h = std::fmod((g - b) / d, 6.0f);
                if (h < 0.0f) {
                    h += 6.0f;
                }
            } else if (mx == g) {
                h = (b - r) / d + 2.0f;
            } else {
                h = (r - g) / d + 4.0f;
            }
        }
        float s = (mx > 1e-6f) ? (d / mx) : 0.0f;
        float v = mx;

        s = std::clamp(s * satMul, 0.0f, 1.0f);
        v = std::clamp(v * briMul, 0.0f, 1.0f);

        const float c = v * s;
        const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
        const float m = v - c;
        float rr = 0.0f;
        float gg = 0.0f;
        float bb = 0.0f;
        if (h < 1.0f) {
            rr = c; gg = x; bb = 0.0f;
        } else if (h < 2.0f) {
            rr = x; gg = c; bb = 0.0f;
        } else if (h < 3.0f) {
            rr = 0.0f; gg = c; bb = x;
        } else if (h < 4.0f) {
            rr = 0.0f; gg = x; bb = c;
        } else if (h < 5.0f) {
            rr = x; gg = 0.0f; bb = c;
        } else {
            rr = c; gg = 0.0f; bb = x;
        }

        const auto outR = static_cast<std::uint32_t>(std::clamp(std::lround((rr + m) * 255.0f), 0l, 255l));
        const auto outG = static_cast<std::uint32_t>(std::clamp(std::lround((gg + m) * 255.0f), 0l, 255l));
        const auto outB = static_cast<std::uint32_t>(std::clamp(std::lround((bb + m) * 255.0f), 0l, 255l));
        return (outR << 16) | (outG << 8) | outB;
    }

    void ApplyIconRadialShading(
        std::vector<std::uint8_t>& a_rgba,
        int a_tileSize) noexcept
    {
        if (a_tileSize <= 0) {
            return;
        }
        const std::size_t expected = static_cast<std::size_t>(a_tileSize) * static_cast<std::size_t>(a_tileSize) * 4u;
        if (a_rgba.size() != expected) {
            return;
        }

        // Matches FallUI_IconLib.swf DefineShape3 radial gradient stops [(0, 255), (255, 170)]
        const float half = static_cast<float>(a_tileSize) * 0.5f;
        static constexpr float kEdgeDrop = 85.0f / 255.0f;
        for (int py = 0; py < a_tileSize; ++py) {
            const float ny = (static_cast<float>(py) + 0.5f - half) / half;
            for (int px = 0; px < a_tileSize; ++px) {
                const std::size_t idx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(a_tileSize) +
                                         static_cast<std::size_t>(px)) * 4u;
                if (a_rgba[idx + 3] == 0u) {
                    continue;
                }
                const float nx = (static_cast<float>(px) + 0.5f - half) / half;
                const float r = std::clamp(std::hypot(nx, ny), 0.0f, 1.0f);
                const float shade = 1.0f - r * kEdgeDrop;
                a_rgba[idx + 0] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<float>(a_rgba[idx + 0]) * shade)), 0, 255));
                a_rgba[idx + 1] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<float>(a_rgba[idx + 1]) * shade)), 0, 255));
                a_rgba[idx + 2] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<float>(a_rgba[idx + 2]) * shade)), 0, 255));
            }
        }
    }

    void FallUIIconRegistry::Clear()
    {
        std::lock_guard lock{ _mutex };
        _initialized = false;
        _interfaceDir.clear();
        _tags.clear();
        _indexedBySection.clear();
        _indexedGlobal.clear();
        _autoRules.clear();
        _autoWildcards.clear();
        _swfCache.clear();
        _loadedAddons.clear();
        _activeColorSet = "Default.xml";
        _iconsEffectEnabled = true;
        _colorCount = 0;
    }

    bool FallUIIconRegistry::IsInitialized() const noexcept
    {
        std::lock_guard lock{ _mutex };
        return _initialized;
    }

    std::size_t FallUIIconRegistry::TagCount() const noexcept
    {
        std::lock_guard lock{ _mutex };
        return _tags.size();
    }

    std::size_t FallUIIconRegistry::ColorCount() const noexcept
    {
        std::lock_guard lock{ _mutex };
        return _colorCount;
    }

    std::size_t FallUIIconRegistry::LoadedSwfCount() const noexcept
    {
        std::lock_guard lock{ _mutex };
        std::size_t count = 0;
        for (const auto& [k, e] : _swfCache) {
            if (e.valid) {
                ++count;
            }
        }
        return count;
    }

    std::vector<std::string> FallUIIconRegistry::LoadedAddonNames() const
    {
        std::lock_guard lock{ _mutex };
        return _loadedAddons;
    }

    std::string FallUIIconRegistry::ActiveColorSetName() const
    {
        std::lock_guard lock{ _mutex };
        return _activeColorSet;
    }

    bool FallUIIconRegistry::IconsEffectEnabled() const noexcept
    {
        std::lock_guard lock{ _mutex };
        return _iconsEffectEnabled;
    }

    bool FallUIIconRegistry::ResolveColorName(
        const std::unordered_map<std::string, ColorDefinition>& a_colors,
        std::string_view a_colorName,
        std::uint32_t& a_outRgb,
        int a_depth) const
    {
        if (a_depth > 8 || a_colorName.empty()) {
            return false;
        }
        std::size_t pos = 0;
        while (pos <= a_colorName.size()) {
            const auto comma = a_colorName.find(',', pos);
            const std::string token = ToLowerAscii(TrimSv(
                comma == std::string_view::npos
                    ? a_colorName.substr(pos)
                    : a_colorName.substr(pos, comma - pos)));
            if (!token.empty()) {
                const auto it = a_colors.find(token);
                if (it != a_colors.end()) {
                    if (it->second.isAlias) {
                        if (ResolveColorName(a_colors, it->second.aliasTarget, a_outRgb, a_depth + 1)) {
                            return true;
                        }
                    } else {
                        a_outRgb = it->second.rgb;
                        return true;
                    }
                } else if (token == "white") {
                    a_outRgb = 0xFFFFFFu;
                    return true;
                } else if (token == "black") {
                    a_outRgb = 0x000000u;
                    return true;
                } else if (TryParseHexRgb(token, a_outRgb)) {
                    return true;
                }
            }
            if (comma == std::string_view::npos) {
                break;
            }
            pos = comma + 1;
        }
        return false;
    }

    void FallUIIconRegistry::ParseColorXml(
        const std::string& a_cleanedXml,
        std::unordered_map<std::string, ColorDefinition>& a_ioColors) const
    {
        std::unordered_map<std::string, std::string> attrs;
        std::size_t pos = 0;
        while (pos < a_cleanedXml.size()) {
            const auto lt = a_cleanedXml.find('<', pos);
            if (lt == std::string::npos) {
                break;
            }
            const auto gt = FindXmlTagEnd(a_cleanedXml, lt);
            if (gt == std::string::npos) {
                break;
            }
            pos = gt + 1;

            std::string_view tagView(a_cleanedXml.data() + lt + 1, gt - lt - 1);
            if (tagView.empty() || tagView.front() == '/' || tagView.front() == '?') {
                continue;
            }
            if (!tagView.empty() && tagView.back() == '/') {
                tagView.remove_suffix(1);
            }

            std::size_t sp = 0;
            while (sp < tagView.size() && !std::isspace(static_cast<unsigned char>(tagView[sp]))) {
                ++sp;
            }
            const std::string tagName = ToLowerAscii(tagView.substr(0, sp));
            if (tagName != "color") {
                continue;
            }

            ParseXmlAttributes(tagView.substr(sp), attrs);
            const auto nameIt = attrs.find("name");
            if (nameIt == attrs.end()) {
                continue;
            }
            const std::string colorKey = ToLowerAscii(TrimSv(nameIt->second));
            if (colorKey.empty()) {
                continue;
            }

            ColorDefinition def{};
            if (const auto hexIt = attrs.find("hex"); hexIt != attrs.end() && TryParseHexRgb(hexIt->second, def.rgb)) {
                a_ioColors[colorKey] = std::move(def);
            } else if (const auto rgbIt = attrs.find("rgb"); rgbIt != attrs.end() && TryParseRgbTriplet(rgbIt->second, def.rgb)) {
                a_ioColors[colorKey] = std::move(def);
            } else if (const auto alIt = attrs.find("alias"); alIt != attrs.end()) {
                def.isAlias = true;
                def.aliasTarget = ToLowerAscii(TrimSv(alIt->second));
                if (!def.aliasTarget.empty()) {
                    a_ioColors[colorKey] = std::move(def);
                }
            }
        }
    }

    std::size_t FallUIIconRegistry::ParseTagXml(
        const std::string& a_rawXml,
        const std::string& a_defaultSwfRelPath,
        bool a_verifySwfOnDisk,
        std::unordered_map<std::string, ColorDefinition>& a_ioColors)
    {
        const std::string cleaned = StripCommentsXml(a_rawXml);
        ParseColorXml(cleaned, a_ioColors);

        std::unordered_map<std::string, std::string> attrs;
        std::string currentSwf = NormalizeSlashStr(a_defaultSwfRelPath);
        std::size_t acceptedTags = 0;
        std::unordered_map<std::string, bool> swfExistsCache;
        auto doesSwfExist = [&](const std::string& a_rel) -> bool {
            if (!a_verifySwfOnDisk || _interfaceDir.empty()) {
                return true;
            }
            const std::string k = ToLowerAscii(a_rel);
            if (const auto it = swfExistsCache.find(k); it != swfExistsCache.end()) {
                return it->second;
            }
            std::error_code ec;
            const auto u8v = std::u8string_view(
                reinterpret_cast<const char8_t*>(a_rel.data()),
                a_rel.size());
            const std::filesystem::path full = _interfaceDir / std::filesystem::path(u8v);
            const bool exists = std::filesystem::exists(full, ec) && std::filesystem::is_regular_file(full, ec);
            swfExistsCache[k] = exists;
            return exists;
        };

        std::size_t pos = 0;
        while (pos < cleaned.size()) {
            const auto lt = cleaned.find('<', pos);
            if (lt == std::string::npos) {
                break;
            }
            const auto gt = FindXmlTagEnd(cleaned, lt);
            if (gt == std::string::npos) {
                break;
            }
            pos = gt + 1;

            std::string_view tagView(cleaned.data() + lt + 1, gt - lt - 1);
            if (tagView.empty() || tagView.front() == '/' || tagView.front() == '?') {
                continue;
            }
            if (!tagView.empty() && tagView.back() == '/') {
                tagView.remove_suffix(1);
            }

            std::size_t sp = 0;
            while (sp < tagView.size() && !std::isspace(static_cast<unsigned char>(tagView[sp]))) {
                ++sp;
            }
            const std::string tagName = ToLowerAscii(tagView.substr(0, sp));
            if (tagName == "tags") {
                ParseXmlAttributes(tagView.substr(sp), attrs);
                if (const auto libIt = attrs.find("iconlibraryfile"); libIt != attrs.end()) {
                    std::string p = NormalizeSlashStr(TrimSv(libIt->second));
                    if (!p.empty()) {
                        currentSwf = std::move(p);
                    }
                }
                continue;
            }
            if (tagName != "tag") {
                continue;
            }

            ParseXmlAttributes(tagView.substr(sp), attrs);
            const auto kwIt = attrs.find("keyword");
            const auto icIt = attrs.find("icon");
            if (kwIt == attrs.end() || icIt == attrs.end()) {
                continue;
            }

            const std::string kwKey = ToLowerAscii(StripBracketWrapper(kwIt->second));
            const std::string iconSym = NormalizeSymbolKey(icIt->second);
            if (kwKey.empty() || iconSym.empty() || currentSwf.empty()) {
                continue;
            }

            SwfCandidate cand;
            cand.swfRelPath = currentSwf;
            cand.iconSymbol = iconSym;
            if (const auto subIt = attrs.find("subicon"); subIt != attrs.end()) {
                cand.subIconSymbol = NormalizeSymbolKey(subIt->second);
            }
            if (const auto colIt = attrs.find("colorname"); colIt != attrs.end()) {
                const std::string_view rawColor = colIt->second;
                if (cand.subIconSymbol.empty()) {
                    cand.hasColor = ResolveColorName(a_ioColors, rawColor, cand.rgbColor);
                } else {
                    const auto commaPos = rawColor.find(',');
                    const std::string primaryTok = TrimSv(
                        commaPos == std::string_view::npos ? rawColor : rawColor.substr(0, commaPos));
                    if (!primaryTok.empty()) {
                        cand.hasColor = ResolveColorName(a_ioColors, primaryTok, cand.rgbColor);
                    }
                    if (commaPos != std::string_view::npos) {
                        const std::string subTok = TrimSv(rawColor.substr(commaPos + 1));
                        if (!subTok.empty()) {
                            cand.hasSubColor = ResolveColorName(a_ioColors, subTok, cand.subRgbColor);
                        }
                    }
                }
            }

            if (a_verifySwfOnDisk && !doesSwfExist(cand.swfRelPath)) {
                continue; // Do not accept/clobber when addon SWF is missing/archived
            }
            const auto existingIt = _tags.find(kwKey);
            if (existingIt != _tags.end()) {
                existingIt->second.fallback = std::move(existingIt->second.primary);
                existingIt->second.hasFallback = true;
                existingIt->second.primary = std::move(cand);
            } else {
                TagDefinition def;
                def.primary = std::move(cand);
                _tags[kwKey] = std::move(def);
            }
            ++acceptedTags;
        }
        return acceptedTags;
    }

    void FallUIIconRegistry::ParseIndexedTagsFile(const std::filesystem::path& a_path)
    {
        std::string text;
        if (!ReadWholeFileText(a_path, kMaxXmlFileBytes, text)) {
            return;
        }

        std::string currentSection;
        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::string trimmed = TrimSv(line);
            if (trimmed.empty() || trimmed.front() == ';') {
                continue;
            }

            const auto delimPos = trimmed.find('\x1f');
            if (delimPos == std::string::npos) {
                currentSection = ToLowerAscii(StripBracketWrapper(trimmed));
                continue;
            }

            const std::string tagKw = ExtractLeadingSorterTag(std::string_view(trimmed).substr(0, delimPos));
            if (tagKw.empty()) {
                continue;
            }

            std::size_t pos = delimPos + 1;
            while (pos < trimmed.size()) {
                const auto next = trimmed.find('\x1f', pos);
                const std::string itemLower = ToLowerAscii(TrimSv(
                    next == std::string::npos
                        ? std::string_view(trimmed).substr(pos)
                        : std::string_view(trimmed).substr(pos, next - pos)));
                if (!itemLower.empty()) {
                    if (!currentSection.empty()) {
                        _indexedBySection.try_emplace(currentSection + "|" + itemLower, tagKw);
                    }
                    _indexedGlobal.try_emplace(itemLower, tagKw);
                }
                if (next == std::string::npos) {
                    break;
                }
                pos = next + 1;
            }
        }
    }

    void FallUIIconRegistry::ParseAutoTagsIni(const std::filesystem::path& a_path)
    {
        std::string text;
        if (!ReadWholeFileText(a_path, kMaxXmlFileBytes, text)) {
            return;
        }

        std::string currentSection;
        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line)) {
            const std::string trimmed = TrimSv(line);
            if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') {
                continue;
            }
            if (trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = ToLowerAscii(TrimSv(std::string_view(trimmed).substr(1, trimmed.size() - 2)));
                continue;
            }
            const auto eq = trimmed.find('=');
            if (eq == std::string::npos || currentSection.empty()) {
                continue;
            }
            const std::string needle = ToLowerAscii(TrimSv(std::string_view(trimmed).substr(0, eq)));
            const std::string tagKw = ToLowerAscii(StripBracketWrapper(std::string_view(trimmed).substr(eq + 1)));
            if (!needle.empty() && !tagKw.empty()) {
                if (needle == "*") {
                    _autoWildcards.try_emplace(currentSection, tagKw);
                } else {
                    _autoRules[currentSection].push_back({ needle, tagKw });
                }
            }
        }
    }

    bool FallUIIconRegistry::InitializeFromInterfaceDir(const std::filesystem::path& a_interfaceDir)
    {
        std::lock_guard lock{ _mutex };
        _initialized = true;
        _interfaceDir = a_interfaceDir;
        _tags.clear();
        _indexedBySection.clear();
        _indexedGlobal.clear();
        _autoRules.clear();
        _autoWildcards.clear();
        _swfCache.clear();
        _loadedAddons.clear();
        _activeColorSet = "Default.xml";
        _colorCount = 0;

        auto readIniValues = [&](const std::filesystem::path& a_iniPath) {
            std::unordered_map<std::string, std::string> kv;
            std::string iniText;
            if (!ReadWholeFileText(a_iniPath, 256u * 1024u, iniText)) {
                return kv;
            }
            std::istringstream iss(iniText);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                const std::string trimmed = TrimSv(line);
                if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' || trimmed.front() == '[') {
                    continue;
                }
                const auto eq = trimmed.find('=');
                if (eq == std::string::npos) {
                    continue;
                }
                std::string k = ToLowerAscii(TrimSv(std::string_view(trimmed).substr(0, eq)));
                std::string v = TrimSv(std::string_view(trimmed).substr(eq + 1));
                if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
                    v = TrimSv(std::string_view(v).substr(1, v.size() - 2));
                }
                if (!k.empty()) {
                    kv[std::move(k)] = std::move(v);
                }
            }
            return kv;
        };

        std::error_code mcmEc;
        std::filesystem::path mcmSettingsDir = a_interfaceDir.parent_path() / "MCM" / "Settings";
        if (!std::filesystem::exists(mcmSettingsDir, mcmEc) || !std::filesystem::is_directory(mcmSettingsDir, mcmEc)) {
            const auto altDir = a_interfaceDir / "MCM" / "Settings";
            if (std::filesystem::exists(altDir, mcmEc) && std::filesystem::is_directory(altDir, mcmEc)) {
                mcmSettingsDir = altDir;
            }
        }

        const auto iconLibIni = readIniValues(mcmSettingsDir / "FallUIIconLibrary.ini");
        const auto falluiIni = readIniValues(mcmSettingsDir / "FallUI.ini");

        std::string colorSetFile;
        if (const auto it = iconLibIni.find("siconscolorset"); it != iconLibIni.end()) {
            const auto fn = PathToUtf8(std::filesystem::path(it->second).filename());
            if (!fn.empty() && ToLowerAscii(fn) != "none" && ToLowerAscii(fn) != "default.xml") {
                colorSetFile = fn;
            }
        }
        auto parseIniBool = [](const std::unordered_map<std::string, std::string>& a_kv,
                               const std::string& a_key,
                               bool a_default) -> bool {
            const auto it = a_kv.find(a_key);
            if (it == a_kv.end() || it->second.empty()) {
                return a_default;
            }
            const std::string val = ToLowerAscii(TrimSv(it->second));
            if (val == "0" || val == "false" || val == "no" || val == "off") {
                return false;
            }
            if (val == "1" || val == "true" || val == "yes" || val == "on") {
                return true;
            }
            return a_default;
        };
        const bool iconsColored = parseIniBool(iconLibIni, "biconscolored", true);
        _iconsEffectEnabled = parseIniBool(iconLibIni, "biconseffect", true);

        int brightnessPct = 100;
        if (const auto it = iconLibIni.find("iiconsbrightness"); it != iconLibIni.end()) {
            try {
                brightnessPct = std::clamp(std::stoi(it->second), 10, 200);
            } catch (...) {
                brightnessPct = 100;
            }
        }
        int saturationPct = 100;
        if (const auto it = iconLibIni.find("iiconssaturation"); it != iconLibIni.end()) {
            try {
                saturationPct = std::clamp(std::stoi(it->second), 0, 200);
            } catch (...) {
                saturationPct = 100;
            }
        }

        std::string tagConfigFile = "FIS (FallUI Item Sorter).xml";
        if (const auto it = falluiIni.find("sitemsortertagconfig"); it != falluiIni.end()) {
            const auto fn = PathToUtf8(std::filesystem::path(it->second).filename());
            const std::string fnLower = ToLowerAscii(fn);
            if (!fn.empty() && fnLower != "none" && fnLower != "auto-detect.xml") {
                std::error_code cfgEc;
                const auto candidatePath = a_interfaceDir / "ItemSorter" / fn;
                if (std::filesystem::exists(candidatePath, cfgEc) && std::filesystem::is_regular_file(candidatePath, cfgEc)) {
                    tagConfigFile = fn;
                }
            }
        }

        std::unordered_map<std::string, ColorDefinition> colors;
        std::string xmlBuf;

        // 1. Match FallUI Inventory TagConfiguration.loadTagConfig 1:1:
        // Load ONLY the selected ColorSet XML (without preloading Default.xml when a custom ColorSet is active)
        if (iconsColored) {
            std::string colorSetXmlBuf;
            if (!colorSetFile.empty() &&
                ReadWholeFileText(a_interfaceDir / "ItemSorter" / "ColorSets" / colorSetFile, kMaxXmlFileBytes, colorSetXmlBuf)) {
                ParseColorXml(StripCommentsXml(colorSetXmlBuf), colors);
                _activeColorSet = colorSetFile;
            } else if (ReadWholeFileText(a_interfaceDir / "ItemSorter" / "ColorSets" / "Default.xml", kMaxXmlFileBytes, xmlBuf)) {
                ParseColorXml(StripCommentsXml(xmlBuf), colors);
            }
        } else if (!colorSetFile.empty()) {
            _activeColorSet = colorSetFile;
        }

        // 2. Primary FIS tag configuration
        if (ReadWholeFileText(a_interfaceDir / "ItemSorter" / tagConfigFile, kMaxXmlFileBytes, xmlBuf)) {
            ParseTagXml(xmlBuf, "FallUI_IconLib.swf", false, colors);
        }

        // 3. Sorted Addon XMLs (e.g. Icon Expansion Pack XeniuIconEditLib.xml)
        std::error_code ec;
        const auto addonDir = a_interfaceDir / "ItemSorter" / "FIS" / "TagConfiguration.Addons";
        if (std::filesystem::exists(addonDir, ec) && std::filesystem::is_directory(addonDir, ec)) {
            std::vector<std::filesystem::path> addonFiles;
            std::filesystem::directory_iterator it(addonDir, ec);
            for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                std::error_code entryEc;
                if (!it->is_regular_file(entryEc) || entryEc) {
                    continue;
                }
                if (ToLowerAscii(PathToUtf8(it->path().extension())) == ".xml") {
                    addonFiles.push_back(it->path());
                    if (addonFiles.size() >= kMaxAddons) {
                        break;
                    }
                }
            }
            std::sort(addonFiles.begin(), addonFiles.end());
            for (const auto& xmlPath : addonFiles) {
                if (ReadWholeFileText(xmlPath, kMaxXmlFileBytes, xmlBuf)) {
                    if (ParseTagXml(xmlBuf, "FallUI_IconLib.swf", true, colors) > 0) {
                        const std::string addonStem = PathToUtf8(xmlPath.stem());
                        if (!addonStem.empty()) {
                            _loadedAddons.push_back(addonStem);
                        }
                    }
                }
            }
        }

        if (!iconsColored) {
            for (auto& [k, def] : _tags) {
                def.primary.hasColor = false;
                def.primary.rgbColor = 0xFFFFFFu;
                def.primary.hasSubColor = false;
                def.primary.subRgbColor = 0xFFFFFFu;
                if (def.hasFallback) {
                    def.fallback.hasColor = false;
                    def.fallback.rgbColor = 0xFFFFFFu;
                    def.fallback.hasSubColor = false;
                    def.fallback.subRgbColor = 0xFFFFFFu;
                }
            }
        } else if (brightnessPct != 100 || saturationPct != 100) {
            for (auto& [k, def] : _tags) {
                if (def.primary.hasColor) {
                    def.primary.rgbColor = AdjustColorBrightnessSaturation(def.primary.rgbColor, brightnessPct, saturationPct);
                }
                if (def.primary.hasSubColor) {
                    def.primary.subRgbColor = AdjustColorBrightnessSaturation(def.primary.subRgbColor, brightnessPct, saturationPct);
                }
                if (def.hasFallback) {
                    if (def.fallback.hasColor) {
                        def.fallback.rgbColor = AdjustColorBrightnessSaturation(def.fallback.rgbColor, brightnessPct, saturationPct);
                    }
                    if (def.fallback.hasSubColor) {
                        def.fallback.subRgbColor = AdjustColorBrightnessSaturation(def.fallback.subRgbColor, brightnessPct, saturationPct);
                    }
                }
            }
        }

        _colorCount = iconsColored ? colors.size() : 0;

        // 4. IndexedTags & AutoTags fallback matching dictionaries (_en first, then any extra locale files)
        {
            const std::filesystem::path autoTagsDir = a_interfaceDir / "ItemSorter" / "FIS" / "AutoTags";
            ParseIndexedTagsFile(autoTagsDir / "IndexedTags_en.txt");
            ParseAutoTagsIni(autoTagsDir / "AutoTags_en.ini");

            std::error_code ec;
            if (std::filesystem::exists(autoTagsDir, ec) && std::filesystem::is_directory(autoTagsDir, ec)) {
                std::vector<std::filesystem::path> indexedExtras;
                std::vector<std::filesystem::path> autoExtras;
                std::filesystem::directory_iterator it(autoTagsDir, ec);
                for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                    std::error_code entryEc;
                    if (!it->is_regular_file(entryEc) || entryEc) {
                        continue;
                    }
                    const std::string fnameLower = ToLowerAscii(PathToUtf8(it->path().filename()));
                    if (fnameLower.starts_with("indexedtags_") && fnameLower.ends_with(".txt") &&
                        fnameLower != "indexedtags_en.txt") {
                        indexedExtras.push_back(it->path());
                    } else if (fnameLower.starts_with("autotags_") && fnameLower.ends_with(".ini") &&
                               fnameLower != "autotags_en.ini") {
                        autoExtras.push_back(it->path());
                    }
                }
                std::sort(indexedExtras.begin(), indexedExtras.end());
                std::sort(autoExtras.begin(), autoExtras.end());
                for (const auto& p : indexedExtras) {
                    ParseIndexedTagsFile(p);
                }
                for (const auto& p : autoExtras) {
                    ParseAutoTagsIni(p);
                }
            }
        }

        return !_tags.empty();
    }

    bool FallUIIconRegistry::ResolveIcon(
        std::string_view a_rawName,
        TargetInfo::ItemKind a_kind,
        ResolvedFallUIIcon& a_out) const
    {
        a_out = {};
        switch (a_kind) {
        case TargetInfo::ItemKind::None:
        case TargetInfo::ItemKind::Container:
        case TargetInfo::ItemKind::Door:
        case TargetInfo::ItemKind::Furniture:
        case TargetInfo::ItemKind::Terminal:
        case TargetInfo::ItemKind::Npc:
            return false;
        default:
            break;
        }

        std::lock_guard lock{ _mutex };
        if (_tags.empty()) {
            return false;
        }

        auto tryKeyword = [&](std::string_view a_kw) -> bool {
            if (a_kw.empty()) {
                return false;
            }
            const auto it = _tags.find(std::string(a_kw));
            if (it == _tags.end() || it->second.primary.iconSymbol.empty() || it->second.primary.swfRelPath.empty()) {
                return false;
            }
            a_out.swfRelPath = it->second.primary.swfRelPath;
            a_out.symbolName = it->second.primary.iconSymbol;
            a_out.rgbColor = it->second.primary.rgbColor;
            a_out.hasColor = it->second.primary.hasColor;
            a_out.subSymbolName = it->second.primary.subIconSymbol;
            a_out.subRgbColor = it->second.primary.subRgbColor;
            a_out.hasSubColor = it->second.primary.hasSubColor;
            if (it->second.hasFallback &&
                !it->second.fallback.iconSymbol.empty() &&
                !it->second.fallback.swfRelPath.empty()) {
                a_out.hasFallback = true;
                a_out.fallbackSwfRelPath = it->second.fallback.swfRelPath;
                a_out.fallbackSymbolName = it->second.fallback.iconSymbol;
                a_out.fallbackRgbColor = it->second.fallback.rgbColor;
                a_out.fallbackHasColor = it->second.fallback.hasColor;
                a_out.fallbackSubSymbolName = it->second.fallback.subIconSymbol;
                a_out.fallbackSubRgbColor = it->second.fallback.subRgbColor;
                a_out.fallbackHasSubColor = it->second.fallback.hasSubColor;
            } else {
                a_out.hasFallback = false;
                a_out.fallbackSubSymbolName.clear();
                a_out.fallbackSubRgbColor = 0xFFFFFFu;
                a_out.fallbackHasSubColor = false;
            }
            return true;
        };

        // 1. Explicit [Tag] prefix in raw item name
        const std::string leadingTag = ExtractLeadingSorterTag(a_rawName);
        if (tryKeyword(leadingTag)) {
            return true;
        }

        // 2. Clean display name lookup against IndexedTags + AutoTags
        const std::string cleanLower = ToLowerAscii(
            TargetInfo::CleanDisplayName(std::string(a_rawName), true));

        std::array<std::string_view, 3> sections{};
        std::size_t secCount = 0;
        switch (a_kind) {
        case TargetInfo::ItemKind::Weapon:
            sections = { "weap", "", "" };
            secCount = 1;
            break;
        case TargetInfo::ItemKind::Armor:
            sections = { "armo", "", "" };
            secCount = 1;
            break;
        case TargetInfo::ItemKind::Aid:
            sections = { "alch", "", "" };
            secCount = 1;
            break;
        case TargetInfo::ItemKind::Ammo:
            sections = { "ammo", "", "" };
            secCount = 1;
            break;
        case TargetInfo::ItemKind::MiscJunk:
            sections = { "junk", "misc", "mods" };
            secCount = 3;
            break;
        case TargetInfo::ItemKind::Key:
            sections = { "keym", "misc", "" };
            secCount = 2;
            break;
        case TargetInfo::ItemKind::BookNote:
            sections = { "book", "note", "" };
            secCount = 2;
            break;
        case TargetInfo::ItemKind::Holotape:
            sections = { "note", "holo", "" };
            secCount = 2;
            break;
        default:
            break;
        }

        if (!cleanLower.empty()) {
            for (std::size_t s = 0; s < secCount; ++s) {
                const std::string secKey = std::string(sections[s]) + "|" + cleanLower;
                if (const auto it = _indexedBySection.find(secKey); it != _indexedBySection.end()) {
                    if (tryKeyword(it->second)) {
                        return true;
                    }
                }
            }
            if (const auto it = _indexedGlobal.find(cleanLower); it != _indexedGlobal.end()) {
                if (tryKeyword(it->second)) {
                    return true;
                }
            }

            for (std::size_t s = 0; s < secCount; ++s) {
                if (const auto rIt = _autoRules.find(std::string(sections[s])); rIt != _autoRules.end()) {
                    for (const auto& rule : rIt->second) {
                        if (cleanLower.find(rule.needleLower) != std::string::npos) {
                            if (tryKeyword(rule.tagLower)) {
                                return true;
                            }
                        }
                    }
                }
            }
            if (const auto fixIt = _autoRules.find("fixes"); fixIt != _autoRules.end()) {
                for (const auto& rule : fixIt->second) {
                    if (cleanLower.find(rule.needleLower) != std::string::npos) {
                        if (tryKeyword(rule.tagLower)) {
                            return true;
                        }
                    }
                }
            }

            for (std::size_t s = 0; s < secCount; ++s) {
                if (const auto wIt = _autoWildcards.find(std::string(sections[s])); wIt != _autoWildcards.end()) {
                    if (tryKeyword(wIt->second)) {
                        return true;
                    }
                }
            }
        }

        // 3. Default category FIS tag fallback
        std::string_view fallbackKw;
        switch (a_kind) {
        case TargetInfo::ItemKind::Weapon: fallbackKw = "pistol"; break;
        case TargetInfo::ItemKind::Armor: fallbackKw = "armor"; break;
        case TargetInfo::ItemKind::Aid: fallbackKw = "aid"; break;
        case TargetInfo::ItemKind::Ammo: fallbackKw = "ammo"; break;
        case TargetInfo::ItemKind::MiscJunk: fallbackKw = "scrap"; break;
        case TargetInfo::ItemKind::Key: fallbackKw = "key"; break;
        case TargetInfo::ItemKind::BookNote: fallbackKw = "note"; break;
        case TargetInfo::ItemKind::Holotape: fallbackKw = "holotape"; break;
        default: break;
        }
        return tryKeyword(fallbackKw);
    }

    bool FallUIIconRegistry::RasterizeResolvedIcon(
        const ResolvedFallUIIcon& a_resolved,
        int a_tileSize,
        std::vector<std::uint8_t>& a_outRgba,
        bool* a_outUsedFallback,
        bool a_bakeSubColors,
        bool* a_outColorsBaked)
    {
        if (a_outUsedFallback) {
            *a_outUsedFallback = false;
        }
        if (a_outColorsBaked) {
            *a_outColorsBaked = false;
        }
        if (a_resolved.swfRelPath.empty() || a_resolved.symbolName.empty()) {
            return false;
        }

        std::lock_guard lock{ _mutex };
        auto rasterizeOne = [&](std::string_view a_relPath, std::string_view a_sym, int a_size, std::vector<std::uint8_t>& a_dst) -> bool {
            if (a_relPath.empty() || a_sym.empty()) {
                return false;
            }
            const std::string cacheKey = ToLowerAscii(NormalizeSlashStr(std::string(a_relPath)));
            auto& entry = _swfCache[cacheKey];
            if (!entry.attempted) {
                entry.attempted = true;
                std::filesystem::path fullPath = _interfaceDir;
                const auto u8view = std::u8string_view(
                    reinterpret_cast<const char8_t*>(a_relPath.data()),
                    a_relPath.size());
                fullPath /= std::filesystem::path(u8view);
                entry.valid = entry.swf.LoadFromFile(fullPath);
            }
            if (!entry.valid) {
                return false;
            }
            if (!entry.swf.RasterizeSymbol(a_sym, a_size, a_dst)) {
                return false;
            }
            if (_iconsEffectEnabled) {
                ApplyIconRadialShading(a_dst, a_size);
            }
            return true;
        };

        auto compositeSubicon = [&](
            std::string_view a_relPath,
            std::string_view a_subSym,
            std::uint32_t a_mainRgb,
            bool a_hasMainColor,
            std::uint32_t a_subRgb,
            bool a_hasSubColor) -> bool
        {
            if (a_subSym.empty() || a_tileSize < 8) {
                return false;
            }
            const int subSize = std::clamp((a_tileSize * 9) / 16, 6, a_tileSize);
            std::vector<std::uint8_t> subRgba;
            bool gotSub = rasterizeOne(a_relPath, a_subSym, subSize, subRgba);
            if (!gotSub && ToLowerAscii(NormalizeSlashStr(std::string(a_relPath))) != "fallui_iconlib.swf") {
                gotSub = rasterizeOne("FallUI_IconLib.swf", a_subSym, subSize, subRgba);
            }
            if (!gotSub) {
                return false;
            }
            if (subRgba.size() != static_cast<std::size_t>(subSize) * static_cast<std::size_t>(subSize) * 4u) {
                return false;
            }

            const bool bakeColors = a_bakeSubColors && (a_hasMainColor || a_hasSubColor);
            const std::uint32_t effMainRgb = a_hasMainColor ? a_mainRgb : 0xFFFFFFu;
            const std::uint32_t effSubRgb = a_hasSubColor ? a_subRgb : effMainRgb;
            const std::uint8_t mainR = static_cast<std::uint8_t>((effMainRgb >> 16) & 0xFFu);
            const std::uint8_t mainG = static_cast<std::uint8_t>((effMainRgb >> 8) & 0xFFu);
            const std::uint8_t mainB = static_cast<std::uint8_t>(effMainRgb & 0xFFu);
            const std::uint8_t subR = static_cast<std::uint8_t>((effSubRgb >> 16) & 0xFFu);
            const std::uint8_t subG = static_cast<std::uint8_t>((effSubRgb >> 8) & 0xFFu);
            const std::uint8_t subB = static_cast<std::uint8_t>(effSubRgb & 0xFFu);

            if (bakeColors) {
                for (std::size_t i = 0; i + 3 < a_outRgba.size(); i += 4) {
                    a_outRgba[i + 0] = static_cast<std::uint8_t>((static_cast<unsigned>(a_outRgba[i + 0]) * mainR + 127u) / 255u);
                    a_outRgba[i + 1] = static_cast<std::uint8_t>((static_cast<unsigned>(a_outRgba[i + 1]) * mainG + 127u) / 255u);
                    a_outRgba[i + 2] = static_cast<std::uint8_t>((static_cast<unsigned>(a_outRgba[i + 2]) * mainB + 127u) / 255u);
                }
            }

            const int dstX0 = a_tileSize - subSize;
            const int dstY0 = a_tileSize - subSize;

            // 1. Cut a subtle 1px halo out of the main icon behind the bottom-right subicon
            for (int y = std::max(0, dstY0 - 1); y < a_tileSize; ++y) {
                for (int x = std::max(0, dstX0 - 1); x < a_tileSize; ++x) {
                    std::uint8_t maxNeighborAlpha = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int sx = (x + dx) - dstX0;
                            const int sy = (y + dy) - dstY0;
                            if (sx >= 0 && sx < subSize && sy >= 0 && sy < subSize) {
                                const std::size_t sIdx = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(subSize) +
                                                          static_cast<std::size_t>(sx)) * 4u;
                                maxNeighborAlpha = std::max(maxNeighborAlpha, subRgba[sIdx + 3]);
                            }
                        }
                    }
                    if (maxNeighborAlpha > 0) {
                        const std::size_t dIdx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(a_tileSize) +
                                                  static_cast<std::size_t>(x)) * 4u;
                        const int keepAlpha = (static_cast<int>(a_outRgba[dIdx + 3]) * (255 - static_cast<int>(maxNeighborAlpha))) / 255;
                        a_outRgba[dIdx + 3] = static_cast<std::uint8_t>(std::clamp(keepAlpha, 0, 255));
                    }
                }
            }

            // 2. Alpha-composite the subicon over the bottom-right quadrant (straight alpha)
            for (int sy = 0; sy < subSize; ++sy) {
                const int dy = dstY0 + sy;
                if (dy < 0 || dy >= a_tileSize) {
                    continue;
                }
                for (int sx = 0; sx < subSize; ++sx) {
                    const int dx = dstX0 + sx;
                    if (dx < 0 || dx >= a_tileSize) {
                        continue;
                    }
                    const std::size_t sIdx = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(subSize) +
                                              static_cast<std::size_t>(sx)) * 4u;
                    const float sa = static_cast<float>(subRgba[sIdx + 3]) / 255.0f;
                    if (sa <= 0.0f) {
                        continue;
                    }
                    const std::size_t dIdx = (static_cast<std::size_t>(dy) * static_cast<std::size_t>(a_tileSize) +
                                              static_cast<std::size_t>(dx)) * 4u;
                    const float da = static_cast<float>(a_outRgba[dIdx + 3]) / 255.0f;
                    const float outA = sa + da * (1.0f - sa);
                    const float srcR = bakeColors
                        ? (static_cast<float>(subRgba[sIdx + 0]) * (static_cast<float>(subR) / 255.0f))
                        : static_cast<float>(subRgba[sIdx + 0]);
                    const float srcG = bakeColors
                        ? (static_cast<float>(subRgba[sIdx + 1]) * (static_cast<float>(subG) / 255.0f))
                        : static_cast<float>(subRgba[sIdx + 1]);
                    const float srcB = bakeColors
                        ? (static_cast<float>(subRgba[sIdx + 2]) * (static_cast<float>(subB) / 255.0f))
                        : static_cast<float>(subRgba[sIdx + 2]);
                    if (outA > 0.0001f) {
                        const float r = (srcR * sa + static_cast<float>(a_outRgba[dIdx + 0]) * da * (1.0f - sa)) / outA;
                        const float g = (srcG * sa + static_cast<float>(a_outRgba[dIdx + 1]) * da * (1.0f - sa)) / outA;
                        const float b = (srcB * sa + static_cast<float>(a_outRgba[dIdx + 2]) * da * (1.0f - sa)) / outA;
                        a_outRgba[dIdx + 0] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(r)), 0, 255));
                        a_outRgba[dIdx + 1] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(g)), 0, 255));
                        a_outRgba[dIdx + 2] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(b)), 0, 255));
                    }
                    a_outRgba[dIdx + 3] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(outA * 255.0f)), 0, 255));
                }
            }
            return bakeColors;
        };

        if (rasterizeOne(a_resolved.swfRelPath, a_resolved.symbolName, a_tileSize, a_outRgba)) {
            const bool baked = compositeSubicon(
                a_resolved.swfRelPath,
                a_resolved.subSymbolName,
                a_resolved.rgbColor,
                a_resolved.hasColor,
                a_resolved.subRgbColor,
                a_resolved.hasSubColor);
            if (a_outColorsBaked) {
                *a_outColorsBaked = baked;
            }
            return true;
        }
        if (a_resolved.hasFallback &&
            rasterizeOne(a_resolved.fallbackSwfRelPath, a_resolved.fallbackSymbolName, a_tileSize, a_outRgba)) {
            if (a_outUsedFallback) {
                *a_outUsedFallback = true;
            }
            const bool baked = compositeSubicon(
                a_resolved.fallbackSwfRelPath,
                a_resolved.fallbackSubSymbolName,
                a_resolved.fallbackRgbColor,
                a_resolved.fallbackHasColor,
                a_resolved.fallbackSubRgbColor,
                a_resolved.fallbackHasSubColor);
            if (a_outColorsBaked) {
                *a_outColorsBaked = baked;
            }
            return true;
        }
        return false;
    }
}
