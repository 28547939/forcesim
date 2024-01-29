

#include "../types.h"

#include <iostream>
#include <optional>

int main(int argc, char* argv[]) {

	timepoint_t x;
	timepoint_t y;
	x++;

	// TODO - test the entire timepoint_t interface

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
