#pragma once
// Engine-free parsing of the group's "armor" value (the MAX ARMOR of the original module):
//
//   empty            -> 100 (default)
//   "150"            -> 150
//   "+50"            -> 100 + <the string after erasing TWO characters> = 100 (the original's
//                       str.erase(0, 2) quirk: it drops the '+' and the first digit)
//   "+150"           -> 150
//   anything else    -> not a number (the caller keeps 100 and logs it; the original's std::stoi
//                       would throw into the engine)

#include <string>
#include <string_view>

namespace vip_regen_armor {

inline constexpr int kDefaultMaxArmor = 100;

// True and *maxArmor set when the value parses (including the empty value -> 100).
inline bool ParseMaxArmor(std::string_view value, int& maxArmor) {
	maxArmor = kDefaultMaxArmor;
	if (value.empty()) {
		return true;
	}
	try {
		if (value.front() == '+') {
			std::string rest(value);
			rest.erase(0, 2); // the original: str(sArmor); str.erase(0, 2)
			maxArmor = kDefaultMaxArmor + std::stoi(rest);
			return true;
		}
		maxArmor = std::stoi(std::string(value));
		return true;
	} catch (...) {
		maxArmor = kDefaultMaxArmor;
		return false;
	}
}

} // namespace vip_regen_armor
