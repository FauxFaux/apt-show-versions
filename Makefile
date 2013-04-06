CXXFLAGS ?= -O2 -g

apt-show-versions: apt-show-versions.cc
	$(CXX) -Wall -Wextra -pedantic -std=c++11 $(CXXFLAGS) $(LDFLAGS) -lapt-pkg -o $@ $<
