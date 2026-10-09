# vip_regen_armor (Plugify)

Armor regeneration for VIPs: 'RegenArmor' (armor per tick), 'DelayRegenArmor' (seconds after taking armor damage before regeneration starts) and 'IntervalRegenArmor' (seconds between ticks) come from groups.ini, and the armor ceiling from the group's 'armor' value (100 by default, absolute or '+N' like the original). A player_hurt hook starts the timer; every tick adds RegenArmor until the ceiling. A port of the Metamod:Source module 'VIP_RegenArmor' of the Pisex cs2-vip plugin.

Plugify port (fork) of **VIP_RegenArmor** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, vip, hudmenu (optional)

## Installation
Download `vip_regen_armor-1.0.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_regen_armor`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

