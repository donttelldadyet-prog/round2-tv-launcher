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
      [ "$id" != "$prev" ] && { log "front $id"; echo "$id" > "$CFG/front"; }   # for Staff tools > Home helper check, and food alerts
      case "$id" in "$APP"|com.webos.app.home|com.webos.app.launcher|com.webos.surfacemanager*|"") ;; *) last_app=$id ;; esac
      if [ "$id" = "$APP" ] && [ "$id" != "$prev" ]; then   # the guest is back on the menu: anything still waiting for OK
        rm -f "$CFG/msg.last"                                  # an unread message: shown again within seconds
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
      if [ -f "$CFG/relaunch" ] && [ "$id" != "$APP" ]; then          # Round2 Home just updated itself
        rm -f "$CFG/relaunch"; sleep 1; launch update; prev=$APP; continue
      fi
      case "$id" in
        com.webos.app.home|com.webos.app.launcher)
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
watch_power() {
  while :; do
    last=""
    luna-send -i luna://com.webos.service.tvpower/power/getPowerState '{"subscribe":true}' 2>/dev/null | while read -r line; do
      st=$(echo "$line" | field state)
      [ -z "$st" ] && continue
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
clean() { tr -d "\\\\\"'"; }   # no quotes or backslashes: safe inside the luna-send JSON below
# --- Round2 Alerts (sg.round2.alerts, from Homebrew Channel): the Round2 card over other apps instead of LG's pop-ups.
# Without it installed, LG's own pop-ups are used, so nothing is ever missed.
ov_app() {
  [ -d /media/developer/apps/usr/palm/applications/sg.round2.alerts ] || return 1
  echo sg.round2.alerts
}
ov_show() {     # $1 app id, $2 params JSON. Fails (so the caller uses LG's pop-up) if webOS won't open it.
  r=$(luna-send -n 1 luna://com.webos.service.applicationManager/launch "{\"id\":\"$1\",\"params\":$2}" 2>&1)
  case "$r" in *'"returnValue":true'*|*'"returnValue": true'*) return 0 ;; esac
  log "Round2 Alerts didn't open: $(echo "$r" | tr -d '\n' | cut -c1-120)"
  return 1
}
# Is Round2 Home in front right now? (asked fresh: the menu shows alerts itself then)
menu_front() {
  id=$(luna-send -n 1 luna://com.webos.applicationManager/getForegroundAppInfo '{}' 2>/dev/null | field appId)
  case "$id" in sg.round2.alerts*|"") id=$(cat "$CFG/front" 2>/dev/null) ;; esac
  [ "$id" = "$APP" ]
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
  log "food $1 #$2: LG's pop-up (Round2 Alerts not installed or didn't open)"
  if [ "$1" = received ]; then
    luna-send -n 1 luna://com.webos.notification/createToast \
      "{\"sourceId\":\"$APP\",\"message\":\"ORDER #$2 LOCKED IN \u00b7 Madd Pizza is firing it up. We\u2019ll ping you when it lands.\"}" >/dev/null 2>&1
  else
    luna-send -n 1 luna://com.webos.notification/createAlert \
      "{\"sourceId\":\"$APP\",\"title\":\"LOOT DROPPED \u00b7 ORDER #$2\",\"message\":\"It\u2019s at the drop zone \u2014 the rack right outside the entrance. Grab it while it\u2019s hot!\\n\\n$3\",\"modal\":false,\"buttons\":[{\"label\":\"GOT IT\",\"onclick\":\"luna://org.webosbrew.hbchannel.service/exec\",\"params\":{\"command\":\"sh $CFG/round2d.sh ack\"}}]}" >/dev/null 2>&1
  fi
  log "food $1 #$2 shown over $(cat "$CFG/front" 2>/dev/null)"
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
    ev=""
    curl -sN -m 3600 $K -H 'Accept: text/event-stream' "$FB/r2/$key/$room/last.json" 2>/dev/null | while read -r line; do
      case "$line" in
        event:*) ev=${line#event: } ;;
        data:*)
          [ "$ev" = put ] || continue
          echo "$line" | grep -q '"path":"/","data":{' || continue
          t=$(echo "$line" | num t); type=$(echo "$line" | field type); no=$(echo "$line" | field no)
          [ -n "$t" ] || continue
          [ "$t" -le "$(cat "$CFG/food.t" 2>/dev/null || echo 0)" ] && continue
          echo "$t" > "$CFG/food.t"
          age=$(( $(date +%s) - t / 1000 ))
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
  card="{\"type\":\"msg\",\"kind\":\"$kind\",\"tag\":\"$tag\",\"title\":\"$title\",\"text\":\"$text\",\"from\":\"$from\",\"to\":\"$to\",\"ack\":$ack,\"id\":\"$(printf '%s\n' "$line" | field id)\",\"until\":$(printf '%s\n' "$line" | num until | grep -E '^[0-9]+$' || echo 0),\"room\":\"$room\"}"
  if menu_front; then menu_card "$card"; return 0; fi   # the menu's big card
  if ov=$(ov_app); then ov_show "$ov" "$card" && return 0; fi   # our own card pages the message itself
  printf '%s\n' "$line" | field text | sed 's/\\n/ /g' | clean | { if command -v fold >/dev/null 2>&1; then fold -s -w 110; else cat; fi; } > "$CFG/msg.pages"
  total=$(wc -l < "$CFG/msg.pages"); [ "$total" -ge 1 ] || total=1
  text=$(sed -n "${n}p" "$CFG/msg.pages")
  # ◀ ▶ move between the pop-up's buttons: PAGE BACK re-reads, NEXT PAGE turns; only the last page has the OK that closes it
  hb="luna://org.webosbrew.hbchannel.service/exec"
  btns=""
  [ "$n" -gt 1 ] && btns="{\"label\":\"\u25C0 PAGE BACK\",\"onclick\":\"$hb\",\"params\":{\"command\":\"sh $CFG/round2d.sh msgpage $((n - 1))\"}},"
  if [ "$n" -lt "$total" ]; then
    btns="$btns{\"label\":\"NEXT PAGE \u25B6 $n/$total\",\"onclick\":\"$hb\",\"params\":{\"command\":\"sh $CFG/round2d.sh msgpage $((n + 1))\"}}"
  else
    [ -n "$from" ] && text="$text\\n\u2014 $from"
    if printf '%s\n' "$line" | grep -q '"ack":true'; then ok="OK \u00B7 SEEN IT (CLOSES THIS)"; cmd="sh $CFG/round2d.sh msgack $(printf '%s\n' "$line" | field id)"; else ok="OK \u00B7 CLOSE"; cmd="true"; fi
    btns="$btns{\"label\":\"$ok\",\"onclick\":\"$hb\",\"params\":{\"command\":\"$cmd\"}}"
  fi
  [ "$total" -gt 1 ] && title="$title ($n/$total)"
  luna-send -n 1 luna://com.webos.notification/createAlert \
    "{\"sourceId\":\"$APP\",\"title\":\"$title\",\"message\":\"$text\",\"modal\":false,\"buttons\":[$btns]}" >/dev/null 2>&1
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
msg_tick() {    # every 3 s: is the message due, and is something other than Round2 Home in front?
  while :; do
    sleep 3
    food_again
    [ -f "$CFG/msg.cur" ] || continue
    line=$(cat "$CFG/msg.cur"); id=$(printf '%s\n' "$line" | field id)
    [ -n "$id" ] || continue
    [ "$id" = "$(cat "$CFG/msg.done" 2>/dev/null)" ] && continue
    now=$(date +%s); a=$(printf '%s\n' "$line" | num at); u=$(printf '%s\n' "$line" | num until)
    at=$(( ${a:-0} / 1000 )); until=$(( ${u:-0} / 1000 ))
    [ "$until" -gt 0 ] || until=$(( at + 10800 ))
    [ "$now" -ge "$at" ] || continue
    [ "$now" -le "$until" ] || { rm -f "$CFG/msg.cur"; continue; }
    last=$(cat "$CFG/msg.last" 2>/dev/null || echo 0)
    [ $(( now - last )) -ge 300 ] || continue                                   # at most every 5 minutes
    up=$(card_up)
    [ "$up" = "msg $id" ] && continue                                           # still on screen: stays until OK, no re-chime
    case "$up" in food\ *) continue ;; esac                                     # Loot dropped is up: it comes first
    if msg_acked "$id"; then echo "$id" > "$CFG/msg.done"; rm -f "$CFG/msg.cur"; continue; fi   # seen on the menu's card
    echo "$now" > "$CFG/msg.last"; echo "$id" > "$CFG/msg.id"
    [ "$id" = "$(cat "$CFG/msg.shown" 2>/dev/null)" ] || { echo "$id" > "$CFG/msg.shown"; msg_put msgShown "$id"; }
    [ "$(printf '%s\n' "$line" | grep -c '"ack":true')" -gt 0 ] || echo "$id" > "$CFG/msg.done"
    msg_alert 1
    log "message $id shown over $(cat "$CFG/front" 2>/dev/null)$( [ -n "$(ov_app)" ] && echo ' (Round2 card)')"
  done
}
watch_msg() {
  command -v curl >/dev/null 2>&1 || return
  while :; do
    set -- $(cat "$CFG/food" 2>/dev/null); room=$1; key=$2
    if [ -z "$room" ] || [ -z "$key" ]; then sleep 30; continue; fi
    ev=""
    curl -sN -m 3600 $K -H 'Accept: text/event-stream' "$FB/r2/$key/$room/msg.json" 2>/dev/null | while read -r line; do
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
watch_apps </dev/null >/dev/null 2>&1 &
A=$!
watch_power </dev/null >/dev/null 2>&1 &
P=$!
watch_food </dev/null >/dev/null 2>&1 &
F=$!
( K=""; curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null || K="-k"; watch_msg ) </dev/null >/dev/null 2>&1 &
M=$!
( K=""; curl -s -m 10 -o /dev/null "$FB/.json" 2>/dev/null || K="-k"; msg_tick ) </dev/null >/dev/null 2>&1 &
T=$!
printf '%s\n%s\n%s\n%s\n%s\n' "$A" "$P" "$F" "$M" "$T" > "$CFG/pids"
rm -rf "$CFG/starting"
[ "$1" = "boot" ] && first_open </dev/null >/dev/null 2>&1 &
# keep the log short
[ -f "$CFG/log.txt" ] && [ "$(wc -l < "$CFG/log.txt")" -gt 300 ] && { tail -n 100 "$CFG/log.txt" > "$CFG/log.tmp"; mv "$CFG/log.tmp" "$CFG/log.txt"; }
exit 0
