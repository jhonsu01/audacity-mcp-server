// Minimal JSON value, parser and writer (enough for engine jobs and results).
#pragma once

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aumcp::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value> > obj;

    bool isNull() const { return type == Type::Null; }
    const Value* find(const std::string& key) const
    {
        for (const auto& kv : obj) {
            if (kv.first == key) {
                return &kv.second;
            }
        }
        return nullptr;
    }
    double num(const std::string& key, double def) const
    {
        const Value* v = find(key);
        return v && v->type == Type::Number ? v->n : (v && v->type == Type::Bool ? (v->b ? 1.0 : 0.0) : def);
    }
    std::string str(const std::string& key, const std::string& def = {}) const
    {
        const Value* v = find(key);
        return v && v->type == Type::String ? v->s : def;
    }
    bool flag(const std::string& key, bool def) const
    {
        const Value* v = find(key);
        return v && v->type == Type::Bool ? v->b : (v && v->type == Type::Number ? v->n != 0.0 : def);
    }
};

class Parser
{
public:
    explicit Parser(const std::string& text) : m_t(text) {}
    Value parse()
    {
        Value v = value();
        ws();
        if (m_i != m_t.size()) {
            fail("trailing characters");
        }
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) { throw std::runtime_error(std::string("Invalid JSON: ") + what); }
    void ws()
    {
        while (m_i < m_t.size() && (m_t[m_i] == ' ' || m_t[m_i] == '\n' || m_t[m_i] == '\r' || m_t[m_i] == '\t')) {
            ++m_i;
        }
    }
    char peek()
    {
        ws();
        if (m_i >= m_t.size()) {
            fail("unexpected end");
        }
        return m_t[m_i];
    }
    void expect(char c)
    {
        if (peek() != c) {
            fail("unexpected character");
        }
        ++m_i;
    }
    static void utf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    unsigned hex4()
    {
        if (m_i + 4 > m_t.size()) {
            fail("bad escape");
        }
        unsigned v = static_cast<unsigned>(std::stoul(m_t.substr(m_i, 4), nullptr, 16));
        m_i += 4;
        return v;
    }
    std::string string()
    {
        expect('"');
        std::string out;
        while (m_i < m_t.size()) {
            char c = m_t[m_i++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_i >= m_t.size()) {
                break;
            }
            c = m_t[m_i++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp < 0xDC00 && m_i + 6 <= m_t.size() && m_t[m_i] == '\\' && m_t[m_i + 1] == 'u') {
                    m_i += 2;
                    const unsigned lo = hex4();
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out += c;
            }
        }
        fail("unterminated string");
    }
    Value value()
    {
        Value v;
        const char c = peek();
        if (c == '{') {
            ++m_i;
            v.type = Value::Type::Object;
            if (peek() == '}') {
                ++m_i;
                return v;
            }
            for (;;) {
                std::string k = string();
                expect(':');
                v.obj.emplace_back(std::move(k), value());
                if (peek() == ',') {
                    ++m_i;
                    continue;
                }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            ++m_i;
            v.type = Value::Type::Array;
            if (peek() == ']') {
                ++m_i;
                return v;
            }
            for (;;) {
                v.arr.push_back(value());
                if (peek() == ',') {
                    ++m_i;
                    continue;
                }
                expect(']');
                return v;
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            v.s = string();
            return v;
        }
        if (m_t.compare(m_i, 4, "true") == 0) {
            m_i += 4;
            v.type = Value::Type::Bool;
            v.b = true;
            return v;
        }
        if (m_t.compare(m_i, 5, "false") == 0) {
            m_i += 5;
            v.type = Value::Type::Bool;
            return v;
        }
        if (m_t.compare(m_i, 4, "null") == 0) {
            m_i += 4;
            return v;
        }
        size_t used = 0;
        try {
            v.n = std::stod(m_t.substr(m_i, 64), &used);
        } catch (...) {
            fail("bad value");
        }
        m_i += used;
        v.type = Value::Type::Number;
        return v;
    }

    const std::string& m_t;
    size_t m_i = 0;
};

inline std::string quote(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o + "\"";
}

inline std::string number(double d)
{
    if (!std::isfinite(d)) {
        return "null";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", d);
    return buf;
}

// Small builder: Obj().add("a", 1).add("b", "x").str()
class Obj
{
public:
    Obj& raw(const std::string& k, const std::string& rawJson)
    {
        m_s += (m_s.empty() ? "" : ",") + quote(k) + ":" + rawJson;
        return *this;
    }
    Obj& add(const std::string& k, const std::string& v) { return raw(k, quote(v)); }
    Obj& add(const std::string& k, const char* v) { return raw(k, quote(v)); }
    Obj& add(const std::string& k, double v) { return raw(k, number(v)); }
    Obj& add(const std::string& k, int v) { return raw(k, number(v)); }
    Obj& add(const std::string& k, long long v) { return raw(k, number(static_cast<double>(v))); }
    Obj& add(const std::string& k, bool v) { return raw(k, v ? "true" : "false"); }
    std::string str() const { return "{" + m_s + "}"; }

private:
    std::string m_s;
};

class Arr
{
public:
    Arr& raw(const std::string& rawJson)
    {
        m_s += (m_s.empty() ? "" : ",") + rawJson;
        return *this;
    }
    Arr& add(const std::string& v) { return raw(quote(v)); }
    std::string str() const { return "[" + m_s + "]"; }
    bool empty() const { return m_s.empty(); }

private:
    std::string m_s;
};

} // namespace aumcp::json
