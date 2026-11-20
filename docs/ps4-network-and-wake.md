# Bella & Atlas: Wi-Fi drops and remote wake – evidence so far and the test plan (20 Nov 2026, 5:40am)

Owner: *"find out what's really going on with the network issue in bella and atlas"*, *"try solving for remote wake and sleep"*.
Only what the logs show is written as fact; everything else is marked as a guess with the test that would prove it.

## The rooms
| PS4 | Model / Wi-Fi chip | Firmware | Network now | Notes |
|---|---|---|---|---|
| Bella | CUH-7218B Pro, AzureWave | 11.00 | round2-ps4 (2.4 GHz only, ch 6), 192.168.120.2 | right above the main router (wooden floor) |
| Atlas | CUH-7208B Pro, Hon Hai | 9.00 | round2-ps4, 192.168.120.3 | upstairs; has been seen on the lobby mesh unit before |
| Kaeden (test bench) | CUH-1000A | 13.50 | round2, ch 6 (main router 2.4 GHz) | |
| Scarlett | CUH-1006A, Liteon | 11.00 | round2, ch 6 | |
| Max / Vale | Hon Hai / AzureWave | 9.00 / 11.00 | round2, ch 36 (5 GHz) | |
| Kai | Hon Hai | 9.00 | round2, ch 36 via the lobby mesh unit | |
Router: TP-Link HB611 (Singtel EasyMesh), main unit 192.168.88.1 upstairs, second unit in the lobby (2.4 GHz wireless link to the main).
round2-ps4 = extra SSID on the main unit, its own subnet 192.168.120.x (devices on round2 / 192.168.88.x can't reach it).

## Three different problems (don't mix them up)
### A. Online on the Wi-Fi but no internet (Bella, 19 Nov 13:35–15:48)
- Fact: helper running, 114 failed connects to the engine; meanwhile Bella answered on the LAN (DDP 200 Ok from Kaeden 3:15pm).
  Same radio as Scarlett/Kaeden/Atlas – none of them had an outage.
- Guess: another device using Bella's old address 192.168.88.6 (IP clash) or a router rule.
- Test: Bella now has a different subnet (192.168.120.2). If problem A never comes back on round2-ps4 → the old address was the cause.

### B. Lost the router completely (Bella 21:22–21:40, Atlas 21:31–21:49, 19 Nov – both at the same time, ~18 min)
- Fact: Bella's path check "shop router no answer · internet no answer · engine no answer" = the Wi-Fi link itself was gone.
- Fact: at the same time Kaeden (same 2.4 GHz radio of the main unit), Max/Vale (5 GHz) and Kai (lobby unit) logged no failure.
  So the router was up; only these two dropped.
- Guess: EasyMesh moving them (band steering / to the lobby unit) – both were on the shared "round2" name then. Other guess: the
  2.4 GHz signal upstairs is weak for them (−71/−72 dBm measured on 18 Nov, even near the router).
- Since the move to round2-ps4 (2.4 GHz-only name, 23:14/23:20): **no failure at all** until they were rested at 2:17am (~3 h, a
  quiet night – not proof yet). Test: a full busy day/evening on round2-ps4. If B never comes back → steering was the cause; then
  the other rooms can follow one at a time.

### C. Remote wake from rest
- Fact (18–19 Nov): on 2.4 GHz in rest, Bella and Atlas stay connected to the router, but the router delivers nothing to them
  (its "sent packets" count froze, no ping answered for 20 min). Kai (5 GHz) answered "620 Standby" and woke fine.
- Fact: Atlas and Bella woke only when the wake was sent ~3 min after rest (from Vale). After ~28 min: Atlas no. After 11.5 h
  (19 Nov 12:36pm): neither. → mornings: staff press their buttons.
- Fact (19 Nov 11pm): since the move, PS4s on round2 can't reach them at all (Kaeden → DDP to 192.168.120.2: no answer), so the
  engine's room-to-room wake can't reach Bella/Atlas any more, whatever the sleep length.
- Remote SLEEP works everywhere (`/_rest`, used tonight on Bella, Atlas, Max).
- Guesses to test, cheapest first:
  1. The wake has to come from inside round2-ps4 now → a sender on that network (Bella wakes Atlas, Atlas wakes Bella, or Kaeden
     moved onto round2-ps4 as the test bench / wake gateway).
  2. After a long sleep the router no longer knows the PS4's address (ARP) → a fixed IP+MAC binding on the router for Bella/Atlas,
     then a DIRECT (unicast) wake. Router change = owner's OK and outside session times.
  3. If 1+2 fail: the Wi-Fi chip in deep rest isn't listening → keep Bella/Atlas awake instead of rest on booking days (screen off
     via the TV, helper keeps it awake; ~70–90 W each) – the booking keep-awake (1.1.68) already does this 2 h before a session.
- Not to be suggested: LAN cables (owner rule).

## Test plan (daytime, no guests, owner present)
1. Bella + Atlas awake on round2-ps4 → rest Atlas → wake it from Bella after 3 min, 30 min, 2 h (direct + broadcast). Swap roles.
2. If only short sleeps work: router IP+MAC binding for both (owner OK), repeat the 2 h test.
3. Meanwhile keep reading net-fail.log on both every evening for A and B.

## Sweep findings – rest mode & wake inside the PS4 (Kaeden 13.50, 20 Nov 6:10am; every PS4 gets the same sweep)
- The menu knows only two wake triggers: **REMOTE_PLAY (100)** = the LAN wake we already use (DDP), and **REMOTE_PLAY_NP_PUSH
  (102)** = a wake through Sony's servers (blocked here – PS4 DNS 127.0.0.1). **No timer/alarm wake exists in the menu.**
- A PS4 woken by Remote Play runs `WakeOnRemotePlayMode`: a shutdown timer that sends it back to rest if no Remote Play session
  connects (standby reason 14). To check in the logs.
- "Main on standby" = half-awake in rest (downloads, auto-update, event schedule), started by the low-power chip (EAP). All
  auto-download settings are 0 on Kaeden.
- Settings read with helper 1.1.111 `/_pwrreg` (Kaeden): rest mode 1 · stay connected 1 · turn on from network 1 · USB power 2 ·
  Wi-Fi band 0 · Remote Play 1. **Next: the same read on Bella, Atlas and Kai** – a different value there would explain the wake
  problem (Kai wakes, Bella/Atlas don't).

## Atlas, 20 Nov 2:00–2:50pm (guest watch + rest)
- Guest session 11:12–2:00pm: 0 network fails, check-ins every minute, signal 82–98% on round2-ps4 ch6, new address 192.168.120.4.
- ~2:00:30pm rested at session end (40 s after its last check-in) – a rest, not a drop: the same helper run survived, no netfail.
  Owner turned it on ~2:30pm; it then auto-updated to 1.1.113 (~2:37).
- **Quirk: Atlas's own PS4 clock is ~8 min 17 s fast.** Its log stamps are PS4 time – subtract ~8 min to line them up with the
  engine / control centre.
- **Cause of the 2pm rest (owner): the guest turned the TV off and the TV sent "rest" to the PS4 over HDMI (HDMI Device Link /
  CEC).** TV off = PS4 rest on Atlas. Matters because Atlas (2.4 GHz) does not wake remotely after a long rest.
