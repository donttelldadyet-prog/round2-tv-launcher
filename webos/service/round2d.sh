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
# Home button (needs LG Input Hook from Homebrew Channel): Round2 Home sets button 773 (Home) to run
#   sh /var/lib/webosbrew/round2/round2d.sh key
# so the TV never sees Home at all: no flash of LG's home bar. 3 quick presses still open LG's home.
# Without LG Input Hook, the helper below catches LG's home bar after it opens instead.
#
# Food order alerts (Round2 food menu): while another app is in front (Netflix, HDMI, YouTube), shows
#   the room's alerts with LG's own pop-ups: a toast for "order locked in", a pop-up with a GOT IT button
#   for "loot dropped". GOT IT is reported to Make.com, which replies under the order in Telegram.
#   Round2 Home writes the room to $CFG/food ("<room> <key>"); while Round2 Home is in front, the menu
#   shows the alert itself.
#
# Usage: round2d.sh boot | start | ensure | stop | key | ack
APP=sg.round2.tvhome
CFG=/var/lib/webosbrew/round2
SELF="$0"
mkdir -p "$CFG"

log() {
  echo "$(date '+%m-%d %H:%M:%S') [$$] $*" >> "$CFG/log.txt"
  [ "$(wc -l < "$CFG/log.txt")" -gt 400 ] && { tail -n 200 "$CFG/log.txt" > "$CFG/log.tmp"; mv "$CFG/log.tmp" "$CFG/log.txt"; }
}
on() { [ ! -f "$CFG/$1.off" ]; }
launch() {
  log "open Round2 Home ($1)"
  luna-send -n 1 luna://com.webos.service.applicationManager/launch "{\"id\":\"$APP\",\"params\":{\"r2\":\"$1\"}}" >/dev/null 2>&1
}
lg_home() {
  log "open LG home ($1)"
  touch "$CFG/lghome"                       # tells watch_apps this one was on purpose
  luna-send -n 1 luna://com.webos.service.applicationManager/launch '{"id":"com.webos.app.home"}' >/dev/null 2>&1
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
  pkill -f '^luna-send -i luna://com.webos.service.tvpower/power/registerScreenSaverRequest' 2>/dev/null
  pkill -f 'curl -sN .*firebasedatabase' 2>/dev/null
  # any other copy still running (an older version, or one started twice at boot): only one may watch,
  # or every Home and Back is handled twice. Home-button presses ("key") are left alone.
  # Read from /proc: LG's 'ps' lists only processes of the current terminal, so it never found old copies, and
  # after an update the old and new helper both ran (two pop-ups for one alert, messages swallowed).
  n=0
  for d in /proc/[0-9]*; do
    p=${d#/proc/}; [ "$p" = "$$" ] && continue
    set -- $(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)
    [ "$1" = "-c" ] || [ "$2" = "-c" ] && continue                 # a 'sh -c' that only started us
    case "$1 $2 $3" in
      *round2d.sh*key*|*round2d.sh*ack*|*round2d.sh*msgpage*) continue ;;   # one-off button presses
      *round2d.sh*) kill "$p" 2>/dev/null && n=$((n + 1)) ;;
    esac
  done
  [ "$n" -gt 0 ] && log "stopped $n old helper processes"
  return 0
}

# --- what each event does ---
watch_apps() {
  while :; do
    prev=""; hits=0; hlast=""; last_app=""
    luna-send -i luna://com.webos.applicationManager/getForegroundAppInfo '{"subscribe":true}' 2>/dev/null | while read -r line; do
      id=$(echo "$line" | field appId)
      [ -z "$id" ] && continue
      case "$id" in sg.round2.alerts*) continue ;; esac   # our pop-up card floats over the app; that app is still in front
      [ "$id" != "$prev" ] && { log "front $id"; echo "$id" > "$CFG/front"; rm -f "$CFG/hb.last"; }   # for Staff tools > Home helper check, food alerts, and the check-in (what's on screen, 2 Nov)
      case "$id" in "$APP"|com.webos.app.home|com.webos.app.launcher|com.webos.surfacemanager*|"") ;; *) last_app=$id ;; esac
      if [ "$id" = "$APP" ] && [ "$id" != "$prev" ]; then   # the guest is back on the menu: anything still waiting for OK
        rm -f "$CFG/msg.last"                                  # an unread message: shown again within seconds
        # Home pressed while our card was up over an app: webOS closes the card without it tidying up, so card.up
        # still said "on screen" and the menu's big banner waited up to 2.5 min (Vale, 31 Oct). The card can't be up now.
        rm -f "$CFG/card.up"
        [ -f "$CFG/food.pending" ] && ( sleep 2; menu_card "$(cat "$CFG/food.pending")" ) &
        # Close the app they came from (Netflix, YouTube...): LG keeps it running in the background, still holding the
        # TV's video player, and the menu's moving background then stays black. Then the menu restarts its background.
        if [ -n "$last_app" ]; then
          luna-send -n 1 luna://com.webos.applicationManager/closeByAppId "{\"id\":\"$last_app\"}" >/dev/null 2>&1
          log "closed $last_app (back on the menu)"; last_app=""
          ( sleep 1; luna-send -n 1 luna://com.webos.service.applicationManager/launch "{\"id\":\"$APP\",\"params\":{\"r2\":\"back\"}}" >/dev/null 2>&1 ) &
        fi
      fi
      # the guest left the menu for an app or input: a message still waiting for OK follows as the small banner now
      [ "$prev" = "$APP" ] && [ "$id" != "$APP" ] && rm -f "$CFG/msg.last"
      # app to app (YouTube -> Netflix with the remote's Netflix button): if webOS closed our card on the way, a message
      # waiting for OK (and Loot dropped) come straight back over the new app instead of after the 5-minute reminder.
      # If the card is still up, nothing changes (it stays until OK).
      case "$prev:$id" in
        :*|*:"$APP"|"$APP":*|*:com.webos.app.home|*:com.webos.app.launcher|com.webos.app.home:*|com.webos.app.launcher:*) ;;
        *) [ "$prev" != "$id" ] && ( sleep 2; alerts_running || { rm -f "$CFG/card.up" "$CFG/msg.last" "$CFG/food.last"; log "app switch: card gone, shows again"; } ) & ;;
      esac
      # Round2 Home is updating itself (Staff tools › Update app). It used to reopen the moment it closed – while Homebrew
      # was still installing – so webOS closed it again and it opened a second time (owner, 2 Nov: "the loading screen
      # twice … the first time the rain plays"). Now: wait until the new version is really in (the file holds it), then
      # open once. Until then LG's home doesn't bring the menu back either (upd.hold).
      if [ -f "$CFG/relaunch" ] && [ "$id" != "$APP" ]; then
        want=$(cat "$CFG/relaunch" 2>/dev/null); rm -f "$CFG/relaunch"; touch "$CFG/upd.hold"
        ( n=0; while [ $n -lt 40 ]; do v=$(app_ver); [ -n "$v" ] && { [ -z "$want" ] || [ "$v" = "$want" ]; } && break; sleep 3; n=$((n + 1)); done
          sleep 3; rm -f "$CFG/upd.hold"; log "update in: ${v:-?}"; launch update ) &
        prev=$APP; continue
      fi
      case "$id" in
        com.webos.app.home|com.webos.app.launcher)
          # an update is installing: the menu opens once, when it's done (not now and again after)
          if [ -n "$(find "$CFG/upd.hold" "$CFG/updating" -maxdepth 0 -mmin -5 2>/dev/null)" ]; then prev=$id; continue; fi
          if [ -f "$CFG/lghome" ]; then rm -f "$CFG/lghome"; prev=$id; continue; fi   # opened on purpose (Home x3)
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
watch_saver() {   # Staff tools > TV screensaver: webOS asks before it starts its screensaver; while Round2 Home is in front
                  # (and the menu had a remote press in the last 3 hours) we answer "no". Anywhere else, or switched off: "yes".
  while :; do
    luna-send -i luna://com.webos.service.tvpower/power/registerScreenSaverRequest '{"subscribe":true,"clientName":"round2home"}' 2>/dev/null | while read -r line; do
      ts=$(printf '%s\n' "$line" | num timestamp)
      [ -n "$ts" ] || continue
      ack=true
      if on saverblock && [ "$(cat "$CFG/front" 2>/dev/null)" = "$APP" ]; then
        k=$(cat "$CFG/menukey" 2>/dev/null || echo 0)
        [ $(( $(date +%s) - ${k:-0} )) -lt 10800 ] && ack=false
      fi
      luna-send -n 1 luna://com.webos.service.tvpower/power/responseScreenSaverRequest "{\"clientName\":\"round2home\",\"ack\":$ack,\"timestamp\":$ts}" >/dev/null 2>&1
      if [ "$ack" = false ]; then
        l=$(cat "$CFG/saver.log" 2>/dev/null || echo 0); [ $(( $(date +%s) - l )) -ge 1800 ] && { date +%s > "$CFG/saver.log"; log "TV screensaver kept away (menu in front)"; }
      fi
    done
    sleep 30                                                              # the subscription ended: start it again
  done
}
watch_power() {
  K=""; curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null || K="-k"   # the check-in below runs from here too
  while :; do
    last=""
    luna-send -i luna://com.webos.service.tvpower/power/getPowerState '{"subscribe":true}' 2>/dev/null | while read -r line; do
      st=$(echo "$line" | field state)
      [ -z "$st" ] && continue
      # screen on or asleep, for the control centre's TV tile (owner, 3 Nov). With Quick Start+ the TV freezes everything a
      # few seconds after it's switched off, so the check-in is sent AT ONCE here (the 5-minute one would be too late), and
      # "going to sleep" (processing: Request Suspend / Power Off / Screen Off) already counts as asleep.
      pr=$(echo "$line" | field processing); ps=$st
      case "$pr" in *[Ss]uspend*|*[Pp]ower*[Oo]ff*|*[Ss]creen*[Oo]ff*|*[Ss]hutdown*) ps="Suspend" ;; esac
      [ "$ps" = "Active" ] || [ "$ps" = "Suspend" ] || [ "$ps" = "Active Standby" ] || ps="Suspend"
      if [ "$ps" != "$(cat "$CFG/power" 2>/dev/null)" ]; then echo "$ps" > "$CFG/power"; rm -f "$CFG/hb.last"; heartbeat; log "power: $st${pr:+ ($pr)} -> told the control centre $ps"; fi
      if [ "$st" = "Active" ]; then
        case "$last" in
          "Active Standby"|"Suspend"|"Power Off") rm -f "$CFG/msg.last" "$CFG/food.last"; on poweron && { sleep 2; launch poweron; } ;;   # TV on: a waiting message shows now
        esac
      fi
      last=$st
    done
    sleep 5
  done
}
# --- food order alerts over other apps ---
FB=https://round2-tv-default-rtdb.asia-southeast1.firebasedatabase.app
ACK=https://hook.eu1.make.com/ks522ziapwremxa35kdtan6h6l1oxe17
num() { sed -n "s/.*\"$1\" *: *\([0-9][0-9]*\).*/\1/p"; }
# ONE CLOCK (owner, 2 Nov): message and food-alert times run on the booking engine's clock (Cloudflare /time), not the
# TV's own. clock_sync stores the difference in seconds; snow = the TV's time + that difference.
ENGINE_TIME=https://round2-book.donttelldadyet.workers.dev/time
clock_sync() {
  command -v curl >/dev/null 2>&1 || return 0
  a=$(date +%s); r=$(curl -s -m 8 "$ENGINE_TIME" 2>/dev/null || curl -s -k -m 8 "$ENGINE_TIME" 2>/dev/null); b=$(date +%s)
  t=$(printf '%s\n' "$r" | num t); [ -n "$t" ] && [ $((b - a)) -le 5 ] || return 0
  echo $(( t / 1000 - (a + b) / 2 )) > "$CFG/clock.off"
}
snow() { echo $(( $(date +%s) + $(cat "$CFG/clock.off" 2>/dev/null || echo 0) )); }
clean() { tr -d "\\\\\"'"; }   # no quotes or backslashes: safe inside the luna-send JSON below
# --- Round2 Alerts (sg.round2.alerts, from Homebrew Channel): the Round2 card over other apps. NEVER LG's own pop-ups
# (owner, 4–5 Nov): if the card won't open, it's tried again shortly and the alert waits (logged) – nothing falls back to LG.
ov_app() {
  [ -d /media/developer/apps/usr/palm/applications/sg.round2.alerts ] || return 1
  echo sg.round2.alerts
}
ov_show() {     # $1 app id, $2 params JSON. One retry after 2 s; fails (the caller tries again later) if webOS still won't open it.
  for try in 1 2; do
    r=$(luna-send -n 1 luna://com.webos.service.applicationManager/launch "{\"id\":\"$1\",\"params\":$2}" 2>&1)
    case "$r" in *'"returnValue":true'*|*'"returnValue": true'*) return 0 ;; esac
    log "Round2 Alerts didn't open (try $try): $(echo "$r" | tr -d '\n' | cut -c1-120)"
    [ "$try" = 1 ] && sleep 2
  done
  return 1
}
alerts_running() {   # is the Round2 Alerts card (its webOS overlay app) still open?
  luna-send -n 1 luna://com.webos.applicationManager/running '{}' 2>/dev/null | grep -q '"sg.round2.alerts'
}
# Is Round2 Home in front right now? (asked fresh: the menu shows alerts itself then)
menu_front() {   # its own variable (2 Nov): it used to overwrite msg_tick's $id, so LG TVs reported the app in front
                 # ("sg.round2.tvhome", "youtube…") as the message shown, and "already OK'd?"/"done" checked the wrong id
  fg_app=$(luna-send -n 1 luna://com.webos.applicationManager/getForegroundAppInfo '{}' 2>/dev/null | field appId)
  case "$fg_app" in sg.round2.alerts*|"") fg_app=$(cat "$CFG/front" 2>/dev/null) ;; esac
  [ "$fg_app" = "$APP" ]
}
# The helper is the one place that decides where an alert or message appears (one route, so nothing shows twice):
# Round2 Home in front: the menu's own big card (the helper hands it over); any other app: Round2 Alerts over it;
# no Round2 Alerts: LG's pop-up. OK anywhere comes back here (round2d.sh ack / msgack <id>).
menu_card() {   # $1 card JSON: Round2 Home shows it (it's in front)
  luna-send -n 1 luna://com.webos.service.applicationManager/launch "{\"id\":\"$APP\",\"params\":{\"r2\":\"card\",\"card\":$1}}" >/dev/null 2>&1
}
food_show() {   # $1 type, $2 order no, $3 items, $4 "reminder" (shown again on the menu)
  room=$(cut -d' ' -f1 "$CFG/food" 2>/dev/null)
  if [ "$1" = received ]; then txt="Madd Pizza is firing it up. We\u2019ll ping you when it lands."; else txt="It\u2019s at the drop zone, the rack right outside the entrance. Grab it while it\u2019s hot!"; fi
  card="{\"type\":\"$1\",\"no\":\"$2\",\"items\":\"$3\",\"text\":\"$txt\",\"room\":\"$room\",\"reminder\":$([ "$4" = reminder ] && echo true || echo false)}"
  [ "$1" = ready ] && date +%s > "$CFG/food.last"
  [ "$1" = ready ] && echo "{\"type\":\"ready\",\"no\":\"$2\",\"items\":\"$3\",\"text\":\"$txt\",\"room\":\"$room\",\"reminder\":true}" > "$CFG/food.pending"
  if menu_front; then menu_card "$card"; log "food $1 #$2 shown on the menu"; return; fi
  if ov=$(ov_app); then
    if ov_show "$ov" "$card"; then
      log "food $1 #$2 shown (Round2 card) over $(cat "$CFG/front" 2>/dev/null)"
      return
    fi
  fi
  # never LG's own pop-up (owner): Loot dropped stays waiting (food.pending) and food_again tries again in 5 minutes
  [ "$1" = ready ] && echo $(( $(date +%s) - 240 )) > "$CFG/food.last"   # retry in about a minute
  log "food $1 #$2 NOT shown: Round2 Alerts not installed or didn't open – will try again"
}
food_ack() {    # GOT IT pressed: tell Make.com about every order waiting at the pickup point
  set -- $(cat "$CFG/food" 2>/dev/null); room=$1; key=$2
  rm -f "$CFG/food.pending" "$CFG/msg.last"   # OK pressed (on the menu or over an app): no more reminders; a waiting message may show now
  [ -f "$CFG/food.unacked" ] || exit 0
  mv "$CFG/food.unacked" "$CFG/food.acking"
  while read -r ref; do
    [ -n "$ref" ] && curl -s -m 15 $K -d "ref=$ref&room=$room&k=$key" "$ACK" >/dev/null 2>&1 && log "food ack $ref"
  done < "$CFG/food.acking"
  rm -f "$CFG/food.acking"
}
watch_food() {
  command -v curl >/dev/null 2>&1 || { log "food alerts: no curl on this TV"; return; }
  K=""
  while :; do
    set -- $(cat "$CFG/food" 2>/dev/null); room=$1; key=$2
    if [ -z "$room" ] || [ -z "$key" ]; then sleep 30; continue; fi
    ev=""; touch "$CFG/food.alive"
    curl -sN -m 3600 $K -H 'Accept: text/event-stream' "$FB/r2/$key/$room/last.json" 2>/dev/null | while read -r line; do
      touch "$CFG/food.alive"
      case "$line" in
        event:*) ev=${line#event: } ;;
        data:*)
          [ "$ev" = put ] || continue
          echo "$line" | grep -q '"path":"/","data":{' || continue
          t=$(echo "$line" | num t); type=$(echo "$line" | field type); no=$(echo "$line" | field no)
          [ -n "$t" ] || continue
          [ "$t" -le "$(cat "$CFG/food.t" 2>/dev/null || echo 0)" ] && continue
          echo "$t" > "$CFG/food.t"
          age=$(( $(snow) - t / 1000 ))
          case "$type" in
            received) [ "$age" -le 180 ] || continue ;;
            ready) [ "$age" -le 3600 ] || continue
                   ref=$(echo "$line" | field ref); [ -n "$ref" ] && echo "$ref" >> "$CFG/food.unacked" ;;
            *) continue ;;
          esac
          food_show "$type" "$no" "$(echo "$line" | field items | clean)" ;;
      esac
    done
    # an old TV that can't check Firebase's certificate: carry on without checking (alerts only, nothing private)
    if [ -z "$K" ] && ! curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null && curl -s -k -m 10 -o /dev/null "$FB/.json" 2>/dev/null; then
      K="-k"; log "food alerts: certificate check unavailable, continuing without it"
    fi
    sleep 5
  done
}

# --- personal messages over other apps (Theme manager > Messages) ---
# The owners write one message per room to /r2/<key>/<room>/msg ({"id","kind","title","text","from","to","ack","at","until"}).
# While Round2 Home is in front, the menu shows it; otherwise this shows an LG pop-up at "at". One that must be
# acknowledged comes back every 5 minutes until OK is pressed; OK writes /msgAck so the manager shows it was seen.
msg_put() {     # $1 node (msgShown | msgAck), $2 id
  set -- "$1" "$2" $(cat "$CFG/food" 2>/dev/null)
  [ -n "$3" ] && [ -n "$4" ] && curl -s -m 15 $K -X PUT -H 'Content-Type: application/json' \
    -d "{\"id\":\"$2\",\"t\":{\".sv\":\"timestamp\"}}" "$FB/r2/$4/$3/$1.json" >/dev/null 2>&1
}
msg_acked() {   # $1 id: already acknowledged somewhere else (OK pressed on the menu's card)?
  set -- "$1" $(cat "$CFG/food" 2>/dev/null)
  [ -n "$2" ] && [ -n "$3" ] || return 1
  { curl -s -m 10 "$FB/r2/$3/$2/msgAck.json" 2>/dev/null || curl -s -k -m 10 "$FB/r2/$3/$2/msgAck.json" 2>/dev/null; } | grep -q "\"id\":\"$1\""
}
msg_ack() {     # $1 the message's id (from the Round2 card; LG's pop-up: the one last shown). OK pressed.
  id=${1:-$(cat "$CFG/msg.id" 2>/dev/null)}; [ -n "$id" ] || exit 0
  echo "$id" > "$CFG/msg.done"
  # only forget the waiting message if it's still this one: a new message may have arrived a moment ago
  grep -q "\"id\":\"$id\"" "$CFG/msg.cur" 2>/dev/null && rm -f "$CFG/msg.cur"
  K=""; curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null || K="-k"
  msg_put msgAck "$id"; log "message $id acknowledged"
}
msg_alert() {   # $1 page: an LG pop-up with one page of the message; NEXT shows the next page, the last one SEEN IT / CLOSE
  line=$(cat "$CFG/msg.cur" 2>/dev/null); [ -n "$line" ] || return 0
  n=${1:-1}
  title=$(printf '%s\n' "$line" | field title | clean); from=$(printf '%s\n' "$line" | field from | clean)
  room=$(cut -d' ' -f1 "$CFG/food" 2>/dev/null); kind=$(printf '%s\n' "$line" | field kind | clean)
  text=$(printf '%s\n' "$line" | field text | sed 's/\\n/ /g' | clean); to=$(printf '%s\n' "$line" | field to | clean)
  ack=false; printf '%s\n' "$line" | grep -q '"ack":true' && ack=true
  case "$kind" in birthday) tag=BIRTHDAY ;; anniversary) tag=ANNIVERSARY ;; celebrate) tag=CELEBRATION ;; volume) tag="A FRIENDLY REMINDER" ;; wrapup|timeup) tag="SESSION REMINDER" ;; extend) tag="SESSION EXTENDED" ;; *) tag="A MESSAGE" ;; esac
  card="{\"type\":\"msg\",\"kind\":\"$kind\",\"tag\":\"$tag\",\"title\":\"$title\",\"text\":\"$text\",\"from\":\"$from\",\"to\":\"$to\",\"ack\":$ack,\"id\":\"$(printf '%s\n' "$line" | field id)\",\"until\":$(printf '%s\n' "$line" | num until | grep -E '^[0-9]+$' || echo 0),\"now\":$(snow)000,\"room\":\"$room\"}"
  if menu_front; then menu_card "$card"; return 0; fi   # the menu's big card
  if ov=$(ov_app); then ov_show "$ov" "$card" && return 0; fi   # our own card pages the message itself
  return 1   # never LG's own pop-up (owner): the message waits and msg_tick tries again in about a minute
}
card_up() {     # the Round2 Alerts card on screen right now ("msg <id>" / "food <no>"), if it said so in the last 150 s
  set -- $(cat "$CFG/card.up" 2>/dev/null)
  [ -n "$3" ] && [ $(( $(date +%s) - $3 )) -lt 150 ] && echo "$1 $2"
}
food_again() {  # Loot dropped not OK'd and not on screen (TV was off, a message took the card…): again every 5 minutes
  [ -f "$CFG/food.pending" ] || return 0
  p=$(cat "$CFG/food.pending"); no=$(printf '%s\n' "$p" | field no)
  [ "$(card_up)" = "food $no" ] && return 0
  [ $(( $(date +%s) - $(cat "$CFG/food.last" 2>/dev/null || echo 0) )) -ge 300 ] || return 0
  menu_front && return 0                                                  # the menu brings it back itself
  food_show ready "$no" "$(printf '%s\n' "$p" | field items)" reminder
}
# A Wi-Fi blip can leave the Firebase stream silently dead (no error, nothing arrives) until curl's 1-hour limit: messages
# and food alerts sent meanwhile never came (Bella, 1 Nov: "Sending… is the TV on?" while the TV was on). Firebase sends a
# keep-alive every ~30 s, so no line for 2 minutes = dead line: kill that curl and the watcher reconnects in 5 s (and gets
# the current message straight away).
stream_watch() {
  now=$(date +%s)
  for w in msg:msg.json food:last.json; do
    f="$CFG/${w%%:*}.alive"; [ -f "$f" ] || continue
    a=$(date -r "$f" +%s 2>/dev/null || echo "$now")
    if [ $(( now - a )) -gt 120 ]; then
      pkill -f "curl -sN .*/${w#*:}" 2>/dev/null; touch "$f"; log "${w%%:*} stream silent for 2 min: reconnecting"
    fi
  done
}
# Check-in: every 5 minutes the helper writes /r2/<key>/<room>/helper = {t, tv, v, failsafe}. The control centre
# (Round2 messages > On the TVs now) shows a TV as offline when it hasn't checked in for 12 minutes (Bella, 1 Nov: the
# helper didn't start all morning and nobody could tell).
heartbeat() {
  now=$(date +%s); l=$(cat "$CFG/hb.last" 2>/dev/null || echo 0)
  [ $(( now - l )) -ge 300 ] || return 0
  echo "$now" > "$CFG/hb.last"
  set -- $(cat "$CFG/food" 2>/dev/null); [ -n "$1" ] && [ -n "$2" ] || return 0
  v=$(sed -n 's/.*"version" *: *"\([^"]*\)".*/\1/p' /media/developer/apps/usr/palm/applications/sg.round2.tvhome/appinfo.json 2>/dev/null | head -n 1)
  fs=false; [ -e "$FAILSAFE" ] && fs=true
  # what's on screen (owner, 2 Nov): the app or input in front and since when – the control centre names it (HDMI 1 = PS4…)
  fr=$(tr -cd 'A-Za-z0-9._-' < "$CFG/front" 2>/dev/null); fa=$(date -r "$CFG/front" +%s 2>/dev/null || echo 0)
  pw=$(tr -cd 'A-Za-z ' < "$CFG/power" 2>/dev/null)   # Active = screen on; Active Standby / Suspend = asleep (Quick Start+)
  curl -s -m 15 $K -X PUT -H 'Content-Type: application/json' \
    -d "{\"t\":{\".sv\":\"timestamp\"},\"tv\":\"lg\",\"v\":\"$v\",\"failsafe\":$fs,\"ru\":$( [ -f "$CFG/remote.off" ] && echo false || echo true ),\"front\":\"$fr\",\"pw\":\"$pw\",\"frontAt\":$(( fa + $(cat "$CFG/clock.off" 2>/dev/null || echo 0) ))000}" "$FB/r2/$2/$1/helper.json" >/dev/null 2>&1
}
# Homebrew Channel's failsafe mode (after a crash or power cut while the TV was starting): it skips every start-up
# add-on, ours included, so the TV comes on as a plain LG (Bella, 1 Nov). When Round2 Home opens and starts us, clear it
# so the next start-up is normal again.
FAILSAFE=/var/luna/preferences/webosbrew_failsafe
clear_failsafe() {
  [ -e "$FAILSAFE" ] || return 0
  rm -f "$FAILSAFE" && log "Homebrew was in failsafe mode (start-up add-ons skipped): cleared, next start-up is normal"
}
# --- Remote updates (owner, 2 Nov: "Update now" from the control centre) ---
# The control centre asks the engine, which only goes ahead when this TV is online, its room has no session now or within
# 10 minutes, and Staff tools › "Updates from the control centre" allows it (remote.off = not allowed). The engine writes
# /r2/<key>/<room>/cmd = {"id","do":"update","at"}. Homebrew Channel downloads round2-home.ipk and checks its SHA-256
# before anything is replaced; LG's toast says not to switch off; progress goes to /r2/<key>/<room>/update =
# {state: installing | done | failed | uptodate | locked, v, t}. This helper lives outside the app, so if an install is cut
# off (TV switched off) the next start-up finds Round2 Home missing and puts it back (upd_resume).
LGJSON=https://staff.round2.sg/lg.json
APPINFO=/media/developer/apps/usr/palm/applications/$APP/appinfo.json
upd_put() {   # $1 state, $2 version / note
  set -- "$1" "$2" $(cat "$CFG/food" 2>/dev/null)
  [ -n "$3" ] && [ -n "$4" ] && curl -s -m 15 $K -X PUT -H 'Content-Type: application/json' \
    -d "{\"state\":\"$1\",\"v\":\"$2\",\"t\":{\".sv\":\"timestamp\"}}" "$FB/r2/$4/$3/update.json" >/dev/null 2>&1
}
app_ver() { sed -n 's/.*"version" *: *"\([^"]*\)".*/\1/p' "$APPINFO" 2>/dev/null | head -n 1; }
lg_pkg() {    # the newest Round2 Home (or $1) in our Homebrew listing: "version ipkUrl sha256"
  curl -s -m 20 $K "$LGJSON" 2>/dev/null | awk -v id="${1:-$APP}" '
    function val(k,   t) { if (!match($0, "\"" k "\": *\"[^\"]*\"")) return ""; t = substr($0, RSTART, RLENGTH); sub(/^[^:]*: *"/, "", t); sub(/"$/, "", t); return t }
    index($0, "\"" id "\"") { on = 1 }
    on && v == "" { v = val("version") }
    on && u == "" { u = val("ipkUrl") }
    on && h == "" { h = val("sha256") }
    on && v != "" && u != "" && h != "" { print v, u, h; exit }'
}
do_update() {   # $1 remote | repair
  if ! mkdir "$CFG/updating" 2>/dev/null; then
    [ -n "$(find "$CFG/updating" -maxdepth 0 -mmin -10 2>/dev/null)" ] && return 0   # one is already under way
    rm -rf "$CFG/updating"; mkdir "$CFG/updating" 2>/dev/null
  fi
  why=$1; set -- $(lg_pkg)
  if [ -z "$3" ]; then upd_put failed "couldn't read the update list"; log "update ($why): no listing"; rm -rf "$CFG/updating"; return 1; fi
  cur=$(app_ver)
  if [ "$why" = remote ] && [ "$cur" = "$1" ]; then upd_put uptodate "$cur"; log "update: already $cur"; rm -rf "$CFG/updating"; return 0; fi
  echo "$1 $2 $3" > "$CFG/upd.want"
  upd_put installing "$1"; log "update ($why): ${cur:-none} -> $1"
  luna-send -n 1 luna://com.webos.notification/createToast \
    "{\"sourceId\":\"$APP\",\"message\":\"Updating Round2 Home · please don’t switch the TV off (about a minute)\"}" >/dev/null 2>&1
  rm -f "$CFG/upd.out"
  luna-send -i luna://org.webosbrew.hbchannel.service/install "{\"ipkUrl\":\"$2\",\"ipkHash\":\"$3\",\"id\":\"$APP\",\"subscribe\":true}" > "$CFG/upd.out" 2>&1 &
  lp=$!; n=0
  while [ $n -lt 100 ]; do
    sleep 3; n=$((n + 1))
    grep -q -E '"finished" *: *true|"returnValue" *: *false|"errorText"' "$CFG/upd.out" 2>/dev/null && break
  done
  kill $lp 2>/dev/null
  # Homebrew Channel's installer didn't answer (Bella, 2 Nov: "Service Unavailable"): install it ourselves, the way
  # Homebrew does underneath – download, check the SHA-256, hand it to webOS's own installer
  if grep -q -E '"returnValue" *: *false|"errorText"' "$CFG/upd.out" 2>/dev/null; then
    log "Homebrew install failed ($(field errorText < "$CFG/upd.out" | head -n 1 | cut -c1-60)): installing directly"
    rm -f /tmp/r2up.ipk
    if curl -sL -m 240 $K -o /tmp/r2up.ipk "$2" 2>/dev/null && [ "$(sha256sum /tmp/r2up.ipk 2>/dev/null | cut -d' ' -f1)" = "$3" ]; then
      luna-send -i luna://com.webos.appInstallService/dev/install "{\"id\":\"$APP\",\"ipkUrl\":\"/tmp/r2up.ipk\",\"subscribe\":true}" > "$CFG/upd.out2" 2>&1 &
      lp=$!; n=0
      while [ $n -lt 60 ]; do
        sleep 3; n=$((n + 1))
        [ "$(app_ver)" = "$1" ] && break
        grep -q -E '"returnValue" *: *false|"errorText"|"failed"' "$CFG/upd.out2" 2>/dev/null && break
      done
      kill $lp 2>/dev/null; rm -f /tmp/r2up.ipk
      if [ "$(app_ver)" = "$1" ]; then rm -f "$CFG/upd.out"; log "installed directly: $1"
      else cp "$CFG/upd.out2" "$CFG/upd.out" 2>/dev/null; log "direct install failed: $(head -c 200 "$CFG/upd.out2")"; fi
    else
      rm -f /tmp/r2up.ipk; log "direct install: download or SHA-256 check failed"
    fi
  fi
  if grep -q -E '"returnValue" *: *false|"errorText"' "$CFG/upd.out" 2>/dev/null; then
    upd_put failed "$(field errorText < "$CFG/upd.out" | head -n 1 | tr -d '"\\' | cut -c1-80)"; log "update to $1 failed: $(head -c 200 "$CFG/upd.out")"; rm -f "$CFG/upd.want"
  fi
  rm -rf "$CFG/updating"
  upd_resume
  # open the menu once, now the new version is in (LG's home was held back while it installed)
  [ -f "$APPINFO" ] && [ "$(cat "$CFG/front" 2>/dev/null)" != "$APP" ] && { sleep 2; launch update; }
}
upd_resume() {   # an update was asked for: say how it ended, or put Round2 Home back if it was cut off
  if [ ! -f "$CFG/upd.want" ]; then [ -f "$APPINFO" ] && return 0; log "Round2 Home missing: installing it again"; do_update repair; return; fi
  set -- $(cat "$CFG/upd.want" 2>/dev/null); cur=$(app_ver)
  if [ -n "$cur" ] && [ "$cur" = "$1" ]; then upd_put done "$cur"; log "update done: $cur"; rm -f "$CFG/upd.want"; return 0; fi
  [ -d "$CFG/updating" ] && [ -n "$(find "$CFG/updating" -maxdepth 0 -mmin -10 2>/dev/null)" ] && return 0   # still installing
  if [ -z "$cur" ]; then log "Round2 Home missing after an update: installing it again"; do_update repair; return; fi
  upd_put failed "still $cur"; log "update to $1 didn't finish (still $cur)"; rm -f "$CFG/upd.want"
}
cmd_poll() {   # every 20 s: anything from the control centre?
  now=$(date +%s); l=$(cat "$CFG/cmd.last" 2>/dev/null || echo 0)
  [ $(( now - l )) -ge 20 ] || return 0
  echo "$now" > "$CFG/cmd.last"
  [ -f "$CFG/upd.want" ] && upd_resume
  set -- $(cat "$CFG/food" 2>/dev/null); [ -n "$1" ] && [ -n "$2" ] || return 0
  # the control centre's TVs › Refresh (owner, 5 Nov): /ping = {t} → check in now
  pt=$(curl -s -m 10 $K "$FB/r2/$2/$1/ping.json" 2>/dev/null | num t)
  if [ -n "$pt" ] && [ "$pt" != "$(cat "$CFG/ping.last" 2>/dev/null)" ] && [ $(( $(snow) - pt / 1000 )) -le 180 ]; then echo "$pt" > "$CFG/ping.last"; rm -f "$CFG/hb.last"; heartbeat; fi
  c=$(curl -s -m 10 $K "$FB/r2/$2/$1/cmd.json" 2>/dev/null); cid=$(printf '%s\n' "$c" | field id)
  [ -n "$cid" ] && [ "$cid" != "$(cat "$CFG/cmd.done" 2>/dev/null)" ] || return 0
  echo "$cid" > "$CFG/cmd.done"
  ca=$(printf '%s\n' "$c" | num at); [ $(( $(snow) - ${ca:-0} / 1000 )) -le 600 ] || return 0   # older than 10 min: ignore
  case "$(printf '%s\n' "$c" | field do)" in
    update)
      if [ -f "$CFG/remote.off" ]; then upd_put locked "$(app_ver)"; log "remote update refused (Staff tools: not allowed)"
      else ( do_update remote ) </dev/null >/dev/null 2>&1 & fi ;;
    log)   # owner, 4 Nov: the control centre / Claude reads this TV's log without a photo of Diagnostics
      tail -n 300 "$CFG/log.txt" 2>/dev/null | awk '{gsub(/\\/,"\\\\"); gsub(/"/,"\\\""); gsub(/[\001-\037]/," "); printf "%s\\n", $0}' > "$CFG/diag.tmp"
      { printf '{"t":{".sv":"timestamp"},"v":"%s","log":"' "$(app_ver)"; cat "$CFG/diag.tmp"; printf '"}'; } > "$CFG/diag.json"
      curl -s -m 20 $K -X PUT -H 'Content-Type: application/json' --data-binary @"$CFG/diag.json" "$FB/r2/$2/$1/diag.json" >/dev/null 2>&1 && log "log sent to the control centre"
      rm -f "$CFG/diag.tmp" "$CFG/diag.json" ;;
    alerttest)   # owner, 4 Nov: which way of drawing our card shows over Netflix? The control centre picks the version;
                 # it's installed if needed, then shown with a test card (OK closes it; it reports like a real one)
      ta=$(printf '%s\n' "$c" | field app); tr=$(printf '%s\n' "$c" | field raise)
      case "$ta" in sg.round2.alerts|sg.round2.alertspop|sg.round2.alertsflt) ;; *) return 0 ;; esac
      ( if [ ! -d "/media/developer/apps/usr/palm/applications/$ta" ]; then
          set -- $(lg_pkg "$ta"); log "test card: installing $ta $1"
          lp=""; [ -n "$3" ] && { luna-send -i luna://org.webosbrew.hbchannel.service/install "{\"ipkUrl\":\"$2\",\"ipkHash\":\"$3\",\"id\":\"$ta\",\"subscribe\":true}" >/dev/null 2>&1 & lp=$!; }
          n=0; while [ ! -f "/media/developer/apps/usr/palm/applications/$ta/appinfo.json" ] && [ $n -lt 40 ]; do sleep 3; n=$((n + 1)); done
          [ -n "$lp" ] && kill $lp 2>/dev/null; sleep 2
        fi
        room=$(cut -d' ' -f1 "$CFG/food" 2>/dev/null)
        card="{\"type\":\"msg\",\"kind\":\"note\",\"tag\":\"TEST\",\"title\":\"Can you see this?\",\"text\":\"Netflix test ($ta). Press OK on the remote.\",\"from\":\"Round2\",\"to\":\"\",\"ack\":false,\"id\":\"test\",\"until\":0,\"room\":\"$room\"}"
        ov_show "$ta" "$card" && log "test card $ta shown over $(cat "$CFG/front" 2>/dev/null)" || log "test card $ta didn't open"
        if [ "$tr" = 1 ]; then sleep 2; ov_show "$ta" "$card" && log "test card $ta raised again"; fi   # Netflix may take the top back
      ) </dev/null >/dev/null 2>&1 & ;;
  esac
}
msg_tick() {    # every 3 s: is the message due, and is something other than Round2 Home in front?
  while :; do
    sleep 3
    stream_watch
    heartbeat
    cmd_poll
    food_again
    [ -f "$CFG/msg.cur" ] || continue
    line=$(cat "$CFG/msg.cur"); id=$(printf '%s\n' "$line" | field id)
    [ -n "$id" ] || continue
    [ "$id" = "$(cat "$CFG/msg.done" 2>/dev/null)" ] && continue
    now=$(snow); a=$(printf '%s\n' "$line" | num at); u=$(printf '%s\n' "$line" | num until)
    at=$(( ${a:-0} / 1000 )); until=$(( ${u:-0} / 1000 ))
    [ "$until" -gt 0 ] || until=$(( at + 10800 ))
    [ "$now" -ge "$at" ] || continue
    [ "$now" -le "$until" ] || { rm -f "$CFG/msg.cur"; continue; }
    case "$(tr -cd 'A-Za-z ' < "$CFG/power" 2>/dev/null)" in ""|Active) ;; *) continue ;; esac   # TV asleep: the message waits (shows when it's on)
    last=$(cat "$CFG/msg.last" 2>/dev/null || echo 0)
    [ $(( now - last )) -ge 300 ] || continue                                   # at most every 5 minutes
    up=$(card_up); menu_front && up=""                                         # our card never floats over the menu
    [ "$up" = "msg $id" ] && continue                                           # still on screen: stays until OK, no re-chime
    case "$up" in food\ *) continue ;; esac                                     # Loot dropped is up: it comes first
    if msg_acked "$id"; then echo "$id" > "$CFG/msg.done"; rm -f "$CFG/msg.cur"; continue; fi   # seen on the menu's card
    echo "$now" > "$CFG/msg.last"; echo "$id" > "$CFG/msg.id"
    if ! msg_alert 1; then echo $(( now - 240 )) > "$CFG/msg.last"; log "message $id NOT shown (Round2 Alerts missing or didn't open) – tries again in a minute"; continue; fi
    [ "$id" = "$(cat "$CFG/msg.shown" 2>/dev/null)" ] || { echo "$id" > "$CFG/msg.shown"; msg_put msgShown "$id"; }
    [ "$(printf '%s\n' "$line" | grep -c '"ack":true')" -gt 0 ] || echo "$id" > "$CFG/msg.done"
    log "message $id shown over $(cat "$CFG/front" 2>/dev/null)$( [ -n "$(ov_app)" ] && echo ' (Round2 card)')"
  done
}
watch_msg() {
  command -v curl >/dev/null 2>&1 || return
  while :; do
    set -- $(cat "$CFG/food" 2>/dev/null); room=$1; key=$2
    if [ -z "$room" ] || [ -z "$key" ]; then sleep 30; continue; fi
    ev=""; touch "$CFG/msg.alive"
    curl -sN -m 3600 $K -H 'Accept: text/event-stream' "$FB/r2/$key/$room/msg.json" 2>/dev/null | while read -r line; do
      touch "$CFG/msg.alive"                                   # Firebase sends keep-alives every ~30 s: the line is alive
      case "$line" in
        event:*) ev=${line#event: } ;;
        data:*)
          [ "$ev" = put ] || continue
          printf '%s\n' "$line" | grep -q '"path":"/"' || continue
          if printf '%s\n' "$line" | grep -q '"data":null'; then rm -f "$CFG/msg.cur"; continue; fi   # cancelled
          id=$(printf '%s\n' "$line" | field id); [ -n "$id" ] || continue
          [ "$id" = "$(cat "$CFG/msg.done" 2>/dev/null)" ] && continue
          [ "$id" = "$(cat "$CFG/msg.id" 2>/dev/null)" ] || rm -f "$CFG/msg.last"
          printf '%s\n' "$line" > "$CFG/msg.cur" ;;   # printf: keep the message's \n as written
      esac
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
  key)                                      # the Home button, from LG Input Hook. Must stay quick.
    on home || { lg_home "home switch off"; exit 0; }
    t=$(cut -d' ' -f1 /proc/uptime | tr -d .)          # hundredths of a second since the TV started
    set -- $(cat "$CFG/homekey" 2>/dev/null) ; last=${1:-0} ; hits=${2:-0}
    if [ "$last" -gt 0 ] && [ $((t - last)) -ge 0 ] && [ $((t - last)) -le 150 ]; then hits=$((hits + 1)); else hits=1; fi
    if [ "$hits" -ge 3 ]; then echo "0 0" > "$CFG/homekey"; lg_home "Home x3"; exit 0; fi
    echo "$t $hits" > "$CFG/homekey"
    launch home
    exit 0 ;;
  stop) stop; log "stopped"; exit 0 ;;
  ack) K=""; curl -s -m 10 -o /dev/null https://hook.eu1.make.com/ 2>/dev/null || K="-k"; food_ack; exit 0 ;;
  msgack) msg_ack "$2"; exit 0 ;;
  msgpage) msg_alert "$2"; exit 0 ;;   # NEXT pressed on a message pop-up
  ensure)                                   # Round2 Home calls this on every start: keep one up-to-date copy running
    clear_failsafe
    cmp -s "$SELF" "$CFG/round2d.sh" 2>/dev/null || cp "$SELF" "$CFG/round2d.sh"
    if running && [ "$(cat "$CFG/sum" 2>/dev/null)" = "$(sum)" ]; then exit 0; fi ;;
  boot|start|"") ;;
esac

# only one start at a time: Round2 Home can open twice in a second (after an update), and two starts at once
# each left half a helper running (every alert and message handled twice)
if ! mkdir "$CFG/starting" 2>/dev/null; then
  if [ -n "$(find "$CFG/starting" -maxdepth 0 -mmin -1 2>/dev/null)" ]; then exit 0; fi   # another start is under way
  rm -rf "$CFG/starting"; mkdir "$CFG/starting" 2>/dev/null
fi
stop
sum > "$CFG/sum"
[ "$SELF" = "$CFG/round2d.sh" ] || cp "$SELF" "$CFG/round2d.sh"   # a fixed path for the Home button setting
log "helper started ($1)"
rm -f "$CFG/hb.last"; clear_failsafe
date +%s > "$CFG/menukey"
( while :; do clock_sync; sleep 1800; done ) </dev/null >/dev/null 2>&1 &   # the engine's clock, every 30 min
watch_apps </dev/null >/dev/null 2>&1 &
A=$!
watch_power </dev/null >/dev/null 2>&1 &
P=$!
watch_food </dev/null >/dev/null 2>&1 &
F=$!
watch_saver </dev/null >/dev/null 2>&1 &
V=$!
( K=""; curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null || K="-k"; watch_msg ) </dev/null >/dev/null 2>&1 &
M=$!
( K=""; curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null || K="-k"; msg_tick ) </dev/null >/dev/null 2>&1 &
T=$!
printf '%s\n%s\n%s\n%s\n%s\n%s\n' "$A" "$P" "$F" "$M" "$T" "$V" > "$CFG/pids"
rm -rf "$CFG/starting"
[ "$1" = "boot" ] && first_open </dev/null >/dev/null 2>&1 &
( sleep 45; K=""; curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null || K="-k"; upd_resume ) </dev/null >/dev/null 2>&1 &   # an update cut off last time?
# keep the log short
[ -f "$CFG/log.txt" ] && [ "$(wc -l < "$CFG/log.txt")" -gt 300 ] && { tail -n 100 "$CFG/log.txt" > "$CFG/log.tmp"; mv "$CFG/log.tmp" "$CFG/log.txt"; }
exit 0
