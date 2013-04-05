apt-show-versions: apt-show-versions.cc
	$(CXX) -Wall -Wextra -pedantic -std=c++11 $(CFLAGS) $(LDFLAGS) -lapt-pkg -o $@ $<
