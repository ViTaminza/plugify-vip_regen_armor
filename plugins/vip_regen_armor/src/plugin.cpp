// vip_regen_armor — Plugify port of the Pisex VIP module "VIP_RegenArmor"
// (the original's vip_regen_armor.cpp).
//
// Behaviour kept identical:
//   * features: "RegenArmor" (VIP_BOOL, TOGGLABLE, toggle + display callbacks), "DelayRegenArmor"
//     and "IntervalRegenArmor" (VIP_INT, HIDE).
//   * the armor ceiling comes from the group's "armor" value, parsed exactly like the original:
//     empty -> 100; "150" -> 150; "+50" -> 100 + the value of the string after erasing *two*
//     characters (the original's quirk, so "+50" contributes 0 and "+150" contributes
//     50). A value that is not a number keeps 100 and is logged instead of throwing into the
//     engine (the original's std::stoi could throw).
//   * a "player_hurt" pre-hook: when a VIP with the feature whose pawn is on a real team and alive
//     takes armor damage (dmg_armor != 0), an old timer is removed and a new one starts with the
//     DelayRegenArmor delay.
//   * the timer adds RegenArmor armor per run while below the ceiling and reschedules itself with
//     IntervalRegenArmor; it stops at the ceiling, when the pawn is gone, or on the next hurt.
//   * the toggle callback clears the pending regeneration when switched off and blocks the toggle
//     (the original returned true); the display renders "<name> [<RegenArmor>/<IntervalRegenArmor> s.]".
//   * loaded/disconnect/spawn reset the regeneration flag of the slot.
//
// Port notes:
//   * m_ArmorValue read/write -> s2sdk::GetClientArmor / SetClientArmor (the schema field with its
//     network state change, the same thing the Pisex field wrapper did).
//   * the original stopped the previous timer only while its flag was still set (g_bRegen), so a
//     reset that does not stop (spawn, disconnect, toggle) leaked a live timer whose handle the
//     next start overwrote; the port stops the previous timer unconditionally before starting one.
//   * the original's bounds check let slot 64 through (an off-by-one); this port stops at 63. A
//     slot 64 never exists in CS2, so the behaviour is unchanged.
//   * the "player_hurt" handler resolves the victim's slot with IGameEvent::GetPlayerSlot("userid"),
//     the accessor the s2sdk game-events guide prescribes. The original read the raw "userid"
//     field, i.e. the engine user id, which for a human player equals the slot, so the original
//     worked for the VIP players it serves; GetPlayerSlot is correct for every player.

#include <pisex/pisex.hpp>
#include <plg/plugin.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include "armor_logic.hpp"
#include "vip_api_compat.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

namespace {

// The ported modules keep the original code's unqualified spellings (VIP_INT, TOGGLABLE,
// DISABLED, ...): the consumer wrapper declares them as plain enums in namespace vip.
using namespace vip;

IVIPApi* g_pVIPCore = &IVIPApi::Get();

constexpr int kMaxSlots = 64;
constexpr const char* kFeature = "RegenArmor";
constexpr const char* kDelayFeature = "DelayRegenArmor";
constexpr const char* kIntervalFeature = "IntervalRegenArmor";
constexpr const char* kArmorFeature = "armor";

// The delay/interval come from the group's features. A 0 second delay is FATAL on s2sdk:
// TimerSystem::RunFrame() re-inserts a repeat timer with executeTime = universalTime + delay and
// keeps looping while any timer is due, so a 0 delay stays due forever (the server frame never
// returns -> "WatchDog! Server took too long to process" -> fatal). The shipped groups use
// 3..6 seconds; a misconfigured 0 (or a negative value) is clamped to 50 ms instead of hanging.
constexpr float kMinTimerInterval = 0.05f;

float SafeTimerInterval(float seconds) {
	return seconds > kMinTimerInterval ? seconds : kMinTimerInterval;
}

static_assert(kMinTimerInterval > 0.0f, "a 0 second interval hangs s2sdk's TimerSystem::RunFrame loop");

void LogInfo(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Info, plg::source_location::current());
	}
}

void LogError(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Error, plg::source_location::current());
	}
}

// The "armor" group value of the original: 100 by default, an absolute number, or "+N" which adds
// the number that remains after the original erases two characters. The parser itself lives in
// armor_logic.hpp (shared with the test); this wrapper only adds the log line the original's
// std::stoi would not have produced (it threw instead).
int MaxArmorFromFeature(const char* sArmor) {
	int maxArmor = vip_regen_armor::kDefaultMaxArmor;
	const std::string_view value = sArmor != nullptr ? std::string_view(sArmor) : std::string_view();
	if (!vip_regen_armor::ParseMaxArmor(value, maxArmor)) {
		// This runs from the per-player regeneration timer (every DelayRegenArmor/IntervalRegenArmor
		// run), so a misconfigured group value would otherwise log once every few seconds per
		// regenerating VIP: log each distinct bad value once instead.
		static std::string lastBadValue;
		if (lastBadValue != value) {
			lastBadValue.assign(value);
			LogError(std::string("vip_regen_armor: 'armor' value '") + std::string(value) +
					 "' is not a number - using 100");
		}
	}
	return maxArmor;
}

class RegenArmorPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

private:
	bool Regen(int iSlot);
	void CreateRegenTimer(int iSlot);
	void StopTimer(int iSlot);
	void ResetSlot(int iSlot);
	void OnPlayerHurt(const char* szName, pisex::IGameEvent* pEvent, bool bDontBroadcast);

	std::array<bool, kMaxSlots> m_regen{};
	std::array<pisex::CTimer*, kMaxSlots> m_timers{};
};

RegenArmorPlugin g_regenArmorPlugin;

plg::PluginResult RegenArmorPlugin::OnPluginStart() {
	try {
		// Never fatal: only logs when the s2sdk bindings are missing.
		pisex::Init("vip_regen_armor");

		g_pVIPCore->VIP_RegisterFeature(kFeature, VIP_BOOL, TOGGLABLE, nullptr,
										[this](int iSlot, const char* /*szFeature*/, VIP_ToggleState /*eOldStatus*/,
											   VIP_ToggleState& eNewStatus) {
											if (eNewStatus == DISABLED && iSlot >= 0 && iSlot < kMaxSlots) {
												m_regen[static_cast<std::size_t>(iSlot)] = false;
											}
											return true; // the original blocked the toggle
										},
										[this](int iSlot, const char* szFeature) {
											char buffer[128];
											std::snprintf(buffer, sizeof(buffer), "%s [%i/%i s.]",
														  g_pVIPCore->VIP_GetTranslate(szFeature),
														  g_pVIPCore->VIP_GetClientFeatureInt(iSlot, kFeature),
														  g_pVIPCore->VIP_GetClientFeatureInt(iSlot, kIntervalFeature));
											return std::string(buffer);
										});
		g_pVIPCore->VIP_RegisterFeature(kDelayFeature, VIP_INT, HIDE);
		g_pVIPCore->VIP_RegisterFeature(kIntervalFeature, VIP_INT, HIDE);

		pisex::HookEvent("player_hurt", [this](const char* szName, pisex::IGameEvent* pEvent, bool bDontBroadcast) {
			OnPlayerHurt(szName, pEvent, bDontBroadcast);
		});

		g_pVIPCore->VIP_OnClientLoaded([this](int iSlot, bool /*bIsVIP*/) { ResetSlot(iSlot); });
		g_pVIPCore->VIP_OnClientDisconnect([this](int iSlot, bool /*bIsVIP*/) { ResetSlot(iSlot); });
		g_pVIPCore->VIP_OnPlayerSpawn([this](int iSlot, int /*iTeam*/, bool /*bIsVIP*/) { ResetSlot(iSlot); });

		LogInfo("vip_regen_armor: ready");
	} catch (const std::exception& ex) {
		LogError(std::string("vip_regen_armor: OnPluginStart failed: ") + ex.what());
	} catch (...) {
		LogError("vip_regen_armor: OnPluginStart failed with an unknown exception");
	}
	return {};
}

plg::PluginResult RegenArmorPlugin::OnPluginUpdate(std::chrono::milliseconds /*dt*/) {
	pisex::Poll(); // timer reschedules and event pump
	return {};
}

plg::PluginResult RegenArmorPlugin::OnPluginEnd() {
	for (pisex::CTimer*& timer : m_timers) {
		if (timer != nullptr) {
			pisex::RemoveTimer(timer);
			timer = nullptr;
		}
	}
	pisex::RemoveAllEvents();
	g_pVIPCore->VIP_OnClientLoadedUnregister();
	g_pVIPCore->VIP_OnClientDisconnectUnregister();
	g_pVIPCore->VIP_OnPlayerSpawnUnregister();
	// The vip API has no feature unregister: re-registering the feature without callbacks
	// replaces the core's entry with null delegates, so the core keeps no pointer into this
	// library after it is unloaded (same value/feature type: the menu entry is unchanged).
	g_pVIPCore->VIP_RegisterFeature(kFeature, VIP_BOOL, TOGGLABLE);
	pisex::Shutdown();
	return {};
}

bool RegenArmorPlugin::Regen(int iSlot) {
	// The original required the controller's pawn, a real team and a live pawn.
	if (!pisex::IsAlive(iSlot) || pisex::GetPlayerTeam(iSlot) < 2) {
		return false;
	}
	// The s2sdk wrappers call their binding slots unchecked; without the engine armor accessors
	// the regeneration stops instead of calling through a null pointer.
	if (!__s2sdk_GetClientArmor || !__s2sdk_SetClientArmor) {
		return false;
	}
	const int iArmor = s2sdk::GetClientArmor(iSlot);
	const int iMaxArmor = MaxArmorFromFeature(g_pVIPCore->VIP_GetClientFeatureString(iSlot, kArmorFeature));
	if (iArmor < iMaxArmor) {
		const int iNewArmor = iArmor + g_pVIPCore->VIP_GetClientFeatureInt(iSlot, kFeature);
		if (iNewArmor > iMaxArmor) {
			s2sdk::SetClientArmor(iSlot, iMaxArmor);
			return false;
		}
		s2sdk::SetClientArmor(iSlot, iNewArmor);
		return true;
	}
	return false;
}

void RegenArmorPlugin::CreateRegenTimer(int iSlot) {
	const float delay = SafeTimerInterval(g_pVIPCore->VIP_GetClientFeatureFloat(iSlot, kDelayFeature));
	m_timers[static_cast<std::size_t>(iSlot)] =
		pisex::CreateTimer(delay, [this, iSlot]() -> float {
			m_regen[static_cast<std::size_t>(iSlot)] = Regen(iSlot);
			if (m_regen[static_cast<std::size_t>(iSlot)]) {
				return SafeTimerInterval(g_pVIPCore->VIP_GetClientFeatureFloat(iSlot, kIntervalFeature));
			}
			m_timers[static_cast<std::size_t>(iSlot)] = nullptr;
			return -1.0f;
		});
}

void RegenArmorPlugin::StopTimer(int iSlot) {
	pisex::CTimer*& timer = m_timers[static_cast<std::size_t>(iSlot)];
	if (timer != nullptr) {
		pisex::RemoveTimer(timer);
		timer = nullptr;
	}
}

void RegenArmorPlugin::ResetSlot(int iSlot) {
	if (iSlot >= 0 && iSlot < kMaxSlots) {
		m_regen[static_cast<std::size_t>(iSlot)] = false;
	}
}

void RegenArmorPlugin::OnPlayerHurt(const char* /*szName*/, pisex::IGameEvent* pEvent, bool /*bDontBroadcast*/) {
	// The original read the raw "userid" event field. GetPlayerSlot is the documented accessor
	// (s2sdk game-events guide) and answers -1 when the key is not a player.
	const int iSlot = pEvent->GetPlayerSlot("userid");
	if (iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	if (!g_pVIPCore->VIP_IsClientVIP(iSlot) || pEvent->GetInt("dmg_armor") == 0) {
		return;
	}
	if (!pisex::IsAlive(iSlot) || pisex::GetPlayerTeam(iSlot) < 2) {
		return;
	}
	if (g_pVIPCore->VIP_GetClientFeatureInt(iSlot, kFeature) == -1) {
		return;
	}
	// The previous timer is stopped unconditionally: the flag can already be false while the timer
	// is still alive (a spawn/load/disconnect reset or the toggle callback clears the flag but not
	// the timer), and CreateRegenTimer() then overwrote the live handle - two timers regenerating
	// the same slot, and the first one to stop cleared the handle of the other, so that one could
	// not be stopped any more.
	StopTimer(iSlot);
	m_regen[static_cast<std::size_t>(iSlot)] = true;
	CreateRegenTimer(iSlot);
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_regenArmorPlugin)
