#include "users.hpp"
#include "user_manager.hpp"

struct UserSlot {
	
	User currentUser;
	bool isActive;
	uint16_t generation;
};

namespace users {

	bool createUser(const char* userName, const char* hashPassword) {
		
		users::User user;
		user.userId = 0;
		user.userName = userName;
		user.hashPassword = hashPassword;
		user.role = users::Roles::USER;
	}
}