#ifndef TYPES_H
#define TYPES_H 1

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <string>
#include <stdexcept>

enum class Direction { UP, DOWN };

inline enum Direction direction_str_ctor(std::string s) {
    if (s == "UP") {
        return Direction::UP;
    }

    if (s == "DOWN") {
        return Direction::DOWN;
    }

    throw std::invalid_argument("Direction must be either UP or DOWN");
}


typedef boost::multiprecision::cpp_dec_float_100 price_t;


class timepoint_t {
    protected:
    std::atomic<uintmax_t> tp;

    public:
    timepoint_t() = default;
    ~timepoint_t() = default;
    timepoint_t(const timepoint_t& _tp) { this->tp.store(_tp.tp.load()); }
    timepoint_t(const uintmax_t i) { this->tp.store(i); }

    uintmax_t to_numeric() const { return this->tp; }

    auto operator=(const timepoint_t& _tp) {
        this->tp.store(_tp.tp.load());
    }

    // TODO ostream

    auto operator<=> (const timepoint_t& x) const = default;
    bool operator< (const timepoint_t& other) const = default;
    bool operator== (const timepoint_t& x) const = default;
    auto operator--() {
        --this->tp;
        return *this;
    }
    auto operator-- (int) {
        timepoint_t old = *this;
        this->tp--;
        return old;
    }
    auto operator++() {
        ++this->tp;
        return *this;
    }
    auto operator++ (int) {
        timepoint_t old = *this;
        this->tp++;
        return old;
    }
    auto operator+= (const timepoint_t& other) {
        this->tp += other.tp;
        return *this;
    }
    auto operator+= (const uintmax_t& x) {
        this->tp += x;
        return *this;
    }
    auto operator+ (const timepoint_t& other) const {
        auto new_tp = timepoint_t();
        new_tp.tp = this->tp + other.tp;
        return new_tp;
    }
    uintmax_t operator-(const timepoint_t& other) const {
        return this->tp - other.tp;
    }
    //https://en.cppreference.com/w/cpp/language/operators
    auto operator+ (uintmax_t x) const {
        return this->tp + x;
    }
    friend timepoint_t operator+ (timepoint_t tp, const timepoint_t& other) {
        tp += other;
        return tp;
    }
    auto operator% (uintmax_t arg) const {
        return this->tp % arg;
    }


    // TODO operator-

    //const _now now; TODO
};

inline std::ostream& operator<<(std::ostream& os, const timepoint_t& tp) {
    return os << std::to_string(tp.to_numeric());
}

#endif 