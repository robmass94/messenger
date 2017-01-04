#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "user.hpp"
#include "utils.hpp"

void loadUserFile(char*);
void *handleConnection(void*);
std::shared_ptr<User> getUserInfo(const std::string&);
int getUserFd(const std::string&);
void writeToUserFile();
void createFriendship(const std::string&, const std::string&);
bool isUsernameAvailable(const std::string&);
bool isUserLoggedIn(const std::string&);
bool isCorrectLogin(const std::string&, const std::string&);
void termination_handler(int);

int server_socket;
std::set<int> all_connections;
std::vector<std::shared_ptr<User>> user_info;
std::map<int, std::shared_ptr<User>> online_users;
std::string user_filename;
pthread_mutex_t connections_mutex;
pthread_mutex_t user_info_mutex;
pthread_mutex_t online_users_mutex;

int main(int argc, char *argv[])
{
	if (argc != 3) {
		std::cerr << "usage: ./messenger_server user_info_file port\n";
		exit(EXIT_FAILURE);
	}

	signal(SIGINT, termination_handler);

	int port = atoi(argv[2]);
	socklen_t address_length;
	char hostname[256];
	struct addrinfo hints;
	struct addrinfo *info;
	struct sockaddr_in address;

	loadUserFile(argv[1]);
	user_filename = argv[1];

	if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		std::cerr << "Failed to create server socket\n";
		exit(EXIT_FAILURE);
	}

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address_length = sizeof(address);

	if (bind(server_socket, (struct sockaddr *)&address, address_length) < 0) {
		std::cerr << "Failed to bind host address to server socket\n";
		exit(EXIT_FAILURE);
	}

	if (listen(server_socket, 5) < 0) {
		std::cerr << "Failed to set server socket as passive\n";
		exit(EXIT_FAILURE);
	}

	if (gethostname(hostname, sizeof(hostname)) < 0) {
		std::cerr << "Failed to get server hostname\n";
		exit(EXIT_FAILURE);
	}

	if (getsockname(server_socket, (struct sockaddr *)&address, &address_length) < 0) {
		std::cerr << "Failed to get address to which server socket is bound\n";
		exit(EXIT_FAILURE);
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	if (getaddrinfo(hostname, nullptr, &hints, &info) != 0) {
		std::cerr << "Failed to get server address information\n";
		exit(EXIT_FAILURE);
	}

	std::cout << "Hostname: " << info->ai_canonname << '\n';
	std::cout << "Port: " << ntohs(address.sin_port) << '\n';
	freeaddrinfo(info);

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int client_socket;
	pthread_t new_thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	while (true) {
		if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) >= 0) {
			if (pthread_create(&new_thread, &attr, handleConnection, (void*)&client_socket) != 0) {
				continue;
			}
			pthread_mutex_lock(&connections_mutex);
			all_connections.insert(client_socket);
			pthread_mutex_unlock(&connections_mutex);
		}
	}

	return EXIT_SUCCESS;
}

void loadUserFile(char *u_fname)
{
	std::ifstream user_file(u_fname);

	if (!user_file.is_open()) {
		std::cerr << "Failed to open user information file " << u_fname << '\n';
		exit(EXIT_FAILURE);
	}

	std::string line;

	/*
		Each line of a non-empty user file should be in the format: 

		username|password|friend1;friend2;...;friendN

	*/


	while (getline(user_file, line)) {
		std::istringstream strm(line);
		std::string username;
		std::string password;
		std::string contact;
		getline(strm, username, '|');
		getline(strm, password, '|');
		std::shared_ptr<User> u(new User(username, password));
		while (getline(strm, contact, ';')) {
			u->addFriend(contact);
		}
		user_info.push_back(u);
	}

	user_file.close();
}

void createFriendship(const std::string &user1, const std::string &user2)
{
	for (auto itr = user_info.begin(); itr != user_info.end(); ++itr) {
		if ((*itr)->getUsername() == user1) {
			(*itr)->addFriend(user2);
		} else if ((*itr)->getUsername() == user2) {
			(*itr)->addFriend(user1);
		}
	}
}

void *handleConnection(void *sock)
{
	int socket_fd = *((int*)sock);
	char response[256];
	char buffer[256];

	while (true) {
		if (read(socket_fd, response, sizeof(response)) > 0) {
			std::istringstream strm(response);
			std::string command;
			strm >> command;
			if (command == "REGISTER") {
				std::string username;
				std::string password;
				strm >> username >> password;
				pthread_mutex_lock(&user_info_mutex);
				if (isUsernameAvailable(username)) {
					user_info.push_back(std::make_shared<User>(username, password));
					snprintf(buffer, sizeof(buffer), "REGISTER %s 200", username.c_str());
				} else {
					snprintf(buffer, sizeof(buffer), "REGISTER %s 500", username.c_str());
				}
				pthread_mutex_unlock(&user_info_mutex);
				write(socket_fd, buffer, sizeof(buffer));
			} else if (command == "LOGIN") {
				std::string username;
				std::string password;
				strm >> username >> password;
				pthread_mutex_lock(&user_info_mutex);
				if (isCorrectLogin(username, password) && !isUserLoggedIn(username)) {
					// retrieve stored information about this user, particularly friends
					pthread_mutex_lock(&online_users_mutex);
					online_users.insert(std::make_pair(socket_fd, getUserInfo(username)));
					snprintf(buffer, sizeof(buffer), "LOGIN %s 200", username.c_str());
					std::cout << "Online users: " << online_users.size() << '\n';
					pthread_mutex_unlock(&online_users_mutex);
				} else {
					snprintf(buffer, sizeof(buffer), "LOGIN %s 500", username.c_str());
				}
				pthread_mutex_unlock(&user_info_mutex);
				write(socket_fd, buffer, sizeof(buffer));
			} else if (command == "LOCATION") {
				std::string address;
				std::string port;
				strm >> address >> port;
				char friend_location_buffer[256];
				// store location information
				pthread_mutex_lock(&online_users_mutex);
				online_users[socket_fd]->setAddressInfo(address, port);
				// exchange location information between client and online friends
				std::string username = online_users[socket_fd]->getUsername();
				snprintf(buffer, sizeof(buffer), "LOCATION %s %s %s", username.c_str(), address.c_str(), port.c_str());
				for (auto itr = online_users.begin(); itr != online_users.end(); ++itr) {
					int fd = itr->first;
					if (fd != socket_fd && itr->second->hasFriend(username)) {
						write(fd, buffer, sizeof(buffer));
						std::string friend_username = itr->second->getUsername();
						Location friend_address = itr->second->getAddressInfo();
						snprintf(friend_location_buffer, sizeof(friend_location_buffer), "LOCATION %s %s %s", friend_username.c_str(), friend_address.hostname.c_str(), friend_address.port.c_str());
						write(socket_fd, friend_location_buffer, sizeof(friend_location_buffer));
					}
				}
				pthread_mutex_unlock(&online_users_mutex);
			} else if (command == "INVITE") {
				std::string potential_friend_username;
				std::string message;
				strm >> potential_friend_username;
				strm.ignore();
				getline(strm, message);
				pthread_mutex_lock(&online_users_mutex);
				int other_fd = getUserFd(potential_friend_username);
				if (other_fd < 0) {
					snprintf(buffer, sizeof(buffer), "INVITE_FAILED %s", potential_friend_username.c_str());
					write(socket_fd, buffer, sizeof(buffer));
				} else {
					snprintf(buffer, sizeof(buffer), "INVITE_FROM %s %s", online_users[socket_fd]->getUsername().c_str(), message.c_str());
					write(other_fd, buffer, sizeof(buffer));
				}
				pthread_mutex_unlock(&online_users_mutex);
			} else if (command == "INVITE_ACCEPT") {
				std::string inviter_username;
				std::string message;
				strm >> inviter_username;
				strm.ignore();
				getline(strm, message);
				pthread_mutex_lock(&online_users_mutex);
				int inviter_fd = getUserFd(inviter_username);
				Location inviter_address = online_users[inviter_fd]->getAddressInfo();
				std::string client_username = online_users[socket_fd]->getUsername();
				Location client_address = online_users[socket_fd]->getAddressInfo();
				// let inviter know client has accepted invite
				snprintf(buffer, sizeof(buffer), "INVITE_ACCEPT %s %s", client_username.c_str(), message.c_str());
				write(inviter_fd, buffer, sizeof(buffer));
				// update friend lists
				createFriendship(inviter_username, client_username);
				// send location information of inviter to client
				snprintf(buffer, sizeof(buffer), "LOCATION %s %s %s", inviter_username.c_str(), inviter_address.hostname.c_str(), inviter_address.port.c_str());
				write(socket_fd, buffer, sizeof(buffer));
				// send location information of client to inviter
				snprintf(buffer, sizeof(buffer), "LOCATION %s %s %s", client_username.c_str(), client_address.hostname.c_str(), client_address.port.c_str());
				write(inviter_fd, buffer, sizeof(buffer));
				pthread_mutex_unlock(&online_users_mutex);
			} else if (command == "LOGOUT") {
				// client is logging out, but server will still maintain connection and thread
				pthread_mutex_lock(&online_users_mutex);
				std::string username = online_users[socket_fd]->getUsername();
				online_users.erase(socket_fd);
				// inform client's friends that client has logged out
				snprintf(buffer, sizeof(buffer), "LOGOUT %s", username.c_str());
				for (auto user_itr = online_users.begin(); user_itr != online_users.end(); ++user_itr) {
					if (user_itr->second->hasFriend(username)) {
						write(user_itr->first, buffer, sizeof(buffer));
					}
				}
				printf("Online users: %lu\n", online_users.size());
				pthread_mutex_unlock(&online_users_mutex);
			} else if (command == "EXIT" || command == "TERMINATE") {
				pthread_mutex_lock(&connections_mutex);
				all_connections.erase(socket_fd);
				pthread_mutex_unlock(&connections_mutex);
				// close client's file descriptor
				close(socket_fd);
				pthread_mutex_lock(&online_users_mutex);
				auto client_itr = online_users.find(socket_fd);
				if (command == "TERMINATE" && client_itr != online_users.end()) {
					// if client terminated while logged in, need to inform friends (if any)
					std::string username = client_itr->second->getUsername();
					online_users.erase(client_itr);
					snprintf(buffer, sizeof(buffer), "TERMINATE %s", username.c_str());
					for (auto user_itr = online_users.begin(); user_itr != online_users.end(); ++user_itr) {
						if (user_itr->second->hasFriend(username)) {
							write(user_itr->first, buffer, sizeof(buffer));
						}
					}
				}
				printf("Online users: %lu\n", online_users.size());
				pthread_mutex_unlock(&online_users_mutex);
				// exit thread
				pthread_exit(nullptr);
			}
		}
	}
	return nullptr;
}

std::shared_ptr<User> getUserInfo(const std::string &username)
{
	// retrieve registered user information
	for (auto itr = user_info.begin(); itr != user_info.end(); ++itr) {
		if ((*itr)->getUsername() == username) {
			return *itr;
		}
	}
	// will never get to this point, since a client needs to successfully
	// login (to a registered account) for this function to be called
	return nullptr;
}

int getUserFd(const std::string &username)
{
	for (auto itr = online_users.begin(); itr != online_users.end(); ++itr) {
		if (itr->second->getUsername() == username) {
			return itr->first;
		}
	}
	return -1;
}

bool isUsernameAvailable(const std::string &username)
{
	for (auto itr = user_info.begin(); itr != user_info.end(); ++itr) {
		if ((*itr)->getUsername() == username) {
			return false;
		}
	}
	return true;
}

bool isUserLoggedIn(const std::string &username)
{
	// check if a user with username is already logged in
	for (auto itr = online_users.begin(); itr != online_users.end(); ++itr) {
		if (itr->second->getUsername() == username) {
			return true;
		}
	}
	return false;
}

bool isCorrectLogin(const std::string &username, const std::string &password)
{
	for (auto itr = user_info.begin(); itr != user_info.end(); ++itr) {
		if ((*itr)->getUsername() == username && (*itr)->getPassword() == password) {
			return true;
		}
	}
	return false;
}

void termination_handler(int sig_num)
{
	// write user information to file
	std::ofstream user_file(user_filename);
	for (auto itr = user_info.begin(); itr != user_info.end(); ++itr) {
		user_file << (*itr)->infoToString() << '\n';
	}
	user_file.close();

	// inform clients of shutdown, and close sockets
	char shutdown_cmd[] = "SHUTDOWN";
	for (auto itr = all_connections.begin(); itr != all_connections.end(); ++itr) {
		write(*itr, shutdown_cmd, sizeof(shutdown_cmd));
		close(*itr);
	}

	close(server_socket);

	pthread_mutex_destroy(&connections_mutex);
	pthread_mutex_destroy(&user_info_mutex);
	pthread_mutex_destroy(&online_users_mutex);

	exit(EXIT_SUCCESS);
}