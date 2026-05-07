#!/usr/bin/env bash
# Flash a previously built node firmware to a Dongle in DFU mode.
#
# Usage:
#   scripts/flash_node.sh <node_id> [tty_path]
#
# If tty_path is omitted, the script auto-detects the (single) Dongle currently
# in Open DFU Bootloader mode. If multiple Dongles are in DFU mode at once it
# will refuse to guess and you must pass the path explicitly.
#
# Environment overrides:
#   NRFUTIL  -- path to the nrfutil binary (default: ~/.local/bin/nrfutil)

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <node_id> [tty_path]" >&2
    exit 2
fi

NODE_ID="$1"
TTY_PATH="${2:-}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZIP_PATH="$REPO_ROOT/tutorial/nRF52840/build/node${NODE_ID}/mixer_node${NODE_ID}.zip"
NRFUTIL="${NRFUTIL:-$HOME/.local/bin/nrfutil}"

if [[ ! -f "$ZIP_PATH" ]]; then
    echo "error: $ZIP_PATH not found" >&2
    echo "  build it first: scripts/build_node.sh $NODE_ID" >&2
    exit 1
fi
if [[ ! -x "$NRFUTIL" ]]; then
    echo "error: nrfutil not found at $NRFUTIL" >&2
    exit 1
fi

# Auto-detect a Dongle in DFU mode if no path was given.
if [[ -z "$TTY_PATH" ]]; then
    DFU_PORTS=()
    for dev in /dev/ttyACM*; do
        [[ -e "$dev" ]] || continue
        if udevadm info -q property -n "$dev" 2>/dev/null \
            | grep -q "ID_MODEL=Open_DFU_Bootloader"; then
            DFU_PORTS+=("$dev")
        fi
    done
    case "${#DFU_PORTS[@]}" in
        0)
            echo "error: no Dongle in DFU mode detected" >&2
            echo "  press the RESET button on the target Dongle, then re-run" >&2
            exit 1
            ;;
        1)
            TTY_PATH="${DFU_PORTS[0]}"
            echo "[flash_node] auto-detected DFU port: $TTY_PATH"
            ;;
        *)
            echo "error: multiple Dongles in DFU mode -- pass the tty path explicitly" >&2
            for p in "${DFU_PORTS[@]}"; do
                SERIAL=$(udevadm info -q property -n "$p" 2>/dev/null \
                    | awk -F= '$1=="ID_SERIAL_SHORT"{print $2}')
                echo "  $p (serial=$SERIAL)" >&2
            done
            exit 1
            ;;
    esac
fi

# Sanity-check the chosen port really is a Dongle in DFU mode.
MODEL=$(udevadm info -q property -n "$TTY_PATH" 2>/dev/null \
    | awk -F= '$1=="ID_MODEL"{print $2}')
if [[ "$MODEL" != "Open_DFU_Bootloader" ]]; then
    echo "warning: $TTY_PATH ID_MODEL=$MODEL (expected Open_DFU_Bootloader)" >&2
    echo "  proceeding anyway -- if this fails, press RESET to enter DFU mode" >&2
fi

echo "[flash_node] node_id=$NODE_ID -> $TTY_PATH"
"$NRFUTIL" nrf5sdk-tools dfu usb-serial -pkg "$ZIP_PATH" -p "$TTY_PATH"
