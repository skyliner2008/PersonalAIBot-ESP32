/**
 * @file Calc.h
 * @brief Small, safe arithmetic evaluator for the `calculate` tool (header-only, host-testable)
 *
 *   + - * / ^ (power) % (modulo), parentheses, unary minus, thousands separators "1,234.5",
 *   "x%" as x/100, functions sqrt abs round floor ceil sin cos tan asin acos atan log (10) ln exp,
 *   constants pi e. Trig uses degrees (what people mean when they say "sin 30").
 *   Returns false with an error message instead of throwing; recursion depth is bounded.
 */

#pragma once

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace calc {

class Parser {
public:
    explicit Parser(const char* s) : _s(s) {}

    bool run(double& out, const char*& err) {
        out = expr(0);
        skip();
        if (!_err && *_s) _err = "unexpected character";
        err = _err;
        return !_err && isfinite(out);
    }

private:
    const char* _s;
    const char* _err = nullptr;
    static constexpr double kDeg = 3.14159265358979323846 / 180.0;

    void skip() {
        while (*_s == ' ' || *_s == '\t') _s++;
    }
    bool eat(char c) {
        skip();
        if (*_s == c) {
            _s++;
            return true;
        }
        return false;
    }

    double expr(int depth) {  // + -
        if (depth > 40) {
            _err = "too deeply nested";
            return 0;
        }
        double v = term(depth);
        for (;;) {
            if (eat('+')) v += term(depth);
            else if (eat('-')) v -= term(depth);
            else return v;
        }
    }
    double term(int depth) {  // * / %
        double v = power(depth);
        for (;;) {
            skip();
            if (*_s == '*' || (*_s == 'x' && !isalpha((unsigned char)_s[1]))) {
                _s++;
                v *= power(depth);
            } else if (eat('/')) {
                const double d = power(depth);
                if (d == 0) _err = "division by zero";
                else v /= d;
            } else if (*_s == '%' && !isPercentSuffix()) {
                _s++;
                const double d = power(depth);
                if (d == 0) _err = "modulo by zero";
                else v = fmod(v, d);
            } else {
                return v;
            }
        }
    }
    // "50%" at the end of a number (followed by an operator / end) means 0.5
    bool isPercentSuffix() const {
        const char* p = _s + 1;
        while (*p == ' ') p++;
        return *p == 0 || *p == ')' || *p == '+' || *p == '-' || *p == '*' || *p == '/';
    }
    double power(int depth) {  // ^ (right associative)
        const double b = unary(depth);
        if (eat('^')) return pow(b, power(depth));
        return b;
    }
    double unary(int depth) {
        if (eat('-')) return -unary(depth);
        if (eat('+')) return unary(depth);
        return postfix(depth);
    }
    double postfix(int depth) {
        double v = primary(depth);
        skip();
        if (*_s == '%' && isPercentSuffix()) {
            _s++;
            v /= 100.0;
        }
        return v;
    }
    double primary(int depth) {
        skip();
        if (eat('(')) {
            const double v = expr(depth + 1);
            if (!eat(')')) _err = "missing )";
            return v;
        }
        if (isdigit((unsigned char)*_s) || *_s == '.') return number();
        if (isalpha((unsigned char)*_s)) return function(depth);
        _err = "expected a number";
        return 0;
    }
    double number() {
        char buf[48];
        size_t n = 0;
        while ((isdigit((unsigned char)*_s) || *_s == '.' || *_s == ',') && n < sizeof(buf) - 1) {
            if (*_s != ',') buf[n++] = *_s;
            _s++;
        }
        if ((*_s == 'e' || *_s == 'E') && (isdigit((unsigned char)_s[1]) || _s[1] == '-' || _s[1] == '+')) {
            buf[n++] = *_s++;
            if (*_s == '-' || *_s == '+') buf[n++] = *_s++;
            while (isdigit((unsigned char)*_s) && n < sizeof(buf) - 1) buf[n++] = *_s++;
        }
        buf[n] = 0;
        return strtod(buf, nullptr);
    }
    double function(int depth) {
        char name[12];
        size_t n = 0;
        while (isalpha((unsigned char)*_s) && n < sizeof(name) - 1) name[n++] = (char)tolower((unsigned char)*_s++);
        name[n] = 0;
        if (!strcmp(name, "pi")) return 3.14159265358979323846;
        if (!strcmp(name, "e")) return 2.71828182845904523536;
        const double a = unary(depth + 1);  // "sqrt 16" and "sqrt(16)" both work
        if (!strcmp(name, "sqrt")) {
            if (a < 0) _err = "sqrt of a negative number";
            return a < 0 ? 0 : sqrt(a);
        }
        if (!strcmp(name, "abs")) return fabs(a);
        if (!strcmp(name, "round")) return round(a);
        if (!strcmp(name, "floor")) return floor(a);
        if (!strcmp(name, "ceil")) return ceil(a);
        if (!strcmp(name, "sin")) return sin(a * kDeg);
        if (!strcmp(name, "cos")) return cos(a * kDeg);
        if (!strcmp(name, "tan")) return tan(a * kDeg);
        if (!strcmp(name, "asin")) return asin(a) / kDeg;
        if (!strcmp(name, "acos")) return acos(a) / kDeg;
        if (!strcmp(name, "atan")) return atan(a) / kDeg;
        if (!strcmp(name, "log")) return log10(a);
        if (!strcmp(name, "ln")) return log(a);
        if (!strcmp(name, "exp")) return exp(a);
        _err = "unknown function";
        return 0;
    }
};

/** Evaluate `expression`. On failure returns false and sets `err`. */
inline bool evaluate(const char* expression, double& result, const char*& err) {
    Parser p(expression ? expression : "");
    return p.run(result, err);
}

}  // namespace calc
