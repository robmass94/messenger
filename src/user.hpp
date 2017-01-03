#ifndef USER_H
#define USER_H

#include <string>
#include <vector>

class User {
public:
	User(const std::string&, const std::string&);
	User(const std::string&, const std::string&, const std::string&);
	void addFriend(const std::string&);
	void setAddressInfo(const std::string&, const std::string&);
	bool hasFriend(const std::string&) const;
	std::string infoToString() const;
	std::string getUsername() const;
	std::string getPassword() const;
	std::string getHostname() const;
	std::string getPort() const;
private:
	std::string username;
	std::string password;
	std::string hostname;
	std::string port;
	std::vector<std::string> friend_list;
};

#endif