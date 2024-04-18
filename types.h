#ifndef TYPES_H
#define TYPES_H 1

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <string>
#include <stdexcept>

enum class Direction { UP, DOWN };

inline enum Direction direction_str_ctor(const std::string& s) {
    if (s == "UP") {
        return Direction::UP;
    }

    if (s == "DOWN") {
        return Direction::DOWN;
    }

    throw std::invalid_argument("Direction must be either UP or DOWN");
}


typedef boost::multiprecision::cpp_dec_float_100 price_t;

// generic auto-incrementing numeric ID type (to be used for Agent IDs, Subscriber IDs)
// needs to be a template so we track auto-incrementing separately for separate uses of numeric_id
template<class>
class numeric_id {
    protected:
        unsigned int id;
        inline static unsigned int last_id = 0;
    public:
    numeric_id() {
        this->id = last_id++;
    }

    unsigned int to_numeric() const {
        return this->id;
    }

    numeric_id(const numeric_id &_id) : id(_id.id) {}
    numeric_id(unsigned int id) : id(id) {}

    std::string str() const {
        return std::to_string(id);
    }
    std::string to_string() const {
        return this->str();
    }

    auto operator<=> (const numeric_id& x) const = default;
    bool operator== (const numeric_id& x) const = default;
    auto operator=(const numeric_id& x) { id = x.id; }

    auto operator-- (int) = delete;
    auto operator++ (int) = delete;
    auto operator++ () = delete;
    auto operator+= (const numeric_id& x) = delete;
    auto operator+ (const numeric_id& x) = delete;

    struct Key {
        size_t operator()(const numeric_id& id) const {
            return std::hash<unsigned int>{}(id.to_numeric());
        }

    };
};

// trivial structs used as template arguments to numeric_id
struct market_numeric_id_tag {};
struct subscriber_numeric_id_tag {};

inline std::ostream& operator<<(std::ostream& os, const numeric_id<market_numeric_id_tag>& id) {
    return os << id.str();
}
inline std::ostream& operator<<(std::ostream& os, const numeric_id<subscriber_numeric_id_tag>& id) {
    return os << id.str();
}


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

    // TODO operator- ; not currently needed, but reasonable to expect that it exists
};

inline std::ostream& operator<<(std::ostream& os, const timepoint_t& tp) {
    return os << std::to_string(tp.to_numeric());
}

#endif 