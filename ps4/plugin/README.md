# round2_input – controller detector (GoldHEN plugin)

Runs inside the game (GoldHEN loads it at game start from `/data/GoldHEN/plugins.ini`). Watch-only hook on the game's own
`scePadRead` / `scePadReadState` (the same method as GoldHEN's gamepad_helper); it never changes what the game gets.

- **Asleep by default** (owner, 18 Nov 2026): it only watches while `/data/round2/input/arm` holds a future time. The helper
  (1.1.62+) writes that when the end-of-session reset countdown starts and deletes it when the countdown ends. Asleep, the
  hook is one flag check; nothing is written.
- Awake: writes `/data/round2/input/last` = `r2in 1 <last_input> <now> <titleid> <pid> <reads> <active>` (temp + rename).
- Off switch: `/data/round2/input/off` (helper `/_inplug.off` also removes it from plugins.ini).
- Helper jobs: `/_inplug.status`, `/_inplug.get.<sha256>` (download via engine `/ps4/plugin`, checked), `/_inplug.on.<CUSA12345|default>`, `/_inplug.off`.
- Build: `ps4/plugin/build.sh` (OpenOrbis toolchain v0.5.2 + GoldHEN Plugins SDK, inside a GoldHEN_Plugins_Repository checkout).
  Makefile and include/pad.h come from GoldHEN_Plugins_Repository (GPL-3.0).
