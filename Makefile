CXX ?= g++
CXXFLAGS = -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -Icore/include -Itests -fsanitize=address,undefined -pthread

test: build/test_main
	./build/test_main

build/test_main: tests/test_main.cpp tests/toy_sim.hpp $(wildcard core/include/prism/*.hpp)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/test_main.cpp -o $@

clean:
	rm -rf build
