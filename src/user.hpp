#ifndef USER_HPP
#define USER_HPP

#include <string>
#include <vector>

struct Location {
	std::string hostname;
	std::string port;
};

class User {
public:
	User(const std::string&, const std::string&);
	User(const std::string&, const std::string&, const std::string&);
	void addFriend(const std::string&);
	void setAddressInfo(const std::string&, const std::string&);
	Location getAddressInfo() const;
	bool hasFriend(const std::string&) const;
	std::string infoToString() const;
	std::string getUsername() const;
	std::string getPassword() const;
private:
	std::string username;
	std::string password;
	Location address;
	std::vector<std::string> friend_list;
};

#endif