#include "hevc_launch.h"
#include <cctype>
#include <cstring>
#include <string>

namespace hevc_probe {
namespace {
static const size_t kMaxArgument = 2048;
struct Reader {
    const char *text;
    size_t size, pos;
    LaunchDecision result;
    bool seen_mode;
    Reader(const char *input, size_t length)
        : text(input), size(length), pos(0), seen_mode(false) {}
    bool skip_space() {
        while (pos < size && (text[pos] == ' ' || text[pos] == '\t' ||
                              text[pos] == '\r' || text[pos] == '\n')) ++pos;
        return pos < size;
    }
    bool string(std::string& value) {
        if (pos >= size || text[pos++] != '"') return false;
        value.clear();
        while (pos < size) {
            unsigned char c = static_cast<unsigned char>(text[pos++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c == '\\') {
                if (pos >= size) return false;
                c = static_cast<unsigned char>(text[pos++]);
                if (c == 'u') {
                    for (unsigned i = 0; i < 4; ++i) {
                        if (pos >= size || !std::isxdigit(static_cast<unsigned char>(text[pos++]))) return false;
                    }
                    c = '?'; // Escaped keys/modes do not authorize playback.
                } else if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' ||
                           c == 'n' || c == 'r' || c == 't') c = '?';
                else return false;
            }
            if (value.size() < 64) value.push_back(static_cast<char>(c));
        }
        return false;
    }
    bool literal(const char *word) {
        const size_t length = std::strlen(word);
        if (length > size - pos || std::memcmp(text + pos, word, length) != 0) return false;
        pos += length; return true;
    }
    bool number() {
        if (pos < size && text[pos] == '-') ++pos;
        if (pos >= size) return false;
        if (text[pos] == '0') ++pos;
        else {
            if (text[pos] < '1' || text[pos] > '9') return false;
            do { ++pos; } while (pos < size && text[pos] >= '0' && text[pos] <= '9');
        }
        if (pos < size && text[pos] == '.') {
            ++pos; if (pos >= size || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
            do { ++pos; } while (pos < size && std::isdigit(static_cast<unsigned char>(text[pos])));
        }
        if (pos < size && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < size && (text[pos] == '+' || text[pos] == '-')) ++pos;
            if (pos >= size || !std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
            do { ++pos; } while (pos < size && std::isdigit(static_cast<unsigned char>(text[pos])));
        }
        return true;
    }
    bool value(unsigned depth) {
        if (depth > 5 || !skip_space()) return false;
        if (text[pos] == '"') { std::string discarded; return string(discarded); }
        if (text[pos] == '{') return object(depth + 1, false);
        if (text[pos] == '[') {
            ++pos;
            if (!skip_space()) return false;
            if (text[pos] == ']') { ++pos; return true; }
            do {
                if (!value(depth + 1) || !skip_space()) return false;
                if (text[pos] == ']') { ++pos; return true; }
                if (text[pos++] != ',') return false;
            } while (true);
        }
        if (text[pos] == 't') return literal("true");
        if (text[pos] == 'f') return literal("false");
        if (text[pos] == 'n') return literal("null");
        return number();
    }
    bool object(unsigned depth, bool inspect_mode) {
        if (depth > 5 || pos >= size || text[pos++] != '{' || !skip_space()) return false;
        if (text[pos] == '}') { ++pos; return true; }
        do {
            std::string key;
            if (!string(key) || !skip_space() || text[pos++] != ':' || !skip_space()) return false;
            if (inspect_mode && key == "mode") {
                if (seen_mode) { result.status = LaunchStatus::Duplicate; return false; }
                seen_mode = true;
                std::string mode;
                if (!string(mode)) return false;
                if (mode == "1080p") result.mode = LaunchMode::Hd1080;
                else if (mode == "uhd") result.mode = LaunchMode::Uhd;
                else { result.status = LaunchStatus::Unsupported; return false; }
            } else if (depth == 0 && key == "params" && text[pos] == '{') {
                if (!object(depth + 1, true)) return false;
            } else if (!value(depth + 1)) return false;
            if (!skip_space()) return false;
            if (text[pos] == '}') { ++pos; return true; }
            if (text[pos++] != ',' || !skip_space()) return false;
        } while (true);
    }
};
}
LaunchDecision parse_launch_argument(const char *argument) {
    LaunchDecision result;
    if (!argument) return result;
    size_t size = 0;
    while (size <= kMaxArgument && argument[size] != '\0') ++size;
    result.argument_bytes = size;
    if (size > kMaxArgument) { result.status = LaunchStatus::Oversized; return result; }
    Reader parser{argument, size};
    if (!parser.skip_space() || parser.text[parser.pos] != '{') {
        result.status = LaunchStatus::NotJson; return result;
    }
    parser.result.status = LaunchStatus::NoMode;
    const bool valid = parser.object(0, true);
    parser.skip_space();
    if (!valid || parser.pos != size) {
        result.status = parser.result.status == LaunchStatus::Duplicate ||
                        parser.result.status == LaunchStatus::Unsupported ? parser.result.status : LaunchStatus::Malformed;
        return result;
    }
    result.mode = parser.result.mode;
    result.status = parser.seen_mode ? LaunchStatus::Valid : LaunchStatus::NoMode;
    return result;
}
const char *launch_status_name(LaunchStatus status) {
    switch (status) {
    case LaunchStatus::Valid: return "valid";
    case LaunchStatus::Missing: return "missing";
    case LaunchStatus::NotJson: return "non-json";
    case LaunchStatus::Oversized: return "oversized";
    case LaunchStatus::Malformed: return "malformed";
    case LaunchStatus::NoMode: return "no-mode";
    case LaunchStatus::Unsupported: return "unsupported";
    case LaunchStatus::Duplicate: return "duplicate";
    }
    return "unknown";
}
}
