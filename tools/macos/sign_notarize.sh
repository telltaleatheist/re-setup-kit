#!/bin/bash
# Signs a Mac app and its DMG with a Developer ID, and notarizes and staples the DMG, for any game or launcher
# built with the kit (README.md, Mac signing). Everything that names a person or a team comes from the
# environment, so none of it is ever in a repo:
#
#   RE_SIGN_IDENTITY    the keychain's "Developer ID Application: ..." identity, by its name or SHA-1
#                       (security find-identity -v -p codesigning). Unset: ad hoc ("-").
#   RE_NOTARY_PROFILE   a notarytool keychain profile (xcrun notarytool store-credentials).
#   RE_ALLOW_UNSIGNED=1 lets a build that will not be notarized go on (development and check builds).
#                       Without it, such a build is refused, so an unnotarized DMG never reaches a release.
#   MW3_SIGN_IDENTITY, MW3_NOTARY_PROFILE and MW3_ALLOW_UNSIGNED are read when the RE_ ones are unset.
#
# Commands:
#   sign_notarize.sh check
#       Before anything is built: prints "notarize" or "unsigned" (allowed, and why, on stderr); exit 1 when
#       refused.
#   sign_notarize.sh app APP [--entitlements FILE] [--entitlements-for PATH FILE]...
#       Every Mach-O in APP (libraries, helper programs), then every bundle inside it (deepest first), then APP.
#       With an identity: the hardened runtime and a secure timestamp, as notarization requires. --entitlements
#       is APP's own; --entitlements-for gives a file inside APP (a path relative to APP) its own. Then
#       codesign --verify --deep --strict.
#   sign_notarize.sh dmg DMG [APP]
#       DMG signed. When notarizing: submitted (--wait), refused with Apple's log unless Accepted, its ticket
#       stapled to DMG and to APP (the same app as inside it, when given), validated, and Gatekeeper's verdict
#       on both checked.
set -euo pipefail

ID="${RE_SIGN_IDENTITY:-${MW3_SIGN_IDENTITY:--}}"
PROFILE="${RE_NOTARY_PROFILE:-${MW3_NOTARY_PROFILE:-}}"
ALLOW="${RE_ALLOW_UNSIGNED:-${MW3_ALLOW_UNSIGNED:-}}"

usage() { sed -n '/^# Commands:/,/^set -euo/p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//' >&2; exit 2; }

# NOTARIZE=1, or 0 with WHY.
decide() {
  NOTARIZE=0 WHY=""
  if [ "$ID" = "-" ]; then WHY="no RE_SIGN_IDENTITY (ad hoc)"
  elif [ -z "$PROFILE" ]; then WHY="no RE_NOTARY_PROFILE"
  elif ! xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null 2>&1; then
    WHY="no notarytool profile \"$PROFILE\" in the keychain (xcrun notarytool store-credentials)"
  else NOTARIZE=1; fi
  if [ "$NOTARIZE" = 0 ] && [ "$ALLOW" != 1 ]; then
    echo "refused: this DMG would not be notarized: $WHY. Set RE_ALLOW_UNSIGNED=1 for a build not for release." >&2
    exit 1
  fi
}

sign_flags() {
  if [ "$ID" = "-" ]; then SIGN=(--force --sign -); else SIGN=(--force --timestamp --options runtime --sign "$ID"); fi
}

# A bundle's main executable, signed with its bundle rather than on its own.
main_of() {
  local exe
  exe=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$1/Contents/Info.plist" 2>/dev/null) || return 0
  [ -n "$exe" ] && printf '%s\n' "$1/Contents/MacOS/$exe"
}

cmd_app() {
  local app="${1:?sign_notarize.sh app APP}"; shift
  [ -d "$app/Contents" ] || { echo "not an app: $app" >&2; exit 1; }
  app="${app%/}"
  local own="" ; local -a rel=() ents=()
  while [ $# -gt 0 ]; do
    case "$1" in
      --entitlements) own="${2:?--entitlements FILE}"; shift 2 ;;
      --entitlements-for) rel+=("${2:?--entitlements-for PATH FILE}"); ents+=("${3:?--entitlements-for PATH FILE}"); shift 3 ;;
      *) usage ;;
    esac
  done
  decide
  sign_flags
  [ "$ID" = "-" ] && echo "note: signed ad hoc: macOS asks once before the first open" >&2
  # Inner bundles (.app, .framework, .bundle, .appex, .xpc), deepest first; their main executables.
  local -a bundles=() mains=()
  while IFS= read -r b; do bundles+=("$b"); done < <(find "$app/Contents" -type d \( -name '*.app' -o -name '*.framework' \
      -o -name '*.bundle' -o -name '*.appex' -o -name '*.xpc' \) | awk '{ print gsub("/", "/"), $0 }' | sort -rn | cut -d' ' -f2-)
  for b in "$app" ${bundles[@]+"${bundles[@]}"}; do
    while IFS= read -r m; do mains+=("$m"); done < <(main_of "$b")
  done
  ent_for() {  # the entitlements flag for one path, if any
    local i
    [ "${#rel[@]}" -gt 0 ] || return 0
    for i in "${!rel[@]}"; do
      if [ "$1" = "$app/${rel[$i]}" ]; then printf '%s\n' "--entitlements" "${ents[$i]}"; return; fi
    done
  }
  local f m skip
  while IFS= read -r -d '' f; do
    [[ "$(file -b "$f")" == Mach-O* ]] || continue
    skip=0
    for m in ${mains[@]+"${mains[@]}"}; do [ "$f" = "$m" ] && skip=1; done
    [ "$skip" = 1 ] && continue
    local -a e=(); while IFS= read -r x; do e+=("$x"); done < <(ent_for "$f")
    codesign "${SIGN[@]}" ${e[@]+"${e[@]}"} "$f"
  done < <(find "$app/Contents" -type f -print0)
  for b in ${bundles[@]+"${bundles[@]}"}; do
    local -a e=(); while IFS= read -r x; do e+=("$x"); done < <(ent_for "$b")
    codesign "${SIGN[@]}" ${e[@]+"${e[@]}"} "$b"
  done
  if [ -n "$own" ]; then codesign "${SIGN[@]}" --entitlements "$own" "$app"; else codesign "${SIGN[@]}" "$app"; fi
  codesign --verify --deep --strict --verbose=2 "$app"
}

cmd_dmg() {
  local dmg="${1:?sign_notarize.sh dmg DMG [APP]}" app="${2:-}"
  [ -f "$dmg" ] || { echo "no DMG: $dmg" >&2; exit 1; }
  decide
  if [ "$ID" != "-" ]; then codesign --force --timestamp --sign "$ID" "$dmg"; fi
  [ "$NOTARIZE" = 1 ] || return 0
  echo "Notarizing $dmg (a few minutes)..."
  local said status
  said=$(xcrun notarytool submit "$dmg" --keychain-profile "$PROFILE" --wait --output-format json) || true
  status=$(printf '%s' "$said" | python3 -I -c 'import json,sys; d=json.load(sys.stdin); print(d.get("status",""), d.get("id",""))' 2>/dev/null || true)
  if [ "${status%% *}" != "Accepted" ]; then
    echo "notarization: ${status:-no answer}" >&2
    [ -n "${status#* }" ] && xcrun notarytool log "${status#* }" --keychain-profile "$PROFILE" >&2 || true
    exit 1
  fi
  echo "notarization: Accepted (${status#* })"
  xcrun stapler staple "$dmg"
  xcrun stapler validate "$dmg"
  spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
  if [ -n "$app" ]; then
    xcrun stapler staple "$app"
    spctl --assess --type execute --verbose=2 "$app"
  fi
}

case "${1:-}" in
  check)
    decide
    if [ "$NOTARIZE" = 1 ]; then echo notarize; else echo "note: not notarized: $WHY (RE_ALLOW_UNSIGNED=1)" >&2; echo unsigned; fi ;;
  app) shift; cmd_app "$@" ;;
  dmg) shift; cmd_dmg "$@" ;;
  *) usage ;;
esac
