#ifndef TS_H
#define TS_H

#include <cstdio>
#include <iostream>
#include <deque>
#include <optional>
#include <map>
#include "types.h"

#include <glog/logging.h>


/*  Rudimentary "timeseries" data structure ("ts") for storing several types of data used throughout 
    the program. It implements and enforces certain assumptions that we make about timeseries,
    such as deletion from only the beginning of the sequence, and also incorporates a notion of
    "missing" or "blank" entries which we attempt to track efficiently 

    There is additionally a "view" class, which is a simplified iterator-like type that 
    supports the operations that we need for the ts class. It allows us to expose the data 
    that the ts holds without exposing the ts's underlying implementation, and is specifically 
    adapted to use with the timepoint_t type.

    Essentially, ts and ts::view emulate a map<timepoint_t, T>, but with constant time reading
    and seeking, and fewer iterator invalidations (thanks to the underlying deque)

    The ts class is used throughout the program to represent various time series data:
    price data (ts<price_t>), an Agent's history of AgentActions (ts<AgentAction>), etc.

    The most important consumers of ts objects, and ts::view as well, are the various Subscriber
    types. Subscribers create temporary ts:view objects for the purpose of accessing, in a read-only
    fashion, the data in the respective ts objects without needing to pass the ts objects
    throughout the program or expose some other kind of accessors.

    ts::view uses the deque iterator of the underlying ts, which means ts::view is subject to the
    validity of the deque's iterator. 
    

    According to the documentation for std::deque, users of a ts::view (such as Subscribers) 
    should actually be able to use the same ts::view object (without having to get a new one),
    so long as they make sure not to try to read past data that has been deleted.

    Notes

        _first_tp represents the timepoint_t that corresponds to the first element in the ts.
        (When the ts is empty, _first_tp represents the timepoint_t that corresponds to where the
        first element will be placed, when append is called).

        The "cursor" is only defined when there are elements in the ts.
*/

// extra space is used to mark which points in series are missing, or alternatively,
// which ones are present. this allows for more efficient construction of the sparse_view,
// which 
enum class mark_mode_t { MARK_PRESENT, MARK_MISSING };

template<typename T>
class ts {
    protected:
    typename std::deque<std::optional<T>> seq;
    std::deque<std::size_t> _marked;
    const mark_mode_t mark_mode;
    timepoint_t _first_tp;


    public:
    ts(const timepoint_t& tp = 0, enum mark_mode_t m = mark_mode_t::MARK_MISSING) 
        : _first_tp(tp), mark_mode(m) {}

    ts(const ts& copy) = default;

    typedef T value_type;

    void clear() {
        this->seq.clear();
        this->_marked.clear();
    }

    const timepoint_t first_tp() {
        return this->_first_tp;
    }

    bool is_empty() const {
        return this->seq.size() == 0;
    }

    // indicates the position of the most recently inserted element, which is also the last element
    const std::optional<timepoint_t> 
    cursor() const {
        if (this->is_empty()) {
            return std::nullopt;
        } else {
            return this->_first_tp + this->seq.size() - 1;
        }
    }
    
    const std::deque<size_t>
    marked() const {
        return this->_marked;
    }

    std::unique_ptr<std::map<timepoint_t, T>> 
    to_map(const std::optional<timepoint_t> start = std::nullopt) {
        auto ret = std::make_unique<std::map<timepoint_t, T>>();
        if (this->is_empty()) {
            return ret;
        }

        timepoint_t cursor = start.has_value() ? start.value() : this->_first_tp;
        uintmax_t offset = cursor - this->_first_tp;

        for (size_t i=0 ; cursor <= this->cursor(); cursor++, i++) {
            auto x = this->seq.at(offset+i);
            if (x.has_value()) {
                (*ret)[cursor] = x.value();
            }
        }    

        return std::move(ret);
    }

    void append(T& x) {
        this->seq.push_back(x);
        if (this->mark_mode == mark_mode_t::MARK_PRESENT) {
            // inserted element is in the final index i.e. size - 1
            this->_marked.push_back(this->seq.size() - 1);
        }
    }


    // same as append but possibly skips forward
    void append_at(T& x, const timepoint_t& tp) {
        auto pos = (tp - this->_first_tp);

        if (pos < 0) {
            throw std::out_of_range("provided timepoint_t < _first_tp");
            // TODO 
        }

        if (pos > this->seq.size()) {
            for (int i=0; this->seq.size() < pos; i++) {
                this->seq.push_back(std::nullopt);

                if (this->mark_mode == mark_mode_t::MARK_MISSING) {
                    this->_marked.push_back(i);
                }
            }
        }

        if (pos < this->seq.size()) {
            std::ostringstream e;
            e   << "at: append_at cannot overwrite:"
                << " tp=" << std::to_string(tp.to_numeric()) 
                << " cursor=" << 
                    (this->cursor().has_value() 
                        ? std::to_string(this->cursor().value().to_numeric())
                        : "[none]"
                    )
            ;
            throw std::invalid_argument(e.str());
        }

        this->seq.push_back(x);
        if (this->mark_mode == mark_mode_t::MARK_PRESENT) {
            this->_marked.push_back(this->seq.size() - 1);
        }
    }

    void append(T&& x) {
        this->append(x);
    }

    void append_at(T&& x, const timepoint_t& tp) {
        this->append_at(x, tp);
    }

    void push_back(T&& x) { this->append(x); }
    void push_back(T& x) { this->append(x); }


    void skip(uintmax_t period) {
        this->seq.insert(this->seq.end(), period, std::nullopt);
        if (this->mark_mode == mark_mode_t::MARK_MISSING) {
            for (size_t i = this->seq.size() - period; i < this->seq.size(); i++) {
                this->_marked.push_back(i);
            }
        }
    }

    // up to and not including the specified timepoint
    uintmax_t delete_until(const timepoint_t& tp) {
        if (tp > this->_first_tp) {
            uintmax_t diff = tp - this->_first_tp;
            auto first = this->seq.begin();

            this->seq.erase(first, first + diff);
            std::erase_if(this->_marked, [&diff](auto& x) {
                return x < diff;
            });

            this->_first_tp += diff;
            return diff;
        } else {
            return 0;
        }
    }

    size_t size() const { return this->seq.size(); }

    // remove the last element in the TS, returning its position (which is now empty),
    // which is the same as 1 element ahead of the cursor
    timepoint_t pop() {
        if (this->seq.size() == 0) {
            return this->_first_tp;
        }

        auto popped_tp = this->cursor().value();

        this->seq.pop_back();
        // invariant: cursor must have a value if this->seq.size() > 0
        return popped_tp;
    }

    // throws when the value is out of the current range of the data.
    // optional: entries in the ts are allowed to be nullopt (i.e. ts values are always optional)
    std::optional<T> at(const timepoint_t& tp) const {
        if (!this->cursor().has_value() || tp > this->cursor().value()) {
            std::ostringstream e;
            e   << "at: timepoint lies beyond ts (too high):"
                << " tp=" << std::to_string(tp.to_numeric())
                << " cursor=" 
                    << (this->cursor().has_value() 
                        ? std::to_string(this->cursor().value().to_numeric())
                        : "[none]"
                    )
                << " size=" << std::to_string(this->seq.size()).c_str()

            ;
            throw std::out_of_range(e.str());
        }

        if (tp < this->_first_tp) {
            std::ostringstream e;
            e   << "at: timepoint lies beyond ts (too low):"
                << " tp=" << std::to_string(tp.to_numeric())
                << " _first_tp=" << std::to_string(this->_first_tp.to_numeric())
                << " size=" << std::to_string(this->seq.size()).c_str()
            ;
            throw std::out_of_range(e.str());
        }

        return this->seq.at(tp - this->_first_tp);
    }


    /*
        view and sparse_view
        - the ts object provided to either view or sparse_view must not be destroyed before 
            the view/sparse_view 
    */


    /*  When constructed, the sparse_view keeps a record of each element in the underlying ts which
        is _marked as being present (non-empty)
        Unlike the view, the sparse_view captures only a fixed contiguous range of values from 
        the underlying ts when constructed; it is not able to continue beyond this original 
        range to newer values. 
            (The range extends to the end of the underlying ts, at the time of construction)
        Like the view, the sparse_view is an object designed for temporary use.
        Unlike the view, the sparse_view contains elements which are guaranteed to be non-empty
        in the underlying ts. So for "sparse" data where many timepoints tend to have empty entries,
        the sparse_view avoids having to linearly check each element for emptiness when iterating.
        Likewise, the sparse_view is guaranteed to be non-empty

        Unlike the view, construction takes time linear in the number of elements _marked present,
        and space is occupied by the map.
        But no data is copied from the source ts - only iterators.

        sparse_view also supports a few operations in addition to what view offers:
        TODO
    */
    class sparse_view {


        typename std::map<timepoint_t, typename std::deque<std::optional<T>>::const_iterator> 
            map;

        typename std::map<timepoint_t, typename std::deque<std::optional<T>>::const_iterator>
            ::const_iterator 
            _cursor;

        public:
        sparse_view(const ts& src, const std::optional<timepoint_t>& _tp = std::nullopt) {
            auto tp = _tp.value_or(src._first_tp);

            if (src.is_empty()) {
                throw std::invalid_argument(
                    "sparse_view cannot be empty, but provided `ts` is empty"
                );
            }

            // validate:
            // ensure our starting point is beyond the starting point of the underlying ts, and 
            // also no further than its last element 
            src.at(tp);

            // relative index inside the underlying ts deque that corresponds to our timepoint_t 
            // starting point
            auto offset = tp - src._first_tp;

            auto begin = src.seq.begin();

            // the sparse_view copies iterators from the source ts, not actual data
            // so this relies on the continued validity of the ts's underlying deque iterators
            auto do_insert = [this, &src](uintmax_t index) {
                this->map.insert(this->map.end(), {
                    src._first_tp + index,
                    src.seq.begin() + index
                });
            };


            // process only those elements which are marked
            if (src.mark_mode == mark_mode_t::MARK_PRESENT) {
                for (auto& index : src._marked) {
                    if (index >= offset) {
                        do_insert(index);
                    }
                }
            } 
            // process only those elements which are not marked
            else if (src.mark_mode == mark_mode_t::MARK_MISSING) {
                auto _marked_it = src._marked.begin();
                // find the first _marked (missing) element that lies beyond our initial offset
                while (_marked_it != src._marked.end() && *_marked_it < offset) {
                    _marked_it++;
                }

                // starting at the initial offset in the underlying ts, add all the elements
                // that are not _marked (missing). when a _marked element is encountered, skip
                // it and begin checking against the next _marked element.
                // so we traverse src.seq and _marked in tandem, with our position in
                //  src.seq never exceeding the value of the next element in _marked
                for (int i = offset; i < src.seq.size(); i++) {
                    if (_marked_it != src._marked.end() && i == *_marked_it) {
                        _marked_it++;
                        continue;
                    } else {
                        do_insert(i);
                    }
                }
            }

            // 
            if (this->map.empty()) {
                throw std::invalid_argument(
                    std::string("sparse_view cannot be empty, but all values in the ") +
                    std::string("provided `ts` are marked as missing starting at time=")+ 
                    std::to_string(tp.to_numeric())
                );
            }

            this->_cursor = this->map.begin();
        }

        sparse_view(const sparse_view&) = default;

        // returns first and last timepoint_t (inclusive) contained by the sparse_view
        const std::pair<timepoint_t, timepoint_t>
        bounds() {
            return {
                (*(this->map.begin())).first,
                (*(std::prev(this->map.end()))).first
            };
        }

        const timepoint_t cursor() const {
            auto element = *(this->_cursor);
            return element.first;
        }
        void reset_cursor() {
            this->_cursor = this->map.begin();
        }

        void seek_to(const timepoint_t& tp) {
            auto it = this->map.find(tp);
            if (it == this->map.end()) {
                throw std::out_of_range("sparse_view::seek_to: timepoint not found");
            }
            this->_cursor = it;
        }

        // TODO guarantee that we never reach map.end()
        auto operator+=(uintmax_t period) {
            this->_cursor += period;
            return *this;
        }
        auto operator++() {
            ++this->_cursor;
            return *this;
        }
        auto operator++(int) {
            this->operator++();
        }

        T value() {
            auto [t,x] = *(this->_cursor);
            return x->value();
            // std::bad_optional_access should never happen since we only consider elements 
            // with non-nullopt values during construction
        }
        T read() { return this->value(); }

        T operator*() {
            return this->value();
        }

    };

    /*  The view class     
        TODO documentation
        cursor represents next unread element

    */
    class view {
        typename std::deque<std::optional<T>>::const_iterator iter;
        timepoint_t _cursor;
        std::pair<timepoint_t, std::optional<timepoint_t>> _bounds;

        public:

        // the timepoint_t is the "current point in time" - point in time that data
        // will be read from
        view(
            const ts& src, const std::optional<timepoint_t>& _tp, 

            // constraint where the view is able to navigate to / read from
            // without this, the view can read beyond the beginning or end of the underlying ts
            //
            // if no _bounds are provided, they will default to the beginning and end of the 
            // underlying ts. 
            // if the pair is given, and the second element is nullopt, there will be no upper
            //  bound; if the first element is nullopt, it will default to the beginning of the
            //  ts, since there is never any possibility of navigating beyond that point (the 
            //  deque used by the ts will only ever grow forwards in time, never backwards)
            std::optional<std::pair<std::optional<timepoint_t>, std::optional<timepoint_t>> > 
            _bounds = std::nullopt
        ) {
            auto tp = _tp.value_or(src._first_tp);

            // check validity - will throw out_of_range otherwise
            src.at(tp);

            auto _bounds_tmp = _bounds.value_or(std::pair<timepoint_t, std::optional<timepoint_t>> { 
                src._first_tp,
                src._first_tp + src.size() - 1
            });

            if (! _bounds_tmp.first.has_value()) 
                _bounds_tmp.first = src._first_tp;
            
            this->_bounds.first = _bounds_tmp.first.value();
            this->_bounds.second = _bounds_tmp.second;

            this->iter = src.seq.begin() + (tp - src._first_tp);

            // cursor is initialized to 0
        }

        view(const view&) = default;

        // returns first and last timepoint_t (inclusive) contained by the view
        const std::pair<timepoint_t, std::optional<timepoint_t>>
        bounds() {
            return this->_bounds;
        }

        bool check_bounds(const timepoint_t& tp) {
            return 
                tp >= this->_bounds.first 
                && (this->_bounds.second.has_value() 
                    ? this->_bounds.second.value() < tp
                    : true) // nullopt upper limit means no limit
            ;
        }

        const timepoint_t& cursor() {
            return this->_cursor;
        }


        // seeking in a view (not sparse_view) is linear
        void seek_to(const timepoint_t& tp) {
            if (!check_bounds(tp)) {
                std::ostringstream e;
                e << "seek_to: check_bounds failed tp=" << std::to_string(tp.to_numeric());
                throw std::out_of_range(e.str());
            }

            while (tp > this->_cursor) {
                ++this->iter;
                ++this->_cursor;
            }
        }

        auto operator+=(uintmax_t period) {
            this->iter += period;
            this->_cursor += period;
            return *this;
        }
        auto operator++() {
            ++this->iter;
            ++this->_cursor;
            return *this;
        }
        auto operator++(int) {
            this->operator++();
        }

        T value() {
            auto x = *(this->iter);
            if (x.has_value()) {
                return x.value();
            } else {
                throw std::bad_optional_access();
            }
        }

        T operator*() {
            return this->value();
        }
        
        bool has_value() {
            return (*(this->iter)).has_value();
        }
    };

    view&& get_view(const timepoint_t& tp) {
        return x(*this, tp);
    }
};

#endif