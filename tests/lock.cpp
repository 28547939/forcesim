


#include <map>
#include <mutex>
#include <iostream>
#include <memory>

#include "../Market.h"

/*
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
*/

int main(int argc, char* argv[]) {

	Market::Market m;


	std::unique_lock U { m.api_mtx_ };

	std::lock_guard<std::recursive_mutex> L(m.api_mtx_);

/*
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
	*/


}
