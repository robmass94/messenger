#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "user.hpp"
#include "utils.hpp"

void allowConnections();
void *handleConnections(void*);
void *handleFriend(void*);
void *handleStdin(void*);
void *handleServer(void*);
void closeLocalSockets();
void terminateFriendThreads();
void exitHandler();
void termination_handler(int);
int getUserFd(const std::string&);
bool hasFriend(const std::string&);
std::string getFriendHostname(const std::string&);
std::string getFriendPort(const std::string&);
void removeFriendInfo(const std::string&);
void removeConnectedFriend(const int&);
bool hasInviteFrom(const std::string&);
void removeInviteFrom(const std::string&);
bool hasSentInviteTo(const std::string&);
void removeSentInviteTo(const std::string&);
void displayHelp();

bool logged_in;
int local_socket;
int server_socket;
std::string client_username;
std::string server_hostname;
std::vector<User*> friend_info;
std::vector<std::string> received_invites;
std::vector<std::string> sent_invites;
std::map<int, std::string> connected_friends;
std::map<int, pthread_t> connected_threads;
pthread_t connection_thread;
pthread_t stdin_thread;
pthread_t server_thread;
pthread_attr_t joined_thread_attr;
pthread_attr_t detached_thread_attr;
pthread_mutex_t friend_info_mutex;
pthread_mutex_t received_invites_mutex;
pthread_mutex_t sent_invites_mutex;
pthread_mutex_t connected_friends_mutex;
pthread_mutex_t connected_threads_mutex;

int main(int argc, char *argv[])
{
	if (argc != 3) {
		std::cerr << "usage: ./messenger_client server_hostname server_port\n";
		exit(EXIT_FAILURE);
	}

	signal(SIGINT, termination_handler);

	struct addrinfo hints;
	struct addrinfo *info;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	if (getaddrinfo(argv[1], argv[2], &hints, &info) != 0) {
		std::cerr << "Failed to get server address information\n";
		exit(EXIT_FAILURE);
	}

	if ((server_socket = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) < 0) {
		std::cerr << "Failed to create client-to-server socket\n";
		exit(EXIT_FAILURE);
	}

	if (connect(server_socket, info->ai_addr, info->ai_addrlen) < 0) {
		std::cerr << "Failed to connect to " << argv[1] << " on port " << argv[2] << '\n';
		exit(EXIT_FAILURE);
	}

	std::cout << "You are now connected to " << argv[1] << " on port " << argv[2] << ". Enter \"help\" for a list of commands.\n";

	logged_in = false;
	server_hostname = argv[1];

	if (pthread_mutex_init(&friend_info_mutex, nullptr) != 0) {
		std::cerr << "Failed to initialize friend_info_mutex\n";
		exit(EXIT_FAILURE);
	}

	if (pthread_mutex_init(&received_invites_mutex, nullptr) != 0) {
		std::cerr << "Failed to initialize received_invites_mutex\n";
		exit(EXIT_FAILURE);
	}

	if (pthread_mutex_init(&sent_invites_mutex, nullptr) != 0) {
		std::cerr << "Failed to initialize sent_invites_mutex\n";
		exit(EXIT_FAILURE);
	}

	if (pthread_mutex_init(&connected_friends_mutex, nullptr) != 0) {
		std::cerr << "Failed to initialize connected_friends_mutex\n";
		exit(EXIT_FAILURE);
	}

	if (pthread_mutex_init(&connected_threads_mutex, nullptr) != 0) {
		std::cerr << "Failed to initialize connected_threads_mutex\n";
		exit(EXIT_FAILURE);
	}

	pthread_attr_init(&joined_thread_attr);
	pthread_attr_setdetachstate(&joined_thread_attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_init(&detached_thread_attr);
    pthread_attr_setdetachstate(&detached_thread_attr, PTHREAD_CREATE_DETACHED);

	if (pthread_create(&stdin_thread, &joined_thread_attr, handleStdin, nullptr) != 0) {
		std::cerr << "Failed to create thread for standard input\n";
		exit(EXIT_FAILURE);
	}

	if (pthread_create(&server_thread, &joined_thread_attr, handleServer, nullptr) != 0) {
		std::cerr << "Failed to create thread for server messages\n";
		exit(EXIT_FAILURE);
	}

	pthread_join(stdin_thread, NULL);
	pthread_join(server_thread, NULL);

	return EXIT_SUCCESS;
}

void allowConnections()
{
	struct sockaddr_in local_address;
	socklen_t local_address_length;
	struct addrinfo hints;
	struct addrinfo *info;
	char buffer[256];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;
	
	// create socket for other clients to connect to
	if ((local_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		std::cerr << "Failed to create client's local socket\n";
		exit(EXIT_FAILURE);
	}

	memset(&local_address, 0, sizeof(local_address));
	local_address.sin_family = AF_INET;
	local_address.sin_port = htons(0);
	local_address.sin_addr.s_addr = htonl(INADDR_ANY);

	local_address_length = sizeof(local_address);

	if (bind(local_socket, (struct sockaddr *)&local_address, local_address_length) < 0) {
		std::cerr << "Failed to bind address to client's local socket\n";
		exit(EXIT_FAILURE);
	}

	if (listen(local_socket, 5) < 0) {
		std::cerr << "Failed to set client's local socket as passive\n";
		exit(EXIT_FAILURE);
	}

	if (getsockname(local_socket, (struct sockaddr *)&local_address, &local_address_length) < 0) {
		std::cerr << "Failed to get address to which client's local socket is bound\n";
		exit(EXIT_FAILURE);
	}

	char hostname[256];

	if (gethostname(hostname, sizeof(hostname)) < 0) {
		std::cerr << "Failed to get client's hostname\n";
		exit(EXIT_FAILURE);
	}

	if (getaddrinfo(hostname, nullptr, &hints, &info) != 0) {
		std::cerr << "Failed to get client address information\n";
		exit(EXIT_FAILURE);
	}

	// send client's address information to server
	snprintf(buffer, sizeof(buffer), "LOCATION %s %d", info->ai_canonname, ntohs(local_address.sin_port));
	write(server_socket, buffer, sizeof(buffer));

	// create thread for accepting connections from friends
	if (pthread_create(&connection_thread, &detached_thread_attr, handleConnections, nullptr) != 0) {
		std::cerr << "Failed to create thread for accepting connections\n";
		exit(EXIT_FAILURE);
	}
}

void *handleConnections(void *arg)
{
	int new_socket;
	struct sockaddr_in new_addr;
	socklen_t new_addr_len = sizeof(new_addr);
	pthread_t new_thread;

	while (true) {
		if ((new_socket = accept(local_socket, (struct sockaddr *)&new_addr, &new_addr_len)) >= 0) {
			if (pthread_create(&new_thread, &detached_thread_attr, handleFriend, (void*)&new_socket) != 0) {
				continue;
			}
			pthread_mutex_lock(&connected_threads_mutex);
			connected_threads.insert(std::make_pair(new_socket, new_thread));
			pthread_mutex_unlock(&connected_threads_mutex);
		}
	}
}

void *handleFriend(void *sock)
{
	int socket_fd = *(int*)sock;
	char response[256];
	while (true) {
		if (read(socket_fd, response, sizeof(response)) > 0) {
			std::istringstream strm(response);
			std::string type;
			strm >> type;
			pthread_mutex_lock(&connected_friends_mutex);
			if (type == "USER") {
				// newly connected friend is informing client of username
				std::string username;
				strm >> username;
				connected_friends.insert(std::make_pair(socket_fd, username));
			} else {
				// just a regular message
				std::cout << '[' << connected_friends[socket_fd] << "]: " <<  response << '\n';
			}
			pthread_mutex_unlock(&connected_friends_mutex);
		}
	}
	return nullptr;
}

void *handleStdin(void *arg)
{
	char buffer[256];
	std::string input;
	while (true) {
		std::getline(std::cin, input);
		trimString(input);
		std::istringstream strm(input);
		std::string command;
		strm >> command;
		if (!logged_in) {
			if (command == "register") {
				// register with server
				std::string username;
				std::string password;
				std::cout << "Username: ";
				std::cin >> username;
				std::cout << "Password: ";
				std::cin >> password;

				snprintf(buffer, sizeof(buffer), "REGISTER %s %s", username.c_str(), createHash(password));
				write(server_socket, buffer, sizeof(buffer));
			} else if (command == "login") {
				// login to server
				std::string username;
				std::string password;
				std::cout << "Username: ";
				std::cin >> username;
				std::cout << "Password: ";
				std::cin >> password;

				snprintf(buffer, sizeof(buffer), "LOGIN %s %s", username.c_str(), createHash(password));
				write(server_socket, buffer, sizeof(buffer));
			} else if (command == "help") {
				displayHelp();
			} else if (command == "exit") {
				strncpy(buffer, "EXIT", sizeof(buffer));
				write(server_socket, buffer, sizeof(buffer));
				close(server_socket);
				exit(EXIT_SUCCESS);
			} else {
				std::cout << "Unrecognized command\n";
			}
		} else {
			if (command == "message") {
				std::string username;
				std::string message;
				strm >> username;
				strm.ignore();
				getline(strm, message);

				if (username.empty() || message.empty()) {
					std::cout << "Syntax: message [friend username] [message]\n";
					continue;
				}

				if (username == client_username) {
					std::cout << "You can't message yourself\n";
					continue;
				}

				pthread_mutex_lock(&connected_friends_mutex);
				// file descriptor to write to
				int friend_fd = getUserFd(username);
				if (friend_fd < 0) {
					// not connected, need to first establish connection
					pthread_mutex_lock(&friend_info_mutex);
					std::string friend_hostname = getFriendHostname(username);
					std::string friend_port = getFriendPort(username);
					pthread_mutex_unlock(&friend_info_mutex);
					if (friend_hostname != "NOT FOUND" && friend_port != "NOT FOUND") {
						struct addrinfo hints;
						struct addrinfo *info;
						int new_socket;

						memset(&hints, 0, sizeof(hints));
						hints.ai_family = AF_INET;
						hints.ai_socktype = SOCK_STREAM;
						hints.ai_flags = AI_CANONNAME;

						if (getaddrinfo(friend_hostname.c_str(), friend_port.c_str(), &hints, &info) != 0) {
							std::cout << "Failed to get address information of friend " << username << '\n';
							continue;
						}
						if ((new_socket = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) < 0) {
							std::cout << "Failed to create socket to friend " << username << '\n';
							continue;
						}
						if (connect(new_socket, info->ai_addr, info->ai_addrlen) < 0) {
							std::cout << "Failed to establish connection with friend " << username << '\n';
							continue;
						}

						friend_fd = new_socket;

						// let friend know of username
						sprintf(buffer, "USER %s", client_username.c_str());
						write(friend_fd, buffer, sizeof(buffer));

						// create thread to handle messages from newly connected friend
						pthread_t new_thread;
						if (pthread_create(&new_thread, &detached_thread_attr, handleFriend, (void*)&friend_fd) != 0) {
							continue;
						}
						pthread_mutex_lock(&connected_threads_mutex);
						connected_threads.insert(std::make_pair(friend_fd, new_thread));
						pthread_mutex_unlock(&connected_threads_mutex);
						connected_friends.insert(std::make_pair(friend_fd, username));
					} else {
						std::cout << "Friend " << username << " not found\n";
						continue;
					}
				}
				pthread_mutex_unlock(&connected_friends_mutex);
				// send the message
				strncpy(buffer, message.c_str(), sizeof(buffer));
				write(friend_fd, buffer, sizeof(buffer));
			} else if (command == "invite") {
				std::string username;
				std::string message;
				strm >> username;
				strm.ignore();
				getline(strm, message);
				
				if (username.empty()) {
					std::cout << "Syntax: invite [username] [optional message]\n";
					continue;
				}

				if (username == client_username) {
					std::cout << "You can't invite yourself\n";
					continue;
				}

				// check if this user is already a friend
				pthread_mutex_lock(&friend_info_mutex);
				if (hasFriend(username)) {
					std::cout << "You are already friends with " << username << '\n';
					continue;
				}
				pthread_mutex_unlock(&friend_info_mutex);

				// check if there is a pending invite to this user
				pthread_mutex_lock(&sent_invites_mutex);
				if (hasSentInviteTo(username)) {
					std::cout << "You have already sent an invite to " << username << '\n';
					continue;
				}
				pthread_mutex_unlock(&sent_invites_mutex);

				// check if there is a pending invite from this user
				pthread_mutex_lock(&received_invites_mutex);
				if (hasInviteFrom(username)) {
					std::cout << "You have a pending invite from " << username << '\n';
					continue;
				}
				pthread_mutex_unlock(&received_invites_mutex);

				snprintf(buffer, sizeof(buffer), "INVITE %s %s", username.c_str(), message.c_str());
				write(server_socket, buffer, sizeof(buffer));
				pthread_mutex_lock(&sent_invites_mutex);
				sent_invites.push_back(username);
				pthread_mutex_unlock(&sent_invites_mutex);
			} else if (command == "accept") {
				std::string username;
				std::string message;
				strm >> username;
				strm.ignore();
				getline(strm, message);
				
				if (username.empty()) {
					std::cout << "Syntax: accept [username] [optional message]\n";
					continue;
				}

				pthread_mutex_lock(&received_invites_mutex);
				if (hasInviteFrom(username)) {
					snprintf(buffer, sizeof(buffer), "INVITE_ACCEPT %s %s", username.c_str(), message.c_str());
					write(server_socket, buffer, sizeof(buffer));
					removeInviteFrom(username);
				} else {
					std::cout << "You have not received an invite from " << username << " to accept\n";
				}
				pthread_mutex_unlock(&received_invites_mutex);
			} else if (command == "logout") {
				strncpy(buffer, "LOGOUT", sizeof(buffer));
				write(server_socket, buffer, sizeof(buffer));
				// terminate connection and friend threads
				pthread_cancel(connection_thread);
				terminateFriendThreads();
				// only close local sockets, keep server socket open
				closeLocalSockets();
				// clear friend information
				friend_info.clear();
				// clear online friends
				connected_friends.clear();
				// clear invites
				received_invites.clear();
				sent_invites.clear();
				// reset username
				client_username = "";
				std::cout << "You have logged out of " << server_hostname << '\n';
				logged_in = false;
			} else if (command == "help") {
				displayHelp();
			} else {
				std::cout << "Unrecognized command\n";
			}
		}
	}
	return nullptr;
}

void *handleServer(void *arg)
{
	char response[256];
	while (true) {
		if (read(server_socket, response, sizeof(response)) > 0) {
			std::istringstream strm(response);
			std::string type;
			strm >> type;
			if (type == "REGISTER") {
				std::string username;
				int status_code;
				strm >> username >> status_code;
				if (status_code == 200) {
					std::cout << "You have successfully registered as " << username << ". Please login.\n";
				} else {
					std::cout << "Username " << username << " is unavailable. Please choose another.\n";
				}
			} else if (type == "LOGIN") {
				std::string username;
				int status_code;
				strm >> username >> status_code;
				if (status_code == 200) {
					std::cout << "You have successfully logged in as " << username << ". Enter \"help\" for a list of commands.\n";
					logged_in = true;
					client_username = username;
					allowConnections();
				} else {
					std::cout << "Credentials are incorrect, or user " << username << " is already logged in. Try again.\n";
				}
			} else if (type == "LOCATION") {
				std::string username;
				std::string address;
				std::string port;
				strm >> username >> address >> port;
				std::cout << "Friend " << username << " is online\n";
				pthread_mutex_lock(&friend_info_mutex);
				friend_info.push_back(new User(username, address, port));
				pthread_mutex_unlock(&friend_info_mutex);
			} else if (type == "INVITE_FROM") {
				std::string username;
				std::string message;
				strm.ignore();
				strm >> username;
				getline(strm, message);
				std::cout << "You have received an invite from " << username << ": " << message << '\n';
				pthread_mutex_lock(&received_invites_mutex);
				received_invites.push_back(username);
				pthread_mutex_unlock(&received_invites_mutex);
			} else if (type == "INVITE_ACCEPT") {
				std::string username;
				std::string message;
				strm >> username;
				strm.ignore();
				getline(strm, message);
				std::cout << username << " has accepted your invitation: " << message << '\n';
				pthread_mutex_lock(&sent_invites_mutex);
				removeSentInviteTo(username);
				pthread_mutex_unlock(&sent_invites_mutex);
			} else if (type == "INVITE_FAILED") {
				std::string username;
				strm >> username;
				std::cout << "Failed to send invite to " << username << ". User does not exist.\n";
				pthread_mutex_lock(&sent_invites_mutex);
				removeSentInviteTo(username);
				pthread_mutex_unlock(&sent_invites_mutex);
			} else if (type == "SHUTDOWN") {
				std::cout << server_hostname << " has shut down\n";
				exitHandler();
			} else if (type == "TERMINATE" || type == "LOGOUT") {
				std::string username;
				strm >> username;
				std::cout << "Friend " << username << " has logged out\n";
				// remove location information for friend
				pthread_mutex_lock(&friend_info_mutex);
				removeFriendInfo(username);
				pthread_mutex_unlock(&friend_info_mutex);
				pthread_mutex_lock(&connected_friends_mutex);
				// if friend was connected, remove from connections and terminate thread
				int fd = getUserFd(username);
				if (fd != -1) {
					removeConnectedFriend(fd);
				}
				pthread_mutex_unlock(&connected_friends_mutex);
			} else {
				// some other message, just display it
				std::cout << response << '\n';
			}
		}
	}
	return nullptr;
}

void closeLocalSockets()
{
	for (auto itr = connected_friends.begin(); itr != connected_friends.end(); ++itr) {
		close(itr->first);
	}
	close(local_socket);
}

int getUserFd(const std::string &username)
{
	// return file descriptor of friend with username username
	for (auto itr = connected_friends.begin(); itr != connected_friends.end(); ++itr) {
		if (itr->second == username) {
			return itr->first;
		}
	}
	return -1;
}

bool hasFriend(const std::string &username)
{
	for (auto itr = friend_info.begin(); itr != friend_info.end(); ++itr) {
		if ((*itr)->getUsername() == username) {
			return true;
		}
	}
	return false;
}

std::string getFriendHostname(const std::string &username)
{
	for (auto itr = friend_info.begin(); itr != friend_info.end(); ++itr) {
		if ((*itr)->getUsername() == username) {
			return (*itr)->getHostname();
		}
	}
	return "NOT FOUND";
}

std::string getFriendPort(const std::string &username)
{
	for (auto itr = friend_info.begin(); itr != friend_info.end(); ++itr) {
		if ((*itr)->getUsername() == username) {
			return (*itr)->getPort();
		}
	}
	return "NOT FOUND";
}

void removeFriendInfo(const std::string &username)
{
	// remove friend's location information
	for (auto itr = friend_info.begin(); itr != friend_info.end(); ++itr) {
		if ((*itr)->getUsername() == username) {
			delete *itr;
			friend_info.erase(itr);
			break;
		}
	}
}

void removeConnectedFriend(const int &fd)
{
	close(fd);
	connected_friends.erase(fd);
	pthread_cancel(connected_threads[fd]);
	connected_threads.erase(fd);
}

bool hasInviteFrom(const std::string &username)
{
	for (auto itr = received_invites.begin(); itr != received_invites.end(); ++itr) {
		if (*itr == username) {
			return true;
		}
	}
	return false;
}

void removeInviteFrom(const std::string &username)
{
	for (auto itr = received_invites.begin(); itr != received_invites.end(); ++itr) {
		if (*itr == username) {
			received_invites.erase(itr);
			break;
		}
	}
}

bool hasSentInviteTo(const std::string &username)
{
	for (auto itr = sent_invites.begin(); itr != sent_invites.end(); ++itr) {
		if (*itr == username) {
			return true;
		}
	}
	return false;
}

void removeSentInviteTo(const std::string &username)
{
	for (auto itr = sent_invites.begin(); itr != sent_invites.end(); ++itr) {
		if (*itr == username) {
			sent_invites.erase(itr);
			break;
		}
	}
}

void terminateFriendThreads()
{
	for (auto itr = connected_threads.begin(); itr != connected_threads.end(); ++itr) {
		pthread_cancel(itr->second);
	}
	connected_threads.clear();
}

void exitHandler()
{
	if (logged_in) {
		terminateFriendThreads();
		closeLocalSockets();
	}
	close(server_socket);
	pthread_mutex_destroy(&friend_info_mutex);
	pthread_mutex_destroy(&received_invites_mutex);
	pthread_mutex_destroy(&sent_invites_mutex);
	pthread_mutex_destroy(&connected_friends_mutex);
	pthread_mutex_destroy(&connected_threads_mutex);
	exit(EXIT_SUCCESS);
}

void termination_handler(int sig_num)
{
	char terminate_cmd[] = "TERMINATE";
	write(server_socket, terminate_cmd, sizeof(terminate_cmd));
	exitHandler();
}

void displayHelp()
{
	if (!logged_in) {
		std::cout << "register - register as new user\n";
		std::cout << "login - login as user\n";
		std::cout << "exit - exit the program\n";
	} else {
		std::cout << "message [friend username] [message] - send message to friend\n";
		std::cout << "invite [username] [optional message] - send friend invite\n";
		std::cout << "accept [username] [optional message] - accept friend invite\n";
		std::cout << "logout - logout of the server\n";
	}
	std::cout << "help - display this help of commands\n";
}