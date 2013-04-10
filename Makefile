CXXFLAGS ?= -O2 -g

all: apt-show-versions

apt-show-versions: apt-show-versions.cc
	$(CXX) -Wall -Wextra -pedantic -std=c++11 $(CXXFLAGS) \
		$(CPPFLAGS) $(LDFLAGS) -lapt-pkg -o $@ $<

install: all
	install -D -m755 -oroot -groot apt-show-versions $(DESTDIR)/usr/bin/apt-show-versions

clean:
	rm -f apt-show-versions
