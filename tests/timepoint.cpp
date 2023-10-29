

#include "../types.h"

#include <iostream>
#include <optional>

struct A {};

struct AR {
	const std::unique_ptr<A> a;
};


int main(int argc, char* argv[]) {

	std::pair<int, AR> p(0,  { {} });

	timepoint_t x;
	timepoint_t y;
	x++;

	x += 5;

	assert(x > y);

	std::cout << x << "\n";
	std::cout << (x-y) << "\n";

	std::optional<timepoint_t> z(0);
	(*z)++;

	std::unique_ptr<int> a(new int(0));
	(*a)++;


	std::cout << (x % (const int) 2) << "\n";

	assert(x % 2 != 1);

}
