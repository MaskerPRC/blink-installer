// Minimal JSON value / parser / serializer.
//
// Deliberately hand-rolled rather than pulling in nlohmann or jsoncpp: this DLL
// ships inside every installer built with the tool, so binary size matters, and
// our needs (ability arguments, the config store) are modest. Strings are UTF-8
// throughout; conversion to UTF-16 happens at the Win32 boundary in strings.h.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace bk {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool v) : type_(Type::Bool), bool_(v) {}
  Json(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(unsigned int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  // `long` is a distinct type from `int` on MSVC, and it is what most of Win32
  // hands back (LONG, LSTATUS, the RECT members). Without these overloads
  // every call site needs a cast.
  Json(long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(unsigned long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(unsigned long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Json(double v) : type_(Type::Number), num_(v) {}
  Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
  Json(std::string v) : type_(Type::String), str_(std::move(v)) {}
  Json(Array v) : type_(Type::Array), arr_(std::move(v)) {}
  Json(Object v) : type_(Type::Object), obj_(std::move(v)) {}

  static Json object() { return Json(Object{}); }
  static Json array() { return Json(Array{}); }

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  // Lenient accessors: return the fallback when the type does not match, so
  // callers can read optional ability arguments without pre-checking.
  bool as_bool(bool fallback = false) const;
  double as_double(double fallback = 0.0) const;
  int as_int(int fallback = 0) const;
  long long as_int64(long long fallback = 0) const;
  std::string as_string(const std::string& fallback = {}) const;

  // Object access. Reading a missing key yields a null Json; writing promotes
  // the value to an object first.
  const Json& operator[](const std::string& key) const;
  Json& operator[](const std::string& key);
  bool has(const std::string& key) const;

  // Array access.
  const Json& operator[](size_t index) const;
  void push_back(Json v);
  size_t size() const;

  const Object& as_object() const { return obj_; }
  const Array& as_array() const { return arr_; }

  // Serialize to compact UTF-8 JSON. Non-ASCII is emitted as raw UTF-8 rather
  // than \u escapes; miniblink and Node both accept that.
  std::string dump() const;

  // Returns false and leaves `out` untouched on malformed input. `error` gets a
  // human-readable reason with a byte offset when non-null.
  static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

 private:
  static const Json& null_instance();

  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0.0;
  std::string str_;
  Array arr_;
  Object obj_;
};

}  // namespace bk
