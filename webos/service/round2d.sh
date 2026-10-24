#!/bin/sh
# Round2 Home helper for ROOTED LG TVs.
#
# Started at boot by /var/lib/webosbrew/init.d/round2 (which Round2 Home writes through
# Homebrew Channel's root service), and by Round2 Home itself after an install or update.
#   - TV switched on (from standby, or a full start): opens Round2 Home
#   - Home button pressed anywhere, including inside Round2 Home: opens Round2 Home's main menu
#     (Home pressed 3 times quickly lets LG's own home screen through, for staff or a frozen menu)
#   - after Round2 Home updates itself: opens it again
# Switches (Round2 Home > Options > staff): /var/lib/webosbrew/round2/poweron.off, home.off
# Undo everything: delete /var/lib/webosbrew/init.d/round2 and restart the TV.
#
# Usage: round2d.sh boot | start | ensure | stop
APP=sg.round2.tvhome
CFG=/var/lib/webosbrew/round2
SELF="$0"
mkdir -p "$CFG"

log() { echo "$(date '+%m-%d %H:%M:%S') $*" >> "$CFG/log.txt"; }
on() { [ ! -f "$CFG/$1.off" ]; }
launch() {
  log "open Round2 Home ($1)"
  luna-send -n 1 luna://com.webos.service.applicationManager/launch "{\"id\":\"$APP\",\"params\":{\"r2\":\"$1\"}}" >/dev/null 2>&1
}
field() { sed -n "s/.*\"$1\" *: *\"\\([^\"]*\\)\".*/\\1/p"; }
sum() { md5sum "$SELF" 2>/dev/null | cut -d' ' -f1; }
running() { [ -f "$CFG/pids" ] && kill -0 "$(head -n 1 "$CFG/pids")" 2>/dev/null; }
stop() {
  if [ -f "$CFG/pids" ]; then
    for p in $(cat "$CFG/pids"); do kill "$p" 2>/dev/null; done
    rm -f "$CFG/pids"
  fi
  # the luna-send subscriptions belonging to the old copy
  pkill -f '^luna-send -i luna://com.webos.applicationManager/getForegroundAppInfo' 2>/dev/null
  pkill -f '^luna-send -i luna://com.webos.service.tvpower/power/getPowerState' 2>/dev/null
}

# --- what each event does ---
watch_apps() {
  while :; do
    prev=""; hits=0; hlast=""
    luna-send -i luna://com.webos.applicationManager/getForegroundAppInfo '{"subscribe":true}' 2>/dev/null | while read -r line; do
      id=$(echo "$line" | field appId)
      [ -z "$id" ] && continue
      if [ -f "$CFG/relaunch" ] && [ "$id" != "$APP" ]; then          # Round2 Home just updated itself
        rm -f "$CFG/relaunch"; sleep 1; launch update; prev=$APP; continue
      fi
      case "$id" in
        com.webos.app.home|com.webos.app.launcher)
          if on home; then
            if [ "$prev" = "$APP" ]; then
              # Home pressed inside Round2 Home: back to its main menu. 3 quick presses = LG's home (staff, or a frozen menu)
              now=$(date +%s)
              if [ -n "$hlast" ] && [ $((now - hlast)) -le 3 ]; then hits=$((hits + 1)); else hits=1; fi
              hlast=$now
              if [ "$hits" -ge 3 ]; then log "Home x3: LG home"; hits=0; hlast=""; prev=$id; continue; fi
            else
              hits=0; hlast=""
            fi
            launch home; id=$APP
          fi ;;
      esac
      prev=$id
    done
    sleep 5                                                               # the subscription ended: start it again
  done
}
watch_power() {
  while :; do
    last=""
    luna-send -i luna://com.webos.service.tvpower/power/getPowerState '{"subscribe":true}' 2>/dev/null | while read -r line; do
      st=$(echo "$line" | field state)
      [ -z "$st" ] && continue
      if [ "$st" = "Active" ]; then
        case "$last" in
          "Active Standby"|"Suspend"|"Power Off") on poweron && { sleep 2; launch poweron; } ;;
        esac
      fi
      last=$st
    done
    sleep 5
  done
}
first_open() {
  # a full start: wait until the TV is showing something, then open Round2 Home over it
  n=0
  while [ $n -lt 40 ]; do
    id=$(luna-send -n 1 luna://com.webos.applicationManager/getForegroundAppInfo '{}' 2>/dev/null | field appId)
    [ -n "$id" ] && break
    sleep 2; n=$((n + 1))
  done
  [ "$id" = "$APP" ] || { sleep 2; on poweron && launch boot; }
}

case "$1" in
  stop) stop; log "stopped"; exit 0 ;;
  ensure)                                   # Round2 Home calls this on every start: keep one up-to-date copy running
    if running && [ "$(cat "$CFG/sum" 2>/dev/null)" = "$(sum)" ]; then exit 0; fi ;;
  boot|start|"") ;;
esac

stop
sum > "$CFG/sum"
log "helper started ($1)"
watch_apps </dev/null >/dev/null 2>&1 &
A=$!
watch_power </dev/null >/dev/null 2>&1 &
P=$!
printf '%s\n%s\n' "$A" "$P" > "$CFG/pids"
[ "$1" = "boot" ] && first_open </dev/null >/dev/null 2>&1 &
# keep the log short
[ -f "$CFG/log.txt" ] && [ "$(wc -l < "$CFG/log.txt")" -gt 300 ] && { tail -n 100 "$CFG/log.txt" > "$CFG/log.tmp"; mv "$CFG/log.tmp" "$CFG/log.txt"; }
exit 0
