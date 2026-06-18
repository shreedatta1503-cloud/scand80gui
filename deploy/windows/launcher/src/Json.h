// ----------------------------------------------------------------------------
// Json — a tiny, dependency-free recursive-descent JSON reader.
//
// The bootstrap manifest schema is small and fixed, so a ~hand-rolled reader is
// preferred over vendoring a JSON library: it keeps the launcher binary tiny and
// free of third-party code. Supports objects, arrays, strings (with \uXXXX),
// numbers, booleans and null. Parsing is read-only; there is no writer.
// ----------------------------------------------------------------------------
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace qgc {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::wstring stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::wstring, JsonValue> objectValue;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const  { return type == Type::Array; }
    bool isString() const { return type == Type::String; }

    // Object accessors (return a static null value if absent / wrong type).
    const JsonValue &operator[](const std::wstring &key) const;
    bool contains(const std::wstring &key) const;

    std::wstring asString(const std::wstring &fallback = L"") const;
    double       asNumber(double fallback = 0.0) const;
    bool         asBool(bool fallback = false) const;
};

// Parses UTF-8 text. Returns false and fills `error` on malformed input.
bool parseJson(const std::string &utf8, JsonValue &out, std::wstring &error);

} // namespace qgc
