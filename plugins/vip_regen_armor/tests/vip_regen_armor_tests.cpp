// vip_regen_armor unit tests — the engine-independent part: the "armor" group value parsing
// (default, absolute, the original's "+N" quirk (it erases two characters), and the values that
// are not numbers - the original's std::stoi threw there).
//
// Run by ../../test.sh (inside the Steam Runtime image, cwd = build/tests).

#include "armor_logic.hpp"

#include <cstdio>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void CheckEq(int actual, int expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %d, got %d)\n", what.c_str(), expected, actual);
	}
}

void CheckEq(bool actual, bool expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %d, got %d)\n", what.c_str(), expected ? 1 : 0, actual ? 1 : 0);
	}
}

int Parse(const std::string& value, bool& ok) {
	int maxArmor = 0;
	ok = vip_regen_armor::ParseMaxArmor(value, maxArmor);
	return maxArmor;
}

void TestMaxArmor() {
	using vip_regen_armor::kDefaultMaxArmor;

	bool ok = false;
	CheckEq(Parse("", ok), kDefaultMaxArmor, "an empty value keeps 100");
	CheckEq(ok, true, "an empty value parses");
	CheckEq(Parse("100", ok), 100, "an absolute value is used as such");
	CheckEq(Parse("150", ok), 150, "an absolute value above 100");
	CheckEq(Parse("1", ok), 1, "a low absolute value");

	// The original's two-character erase: "+50" loses the '+' *and* the first digit.
	CheckEq(Parse("+50", ok), 100, "the original's +50 quirk keeps 100");
	CheckEq(Parse("+150", ok), 150, "the original's +150 quirk adds 50");
	CheckEq(Parse("+5", ok), 100, "a one-digit +N contributes nothing");

	CheckEq(Parse("abc", ok), kDefaultMaxArmor, "garbage keeps 100");
	CheckEq(ok, false, "garbage is reported");
	CheckEq(Parse("+", ok), kDefaultMaxArmor, "a lone + keeps 100");
	CheckEq(ok, false, "a lone + is reported");
	CheckEq(Parse("12abc", ok), 12, "the leading number wins (std::stoi behaviour)");
}

} // namespace

int main() {
	std::printf("vip_regen_armor tests\n");
	TestMaxArmor();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
