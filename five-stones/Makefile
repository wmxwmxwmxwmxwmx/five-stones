CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I.
LDFLAGS := -pthread -lmysqlclient -ljsoncpp -lboost_system

all: gobang_server

gobang_server:main.cc
	g++ $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f gobang_server

.PHONY: all clean

