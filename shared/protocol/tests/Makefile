# Host-side build of the wire-protocol unit tests.
# Run from this directory:  make && ./test_frame

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -g -I..

OBJS = test_frame.o frame.o

test_frame: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

test_frame.o: test_frame.cpp ../frame.h ../packets.h
	$(CXX) $(CXXFLAGS) -c -o $@ test_frame.cpp

frame.o: ../frame.cpp ../frame.h ../packets.h
	$(CXX) $(CXXFLAGS) -c -o $@ ../frame.cpp

run: test_frame
	./test_frame

clean:
	rm -f $(OBJS) test_frame

.PHONY: run clean
