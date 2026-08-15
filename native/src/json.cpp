#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bk {
namespace {

// Appends `cp` to `out` as UTF-8.
void AppendUtf8(std::string& out, unsigned int cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

class Parser {
 public:
  Parser(const std::string& text) : s_(text) {}

  bool Run(Json& out) {
    SkipWs();
    if (!ParseValue(out)) return false;
    SkipWs();
    if (i_ != s_.size()) return Fail("trailing content after JSON value");
    return true;
  }

  const std::string& error() const { return error_; }

 private:
  bool Fail(const char* why) {
    if (error_.empty()) {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%s at offset %zu", why, i_);
      error_ = buf;
    }
    return false;
  }

  void SkipWs() {
    while (i_ < s_.size()) {
      const char c = s_[i_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i_;
      } else {
        break;
      }
    }
  }

  bool Literal(const char* lit) {
    const size_t n = std::char_traits<char>::length(lit);
    if (s_.compare(i_, n, lit) != 0) return false;
    i_ += n;
    return true;
  }

  bool ParseValue(Json& out) {
    if (i_ >= s_.size()) return Fail("unexpected end of input");
    switch (s_[i_]) {
      case 'n':
        if (!Literal("null")) return Fail("invalid literal");
        out = Json();
        return true;
      case 't':
        if (!Literal("true")) return Fail("invalid literal");
        out = Json(true);
        return true;
      case 'f':
        if (!Literal("false")) return Fail("invalid literal");
        out = Json(false);
        return true;
      case '"': {
        std::string str;
        if (!ParseString(str)) return false;
        out = Json(std::move(str));
        return true;
      }
      case '[':
        return ParseArray(out);
      case '{':
        return ParseObject(out);
      default:
        return ParseNumber(out);
    }
  }

  bool ParseString(std::string& out) {
    if (i_ >= s_.size() || s_[i_] != '"') return Fail("expected '\"'");
    ++i_;
    out.clear();
    while (true) {
      if (i_ >= s_.size()) return Fail("unterminated string");
      const unsigned char c = static_cast<unsigned char>(s_[i_]);
      if (c == '"') {
        ++i_;
        return true;
      }
      if (c == '\\') {
        ++i_;
        if (i_ >= s_.size()) return Fail("unterminated escape");
        const char e = s_[i_++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            unsigned int cp = 0;
            if (!ParseHex4(cp)) return false;
            // Combine surrogate pairs so astral characters survive a round trip.
            if (cp >= 0xD800 && cp <= 0xDBFF && s_.compare(i_, 2, "\\u") == 0) {
              const size_t save = i_;
              i_ += 2;
              unsigned int low = 0;
              if (ParseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
              } else {
                i_ = save;
              }
            }
            AppendUtf8(out, cp);
            break;
          }
          default:
            return Fail("invalid escape character");
        }
        continue;
      }
      if (c < 0x20) return Fail("unescaped control character in string");
      out.push_back(static_cast<char>(c));
      ++i_;
    }
  }

  bool ParseHex4(unsigned int& out) {
    if (i_ + 4 > s_.size()) return Fail("truncated \\u escape");
    out = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_ + k];
      out <<= 4;
      if (c >= '0' && c <= '9') {
        out |= static_cast<unsigned int>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        out |= static_cast<unsigned int>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        out |= static_cast<unsigned int>(c - 'A' + 10);
      } else {
        return Fail("invalid hex digit in \\u escape");
      }
    }
    i_ += 4;
    return true;
  }

  bool ParseNumber(Json& out) {
    const size_t start = i_;
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
    bool any = false;
    while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { ++i_; any = true; }
    if (i_ < s_.size() && s_[i_] == '.') {
      ++i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { ++i_; any = true; }
    }
    if (!any) return Fail("invalid number");
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
      bool exp_digits = false;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') { ++i_; exp_digits = true; }
      if (!exp_digits) return Fail("invalid exponent");
    }
    out = Json(std::strtod(s_.substr(start, i_ - start).c_str(), nullptr));
    return true;
  }

  bool ParseArray(Json& out) {
    ++i_;  // '['
    out = Json::array();
    SkipWs();
    if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
    while (true) {
      SkipWs();
      Json item;
      if (!ParseValue(item)) return false;
      out.push_back(std::move(item));
      SkipWs();
      if (i_ >= s_.size()) return Fail("unterminated array");
      if (s_[i_] == ',') { ++i_; continue; }
      if (s_[i_] == ']') { ++i_; return true; }
      return Fail("expected ',' or ']'");
    }
  }

  bool ParseObject(Json& out) {
    ++i_;  // '{'
    out = Json::object();
    SkipWs();
    if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
    while (true) {
      SkipWs();
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (i_ >= s_.size() || s_[i_] != ':') return Fail("expected ':'");
      ++i_;
      SkipWs();
      Json value;
      if (!ParseValue(value)) return false;
      out[key] = std::move(value);
      SkipWs();
      if (i_ >= s_.size()) return Fail("unterminated object");
      if (s_[i_] == ',') { ++i_; continue; }
      if (s_[i_] == '}') { ++i_; return true; }
      return Fail("expected ',' or '}'");
    }
  }

  const std::string& s_;
  size_t i_ = 0;
  std::string error_;
};

void DumpString(const std::string& in, std::string& out) {
  out.push_back('"');
  for (char raw : in) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          // Pass UTF-8 continuation bytes through untouched.
          out.push_back(raw);
        }
    }
  }
  out.push_back('"');
}

void DumpNumber(double v, std::string& out) {
  if (!std::isfinite(v)) {
    out += "null";  // JSON has no NaN/Infinity.
    return;
  }
  char buf[40];
  // Emit integral values without a trailing ".0" — NSIS and JS both read these
  // back as strings in places, and "1" is friendlier than "1.0".
  if (v == static_cast<double>(static_cast<long long>(v)) &&
      std::fabs(v) < 9.2e18) {
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
  } else {
    std::snprintf(buf, sizeof(buf), "%.17g", v);
  }
  out += buf;
}

}  // namespace

const Json& Json::null_instance() {
  static const Json kNull;
  return kNull;
}

bool Json::as_bool(bool fallback) const {
  return type_ == Type::Bool ? bool_ : fallback;
}

double Json::as_double(double fallback) const {
  return type_ == Type::Number ? num_ : fallback;
}

int Json::as_int(int fallback) const {
  return type_ == Type::Number ? static_cast<int>(num_) : fallback;
}

long long Json::as_int64(long long fallback) const {
  return type_ == Type::Number ? static_cast<long long>(num_) : fallback;
}

std::string Json::as_string(const std::string& fallback) const {
  return type_ == Type::String ? str_ : fallback;
}

const Json& Json::operator[](const std::string& key) const {
  if (type_ != Type::Object) return null_instance();
  auto it = obj_.find(key);
  return it == obj_.end() ? null_instance() : it->second;
}

Json& Json::operator[](const std::string& key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    obj_.clear();
  }
  return obj_[key];
}

bool Json::has(const std::string& key) const {
  return type_ == Type::Object && obj_.find(key) != obj_.end();
}

const Json& Json::operator[](size_t index) const {
  if (type_ != Type::Array || index >= arr_.size()) return null_instance();
  return arr_[index];
}

void Json::push_back(Json v) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    arr_.clear();
  }
  arr_.push_back(std::move(v));
}

size_t Json::size() const {
  if (type_ == Type::Array) return arr_.size();
  if (type_ == Type::Object) return obj_.size();
  return 0;
}

std::string Json::dump() const {
  std::string out;
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: DumpNumber(num_, out); break;
    case Type::String: DumpString(str_, out); break;
    case Type::Array: {
      out.push_back('[');
      bool first = true;
      for (const Json& item : arr_) {
        if (!first) out.push_back(',');
        first = false;
        out += item.dump();
      }
      out.push_back(']');
      break;
    }
    case Type::Object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, value] : obj_) {
        if (!first) out.push_back(',');
        first = false;
        DumpString(key, out);
        out.push_back(':');
        out += value.dump();
      }
      out.push_back('}');
      break;
    }
  }
  return out;
}

bool Json::parse(const std::string& text, Json& out, std::string* error) {
  Parser parser(text);
  Json result;
  if (!parser.Run(result)) {
    if (error) *error = parser.error();
    return false;
  }
  out = std::move(result);
  return true;
}

}  // namespace bk
