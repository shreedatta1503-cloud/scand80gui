#include "Json.h"
#include "Common.h"

#include <cstdlib>

namespace qgc {

const JsonValue &JsonValue::operator[](const std::wstring &key) const
{
    static const JsonValue kNull;
    if (type != Type::Object) {
        return kNull;
    }
    const auto it = objectValue.find(key);
    return it == objectValue.end() ? kNull : it->second;
}

bool JsonValue::contains(const std::wstring &key) const
{
    return type == Type::Object && objectValue.find(key) != objectValue.end();
}

std::wstring JsonValue::asString(const std::wstring &fallback) const
{
    return type == Type::String ? stringValue : fallback;
}

double JsonValue::asNumber(double fallback) const
{
    return type == Type::Number ? numberValue : fallback;
}

bool JsonValue::asBool(bool fallback) const
{
    return type == Type::Bool ? boolValue : fallback;
}

namespace {

class Parser {
public:
    Parser(const std::string &text) : m_text(text) {}

    bool parse(JsonValue &out, std::wstring &error)
    {
        skipWhitespace();
        if (!parseValue(out)) {
            error = m_error;
            return false;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            error = L"Trailing data after JSON value";
            return false;
        }
        return true;
    }

private:
    const std::string &m_text;
    size_t m_pos = 0;
    std::wstring m_error;

    bool fail(const wchar_t *msg) { m_error = msg; return false; }
    bool atEnd() const { return m_pos >= m_text.size(); }
    char peek() const { return m_text[m_pos]; }

    void skipWhitespace()
    {
        while (!atEnd()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                // Tolerate // line comments (jsonc convenience in our manifests).
                m_pos += 2;
                while (!atEnd() && peek() != '\n') {
                    ++m_pos;
                }
            } else {
                break;
            }
        }
    }

    bool parseValue(JsonValue &out)
    {
        skipWhitespace();
        if (atEnd()) {
            return fail(L"Unexpected end of input");
        }
        switch (peek()) {
        case '{': return parseObject(out);
        case '[': return parseArray(out);
        case '"': return parseString(out);
        case 't': case 'f': return parseBool(out);
        case 'n': return parseNull(out);
        default:  return parseNumber(out);
        }
    }

    bool parseObject(JsonValue &out)
    {
        out.type = JsonValue::Type::Object;
        ++m_pos; // consume '{'
        skipWhitespace();
        if (!atEnd() && peek() == '}') { ++m_pos; return true; }
        for (;;) {
            skipWhitespace();
            if (atEnd() || peek() != '"') {
                return fail(L"Expected object key");
            }
            JsonValue key;
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (atEnd() || peek() != ':') {
                return fail(L"Expected ':' in object");
            }
            ++m_pos;
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.objectValue[key.stringValue] = std::move(value);
            skipWhitespace();
            if (atEnd()) {
                return fail(L"Unterminated object");
            }
            if (peek() == ',') { ++m_pos; continue; }
            if (peek() == '}') { ++m_pos; return true; }
            return fail(L"Expected ',' or '}' in object");
        }
    }

    bool parseArray(JsonValue &out)
    {
        out.type = JsonValue::Type::Array;
        ++m_pos; // consume '['
        skipWhitespace();
        if (!atEnd() && peek() == ']') { ++m_pos; return true; }
        for (;;) {
            JsonValue value;
            if (!parseValue(value)) {
                return false;
            }
            out.arrayValue.push_back(std::move(value));
            skipWhitespace();
            if (atEnd()) {
                return fail(L"Unterminated array");
            }
            if (peek() == ',') { ++m_pos; continue; }
            if (peek() == ']') { ++m_pos; return true; }
            return fail(L"Expected ',' or ']' in array");
        }
    }

    bool parseString(JsonValue &out)
    {
        out.type = JsonValue::Type::String;
        ++m_pos; // consume opening quote
        std::wstring result;
        while (!atEnd()) {
            const unsigned char c = static_cast<unsigned char>(m_text[m_pos++]);
            if (c == '"') {
                out.stringValue = std::move(result);
                return true;
            }
            if (c == '\\') {
                if (atEnd()) {
                    return fail(L"Unterminated escape");
                }
                const char esc = m_text[m_pos++];
                switch (esc) {
                case '"':  result.push_back(L'"'); break;
                case '\\': result.push_back(L'\\'); break;
                case '/':  result.push_back(L'/'); break;
                case 'b':  result.push_back(L'\b'); break;
                case 'f':  result.push_back(L'\f'); break;
                case 'n':  result.push_back(L'\n'); break;
                case 'r':  result.push_back(L'\r'); break;
                case 't':  result.push_back(L'\t'); break;
                case 'u': {
                    if (m_pos + 4 > m_text.size()) {
                        return fail(L"Invalid \\u escape");
                    }
                    unsigned int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = m_text[m_pos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail(L"Invalid hex in \\u escape");
                    }
                    result.push_back(static_cast<wchar_t>(code));
                    break;
                }
                default:
                    return fail(L"Invalid escape character");
                }
            } else if (c < 0x80) {
                result.push_back(static_cast<wchar_t>(c));
            } else {
                // Collect a UTF-8 multibyte sequence and decode it as a unit.
                std::string bytes;
                bytes.push_back(static_cast<char>(c));
                int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
                for (int i = 0; i < extra && !atEnd(); ++i) {
                    bytes.push_back(m_text[m_pos++]);
                }
                result += utf8ToWide(bytes);
            }
        }
        return fail(L"Unterminated string");
    }

    bool parseNumber(JsonValue &out)
    {
        const size_t start = m_pos;
        if (!atEnd() && (peek() == '-' || peek() == '+')) {
            ++m_pos;
        }
        while (!atEnd()) {
            const char c = peek();
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                ++m_pos;
            } else {
                break;
            }
        }
        if (m_pos == start) {
            return fail(L"Invalid number");
        }
        out.type = JsonValue::Type::Number;
        out.numberValue = std::strtod(m_text.c_str() + start, nullptr);
        return true;
    }

    bool parseBool(JsonValue &out)
    {
        if (m_text.compare(m_pos, 4, "true") == 0) {
            out.type = JsonValue::Type::Bool; out.boolValue = true; m_pos += 4; return true;
        }
        if (m_text.compare(m_pos, 5, "false") == 0) {
            out.type = JsonValue::Type::Bool; out.boolValue = false; m_pos += 5; return true;
        }
        return fail(L"Invalid literal");
    }

    bool parseNull(JsonValue &out)
    {
        if (m_text.compare(m_pos, 4, "null") == 0) {
            out.type = JsonValue::Type::Null; m_pos += 4; return true;
        }
        return fail(L"Invalid literal");
    }
};

} // namespace

bool parseJson(const std::string &utf8, JsonValue &out, std::wstring &error)
{
    // Skip a UTF-8 BOM if present.
    size_t offset = 0;
    if (utf8.size() >= 3 &&
        static_cast<unsigned char>(utf8[0]) == 0xEF &&
        static_cast<unsigned char>(utf8[1]) == 0xBB &&
        static_cast<unsigned char>(utf8[2]) == 0xBF) {
        offset = 3;
    }
    // Bind to a named local so the string outlives `parser` (which holds a
    // reference to it) across the following statement.
    const std::string body = offset ? utf8.substr(offset) : utf8;
    Parser parser(body);
    return parser.parse(out, error);
}

} // namespace qgc
