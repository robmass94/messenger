CXX=g++
CXXFLAGS=-c -std=c++11 -Wall -g

all: messenger_client messenger_server

messenger_client: messenger_client.o user.o utils.o
	$(CXX) -o messenger_client -pthread messenger_client.o user.o utils.o -lcrypt

messenger_server: messenger_server.o user.o utils.o
	$(CXX) -o messenger_server -pthread messenger_server.o user.o utils.o -lcrypt

messenger_client.o: messenger_client.cpp
	$(CXX) $(CXXFLAGS) messenger_client.cpp

messenger_server.o: messenger_server.cpp
	$(CXX) $(CXXFLAGS) messenger_server.cpp

user.o: user.cpp user.hpp
	$(CXX) $(CXXFLAGS) user.cpp

utils.o: utils.cpp utils.hpp
	$(CXX) $(CXXFLAGS) utils.cpp

.PHONY: clean

clean:
	rm -f messenger_client messenger_server *.o
