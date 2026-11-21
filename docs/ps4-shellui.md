# Round2 inside the PS4 system menu (SceShellUI) – findings, tools, how to redo them per firmware

Owner, 20 Nov 2026 5:27am: *"add everything you found to accessible logs, because we might need to redo all these steps on 9.00
and 11.00 … rebuilding on the firmware it was built and probed for is better"*. **Rule: nothing found on Kaeden (13.50) is assumed
to work on a 9.00 or 11.00 room. Each firmware gets its own probe run, its own results file and its own add-on test.**

## Why we went into the menu
- The helper's own `sceUserServiceLogout` signs a profile out but leaves the controller stuck (freeze; recovery = hold PS 10 s).
  The PS menu's **Log Out** works because the *menu* does it and re-links the controllers.
- Owner wants, around a member load: the PS button locked, no profile picker popping up when a controller is switched on, an unlock
  combo (L2+R2+L3+R3), and afterwards everyone logged out properly so players pick their own (renamed) profiles.

## How code gets into the menu (proven on Kaeden 13.50, GoldHEN 2.4b18.x)
1. GoldHEN's own SDK call: indirect syscall 0 → 500, cmd 6 = load a module into a running process (`struct {char process_name[32];
   char prx_path[100]; uint64 res;}`). Use the GoldHEN way (helper `r2_osyscall`): libc `syscall()` gave -256.
   cmd 0 (version) answers -256 on Kaeden – ignore it; cmd 4 (process info) works ("pid 87 name 'Payload'").
2. **The menu can only load from GoldHEN's own plugin folder**: `/user/data/GoldHEN/plugins/<name>.prx`.
   /data/… = 0x80020002 (not found), /user/data/round2/… = 0x8002000d (no permission), /system_tmp = 0x80020023.
3. **The menu writes only under /user/data/…** (it can't see /data). Add-ons log to /user/data/round2/<name>.txt (= /data/round2/…).
4. **A module path loads once per menu start** – loading the same path again returns the old module without running it. So every
   add-on build gets a new name (round2_shprobe2, round2_shprobe3, round2_shell1, …).
5. Delivery without helper updates: add-ons live on branch **ps4-shell** of this repo (`<name>.prx` + `<name>.json {v, sha256, size}`);
   the engine serves them at `/ps4/prx?name=` (key); helper **1.1.110+** `look /_shmod.<name>` downloads, saves to the GoldHEN plugin
   folder (chmod 0777), loads it into SceShellUI, waits ≤30 s for `<name>.txt.done`, uploads the log to R2
   `saves/lib/probe/<name>-<console>.txt`. Refused while a game is open (if the menu restarts, the game would close).
6. `.prx` files are SELF (starts 4F 15 3D 1D), not bare ELF.
7. Helper switches: max ~4–5 per PS4 start (Kaeden's helper hung on the ~8th switch at 4:10am; a restart cleared it).

## The probe (read-only) – `ps4/plugin/round2_shprobe` (GoldHEN plugin SDK + libmonosgen)
The menu's C# program is encrypted on disk (`/system_ex/app/NPXS20001/psm/Application/app.exe.sprx` 16.5 MB + `Sce.Vsh.*.dll.sprx`),
so names are read from the running menu with Mono reflection. Nothing is called or changed.
- 1.00 (round2_shprobe via `/_shprobe`): keyword list (logout/login/signin/user/pad/controller/PS button/quick menu …).
  Kaeden result: R2 `saves/lib/probe/shprobe-73378b38f36f345372e93e1fd488bbea.txt` (app.exe = 11,634 types).
- 1.01/1.02 (round2_shprobe2): full signatures of the target classes. R2 `…/round2_shprobe2-73378b38….txt` (72 KB).
- 1.03 (round2_shprobe3): every loaded assembly (mono_assembly_foreach) + enum values. R2 `…/round2_shprobe3-73378b38….txt` (177 KB).

### Kaeden 13.50 results (re-check on every firmware!)
| Purpose | Function (assembly app.exe unless noted) |
|---|---|
| Pause / bring back the profile picker | `Sce.Vsh.ShellUI.LoginMgrUtil` static `SuspendLoginScreen()`, `ResumeLoginScreen()`, `IsSuspendedLoginScreen()` → bool |
| Log everyone out (menu's way) | `LoginMgrUtil` static `LogoutAll(LoginMgrProxy.LogoutMethodType)` |
| Log one user out | `LoginMgrUtil` static `Logout(int user, LogoutMethodType, int, int, LogoutParam)`, `LogoutSync(int, type, int)`, `SilentLogout(int, type)` |
| LogoutMethodType (platform.dll) | Unknown −1, ChangingPowerState 0, NotUse 1, MultipleSignin 2, …, **LogoutFromPsImposeMenu 10**, **LogoutFromPowerMenuOnFunctionArea 11**, ByLoginDialog 12, **LogoutFromPsImposeMenuV400 13**, … Other 15 |
| Power menu items | `TopMenu.SystemAreaPowerDialog/PageBase` `SelectedLogoutSystem()`, `SelectedSwitchUser()`, `SelectedEnterStandby()` (instance) |
| Switch user / controllers | `LoginMgrUtil.LaunchSwitchUser(int)`, `DisconnectDevice(long)`; `LoginMgrUtilInternal` `StartSwitchUser(int)`, `DisconnectAllDevice(int)`, `ArbitrarilyDisconnectDevice(long)`, `SetForegroundUser(int,bool,bool,bool,bool)`, `GetLoginUserIdList` |
| PS / Share button lock | `PadInternal.Locker(LockType lock, LockType uiMask)` – unlock = `DisposableObj.Dispose()`; subclasses `PSButtonLocker()`, `PSButtonLongLocker()`, `PSButtonDoubleLocker()`; also `EventApp.EventAppExternal.LockShareAndPSButton()` / `TeamChat.TeamChatExternal.LockPSButton()` (instance) |
| LockType (platform.dll) | None 0, PSButton 1, PSButtonLong 2, PSButtonDouble 4, ShareButton 8, ShareButtonLong 16, ShareButtonDouble 32, All 0xffffffff |
| Controller buttons (for the unlock combo) | `PadInternal.PadManager` static `get_Instance()`, field `m_padInfo` (List<PadInfo>, `_items`/`_size`); `PadInfo.m_curGeneralButtons` = `Sce.PlayStation.Core.Input.GamePadButtons` (L2 0x1000, R2 0x2000, L3 0x4000, R3 0x8000) |
| Other | `PadManager.ForceIntercept(bool)`, `EmulatePSButton()`, `IsControllerConnected()`; `QuickMenu.MenuSuppressor()` (quick menu off while it lives) |

## The add-on – `ps4/plugin/round2_shell` (round2_shell1 = 1.00, built 20 Nov 5:23am, NOT yet run)
- On load: 60-s self-test (picker paused + PS/Share locked, L2+R2+L3+R3 held 1 s unlocks), then WATCH: during a member load
  (/data/round2/memqn or clean run "prep" running) picker paused + PS/Share locked (max 10 min, combo unlocks); released at the end.
  Log-out of everyone (type 13) after a load only if /data/round2/shell-logout exists. Newer add-on → older stops (shell.gen).
- Runs on its own thread; every menu call goes through mono_runtime_invoke with an exception slot (errors logged, not thrown).
  Not done (yet): memory patching such as HEN's `CheckRunningOnMainThread` → ret. If the menu refuses calls from our thread,
  that's the next step.

## Add-on runs and versions (log everything – owner 20 Nov)
- **round2_shell3, Kaeden 13.50, 20 Nov 8:09am (self-test, owner there, controller off):** found suspend/resume/is-suspended/
  logout-all/locker/pads all 1; **buttons read 0xffffffff = "can't read"** although the pad fields were found; lock + picker pause
  worked (check said 1); after the log-out the TV showed "Press PS button" (the profile picker – owner: acceptable). Conclusion:
  `m_padInfo` is NOT the List<> assumed above (3.00 gave -1 before reading any controller) – exact type unknown.
- **round2_shell4 (4.00, ps4-shell 3ce2e32, 20 Nov 3:19pm, not run yet):** reads `m_padInfo` as an array OR a list, of objects OR
  structs; "no controller" = 0, -1 only with a reason; logs ONE line describing the pad list (type, element type, struct/object,
  size, field offset, each pad's buttons) + PadInfo's field names/offsets. Self-test only on request (`/data/round2/shell-selftest`,
  helper `/_shtest`), every button change logged during it. `/data/round2/shell.alive` heartbeat every 30 s.
- **Helper 1.1.114 (ps4-test 41b5a51, not on any PS4 yet):** loads round2_shell4 at every PS4 start (`shell_auto`, once a minute:
  skips with /data/round2/shell-off, a game open, the 5 min after a wake, or the add-on alive; max 3 tries 10 min apart; each try
  in save.log). `/_shtest` = self-test + log to R2 `lib/probe/round2_shell4-<console>.txt`.

### 20 Nov evening (Kaeden 13.50)
- 7:10pm round2_shell4 crash (see crash log). 7:23pm **round2_padprobe1** (read-only) OK: m_padInfo = `List<PadInfo>` of objects,
  `_items` PadInfo[32], `_size` 24. PadInfo: m_port, m_readTime, m_curInfo (Orbis.Input.ControllerInfo), m_curButtons
  (Orbis.Input.SystemButtons), m_curGeneralButtons (Core.Input.GamePadButtons), m_buttonState (List<ButtonState>).
- 7:25pm **round2_shell5** auto-loaded by helper 1.1.117 – no crash, "24 entries (24 filled)". 7:27 self-test: lock, pause, unlock,
  log-out all ✅; m_curGeneralButtons read 0x0 the whole minute (owner asked whether he pressed).

- **Button numbers (Kaeden 13.50, m_curGeneralButtons, 20 Nov 7:36pm):** L2 0x1000, R2 0x2000, L3 0x4000, R3 0x8000, L1 0x400,
  R1 0x800, X 0x20080, O 0x10040, triangle 0x20, square 0x10, d-pad left 0x1 / up 0x2 / right 0x4 / down 0x8. Combo L2+R2+L3+R3
  = 0xf000 (seen). m_curButtons stays 0 – not used. 24 slots in the list.

## Redo on another firmware (9.00 = Max, Kai, Atlas; 11.00 = Scarlett, Vale, Bella) – checklist
1. Helper ≥ 1.1.110 on that PS4 (count the switches since its last restart).
2. No game open, staff at the TV. `look /_ghver` → GoldHEN SDK present?
3. `look /_shmod.round2_shprobe3` (if that name already ran since this PS4 started, publish a copy under a new name first).
4. Read R2 `saves/lib/probe/round2_shprobe3-<console>.txt`; compare every row of the table above. Write a new table for that
   firmware here – don't overwrite Kaeden's.
5. Only then load the add-on (a build made/checked for that firmware), with the owner watching.

## Crash log
| When | PS4 | What | Cause | Fix |
|---|---|---|---|---|
| 20 Nov 7:10pm | Kaeden 13.50 | `round2_shell4` loaded by helper 1.1.115 auto-load → menu crash (owner); no log line, no .step; load "call 0, result 0x0, not alive after 15 s" | most likely an import the menu's Mono doesn't export: shell4 added 5 never-used ones (mono_class_get_rank, mono_class_get_element_class, mono_class_is_valuetype, mono_array_element_size, mono_field_get_offset) | Kaeden → helper 1.1.116 (no auto-load). Rule: add-ons import only Mono functions proven in an earlier run; new ones via sceKernelDlsym. `round2_padprobe1` (read-only) checks the 5 by name |
| 20 Nov 5:50am | Kaeden 13.50 | `round2_pwrprobe1` → menu error CE-36329-3 (helper + jailbreak survived) | read Mono's "Constant" metadata table as 3 columns; Mono has 4 and aborts on a column-count mismatch (`mono_metadata_decode_row`). Also suspect: setting up methods of every class | removed from ps4-shell; `round2_sweep1` checks each table's column count from Mono's table header first, reads method names from metadata, writes a breadcrumb `/data/round2/<name>.step` before every step |

## The sweep (owner 20 Nov 5:49am)
One read-only add-on, `round2_sweep2` (sweep1 = same with a 1.8 MB cap, cut on Kaeden), run on EVERY PS4 (not one per firmware): rest mode, wake, network-in-rest, timers, Remote
Play, log-in/out, users, controllers, PS button – class/method names, constants (registry assembly in full), every system function
the menu calls. Result: R2 `lib/probe/round2_sweep2-<console>.txt` + helper 1.1.111 `/_pwrreg`. Kaeden DONE 20 Nov 6:10am (2.36 MB, complete); then each room when the owner turns it on.

## round2_shell9 + helper 1.1.124 (20 Nov 9:10pm, Kaeden)
- shell9 = shell8 without the 2-s wait: logs everyone out the moment the tile leaves the front (fg.txt checked every 0.1 s; the
  helper writes it within 0.25 s during a load). Owner had ~4 s of usable menu with shell8.
- ~~Quirk~~ **Corrected 10:18pm:** the old add-on stops itself when the new one starts (shell8 log "21:11:15 stopped - a newer
  add-on is running"), so add-on updates need no restart. The 1.1.125 guard was removed in 1.1.129.

## round2_shell10 + helper 1.1.129 (20 Nov 10:16pm, Kaeden)
- Clean slate / session reset now works like a member load: PS + Share locked, picker paused, everyone logged out the menu's
  way (LogoutAll 13) at the start (helper asks via /data/round2/shell-logout-now → .done) and again at the end, then unlocked.
  No new Mono functions.
## Home-screen folders (read-only probe, Kaeden 13.50, 21 Nov 2026 ~3:30pm)
- Helper 1.1.141–1.1.143: `/_appdb` copies `/system_data/priv/mms/app.db` to /data/round2 and reads the COPY (nothing written on
  the PS4) → `/data/round2/appdb-probe.txt` (everything) + `appdb-short.txt` (read with `/_head<line>.<file>|<lines>`).
- Folders are per profile: table `tbl_appbrowse_<user id, decimal, 10 digits>` (Kaeden main 16c45cc6 = 0381967558 … 16c45cca =
  0381967562), 55 columns incl. folderType, folderInfo, parentFolderId, positionInFolder.
- Owner's test folder (on Test Member 16c45cc9, table 0381967561 – 49 rows, the others 48): row `titleId=000000001,
  titleName=Folder, folderType=1, folderInfo={"titleId":"CUSA03887","index":0}`, other columns mostly NULL. Broforce:
  `parentFolderId=000000001, positionInFolder=1`.
- Rename = titleName of the folderType=1 row; create = new row + games' parentFolderId/positionInFolder. Not tested: whether the
  shell re-reads app.db while running, and writing the live file safely (backup + one SQLite transaction). Test on Test Member's
  folder first, owner present.
