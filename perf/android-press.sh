#!/usr/bin/env bash
# Press buzzer buttons in a running bounded Android test (buzz_test_input_file).
#   android-press.sh [-g gap_s] P1r P2b ...   (P<1-4><r|y|g|o|b>)
# Writes "<seq>:<hex mask>" into the app's files/test-input.txt via run-as.
export MSYS_NO_PATHCONV=1
ADB=${ADB:-adb}; GAP=2
[ "${1:-}" = -g ] && { GAP=$2; shift 2; }
seq=$($ADB shell run-as org.buzzrecomp.game cat files/test-input.txt 2>/dev/null | cut -d: -f1 | tr -d '\r')
seq=${seq:-0}
for p in "$@"; do
  player=${p:1:1}; button=${p:2:1}
  case $button in r) b=0;; y) b=1;; g) b=2;; o) b=3;; b) b=4;; *) echo "bad press $p"; exit 1;; esac
  mask=$(( 1 << ((player - 1) * 5 + b) ))
  seq=$((seq + 1))
  $ADB shell "run-as org.buzzrecomp.game sh -c 'echo $seq:$(printf %x $mask) > files/test-input.txt'"
  sleep "$GAP"
done
