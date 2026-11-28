#pragma once

#include "../types.hpp"

namespace users {
	
	enum class Roles {
		USER,
		ADMIN,
		SUPERADMIN
	};
	
	struct UserHandle {
		
		uint16_t slot;
		uint16_t generation;
	};
	
	
	struct User {
		
		uint16_t userId;
		char userName[32];
		char hashPassword[128];
		Roles role;
		
	};
}
