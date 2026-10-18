// host stub of the bits of Arduino the dash preview needs
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <math.h>
uint32_t millis();
template <class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? (T)lo : (v > hi ? (T)hi : v); }
class String {
public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(int v) : s_(std::to_string(v)) {}
    const char* c_str() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    String& operator+=(const char* o) { s_ += o; return *this; }
    String operator+(const char* o) const { return String(s_ + o); }
    bool operator==(const char* o) const { return s_ == o; }
private:
    std::string s_;
};
struct SerialStub { int printf(const char* f, ...) { va_list a; va_start(a, f); int n = vprintf(f, a); va_end(a); return n; } void println(const char* s) { puts(s); } };
extern SerialStub Serial;
