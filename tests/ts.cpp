

#include "../types.h"
#include "../ts.h"

#include <map>
#include <set>
#include <iterator>
#include <iostream>

#include <random>


std::random_device r;
std::mt19937 engine (r());

std::bernoulli_distribution tf(0.5);


// TODO - additional functionality/coverage and CLI arguments


// testing the 'ts' class and the associated 'view' and  'sparse_view' classes (ts.h)
// work in progress

int rand(int max) {
    std::uniform_int_distribution rint(0, max);
    return rint(engine);
}

bool randtf() {
    return tf(engine);
}

struct ts_val {
    // in these tests, we always insert ts_val so that this timepoint_t value is equal to the 
    // position in the ts
    timepoint_t i;

    bool operator==(const ts_val&) const = default;
    auto operator<=> (const ts_val&) const = default;
};

template<typename T>
void print(std::map<timepoint_t, T> x) {
	for (auto& [k,v] : x) {
		std::cout << k.to_numeric() << ": " << std::to_string(v) << "\n";
	}
}

template<typename T>
void print(std::deque<T> x) {
	for (auto v : x) {
		std::cout << v << "\n";
	}
}

int max_ts_size = 1000;
int ts_size = rand(max_ts_size);
timepoint_t ts_first_tp = 0;

typedef std::pair<ts<ts_val>, ts<ts_val>> 
tspair;

// track the elements which are 'marked' in the two TSs for verification
// if true, a value was added, if false, the entry is empty (nullopt)
std::deque<bool> ts1_tfval;
std::deque<bool> ts2_tfval;

// ts tests

// random_ts    create two random sized TSs with random contents, with both mark modes, to be passed to the tests
// tests append, append_at, skip, size
tspair random_ts() {

    ts<ts_val> ts1(0, mark_mode_t::MARK_MISSING);
    ts<ts_val> ts2(0, mark_mode_t::MARK_PRESENT);

    for (int i=0; i < ts_size; ++i) {
        auto v = ts_val { ts_first_tp + i };
        auto optv = [&v](ts<ts_val>& dst, std::deque<bool>& record) { 
            bool tfval = randtf();
            record.push_back(tfval);

            if (tfval == true) {
                dst.append(v);
            } else {
                dst.skip(1);
            }
        };

        optv(ts1, ts1_tfval);
        optv(ts2, ts2_tfval);
    }

    return { ts1, ts2 };

/*
    // try to use generate_n with insert_iterator or similar
    int i = 0;
    std::generate_n(std::back_inserter(ts1), N, [first_tp]() { 
        return ts_val { first_tp + i++ };
    });

    std::generate_n(std::back_inserter(ts2), N, [first_tp](int i) { 
        return ts_val { first_tp + i };
    });
    */

}


// TODO - should be made to work with different values of first_tp
void test_at(const tspair& tsp, int test_count) {
    auto& [ts1, ts2] = tsp;
    
    std::set<std::size_t> ts1_marked;
    std::set<std::size_t> ts2_marked;

    for (auto v : ts1.marked()) {
        ts1_marked.insert(v);
    }

    for (auto v : ts2.marked()) {
        ts2_marked.insert(v);
    }
/*
    std::copy(ts1.marked().begin(), ts1.marked().end(), std::inserter(ts1_marked, ts1_marked.end()));
    std::copy(ts2.marked().begin(), ts2.marked().end(), std::inserter(ts2_marked, ts2_marked.end()));
    */

    for (int i=0; i < test_count; ++i) {
        assert(tsp.first.cursor() == tsp.second.cursor());
        int index = rand(tsp.first.cursor().value().to_numeric());

        timepoint_t tp = ts_first_tp + index;

        auto optv = tsp.first.at(tp);
        assert(optv.has_value() == ts1_tfval[index]);
        assert(ts1_tfval[index] != ts1_marked.contains(index));

        optv = tsp.second.at(tp);
        assert(optv.has_value() == ts2_tfval[index]);
        assert(ts2_tfval[index] == ts2_marked.contains(index));
    }

    // each item should throw out_of_range
    std::deque<std::function<void(void)>> exc_test = {
        [&tsp]() { tsp.first.at(-1);},
        [&tsp]() { tsp.second.at(-1);},
        [&tsp]() { tsp.first.at(ts_size);},
        [&tsp]() { tsp.second.at(ts_size);},
    };

    std::for_each(exc_test.begin(), exc_test.end(), [](auto& f) {
        try {  
            f();
            throw std::exception();  // only throws if out_of_range was not thrown
        } catch (std::out_of_range &e) {}
    });
}

// test_pop

void test_pop(const tspair& tsp) {
    // make copies
    auto ts1 = tsp.first;
    auto ts2 = tsp.second;

    assert(ts1.cursor() == ts2.cursor());

    for (timepoint_t i=ts1.cursor().value(); i > ts2.first_tp(); --i) {
        assert(ts1.pop() == i);
        assert(ts2.pop() == i);
    }

    assert(ts1.pop() == ts1.first_tp());
    assert(ts2.pop() == ts2.first_tp());

    assert(ts1.size() == 0);
    assert(ts2.size() == 0);

    // originals should not have been modified
    assert(tsp.first.size() != 0);
    assert(tsp.second.size() != 0);
}

// test_delete_until
void test_delete(const tspair& tsp) {
    // make copies
    auto ts1 = tsp.first;
    auto ts2 = tsp.second;

    auto first = ts1.first_tp();
    auto last = ts1.cursor().value();
    auto k = timepoint_t { rand(last-first) + first };

    ts1.delete_until(k);

    assert(ts1.first_tp() == k);
    assert(ts1.cursor() == last);

    // TODO - test_at with first_tp != 0
    //tspair new_tpair = { ts1, ts2 };
    //test_at(new_tpair, 1000);
}


// sparse_view tests

//void test_empty_

// test that each value that should be set in the sparse_view is actually set, and to
// the correct value
// 
void test_seek(ts<ts_val> x, int test_count) {
    auto sv = ts<ts_val>::sparse_view(x);

    auto [b1, b2] = sv.bounds();

    auto testval = [&x, &sv](timepoint_t& tp) {

        auto optv = x.at(tp);
        if (optv.has_value()) {
            assert(optv.value() == sv.value());
            assert(optv.value() == ts_val { tp });
            return true;
        } else {
            return false;
        }
    };

    // 
    for (timepoint_t tp=b1; tp <= b2; ++tp) {

        if (testval(tp)) {
            // if we have just tested a non-nullopt value, then that value is present in the 
            // sparse_view and we proceed to the next value
            ++sv;
        } else {
            // the sparse_view must not be defined here
            try { sv.seek_to(tp); throw std::exception(); } 
            catch (std::out_of_range&) {}
        }
    }

    auto sv_size = (b2 - b1);


    rand(sv_size);

}

// view ests

//test_seek -same as sparse

// test_unlimited



int main(int argc, char* argv[]) {

    auto tpair = random_ts();

    auto [ts1, ts2] = tpair;

    test_at(tpair, 1000);
    test_pop(tpair);
    test_delete(tpair);

    test_seek(ts1, 1000);


}


/*
int main(int argc, char* argv[]) {

	ts<int> ts_with_no_args;

	std::map<std::string, ts<int>> map_of_ts;

	map_of_ts.insert({ "test", {} });

	map_of_ts["test"].append(1);

	ts<int> x(0);

	int N = 10;

	for (int i = 0; i < N; i++) {
		x.append(i);
		x.skip(1);

		x.append(0);
	}


	auto m = x.to_map();

	print(*m);

	auto sv = ts<int>::sparse_view(x);

	auto [a,b] = sv.bounds();

	std::cout << a.to_numeric() << " - " << b.to_numeric() << "\n";


	print(x.marked());


}
*/
