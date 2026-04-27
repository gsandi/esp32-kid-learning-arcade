#!/usr/bin/env bash
# ESP32 Kid Learning Arcade — macOS one-shot setup
#
# Installs everything you need to flash the device from a fresh MacBook:
#   1. Xcode Command Line Tools (gives you git + compilers)
#   2. Homebrew                  (the Mac package manager)
#   3. PlatformIO                (the embedded build system)
#
# Re-runnable. Anything already installed is skipped.

set -euo pipefail

bold()   { printf "\n\033[1m%s\033[0m\n" "$*"; }
green()  { printf "\033[32m%s\033[0m\n" "$*"; }
yellow() { printf "\033[33m%s\033[0m\n" "$*"; }
red()    { printf "\033[31m%s\033[0m\n" "$*"; }

bold "ESP32 Kid Learning Arcade — macOS setup"
echo "This will install the tools needed to flash the device."
echo "It's safe to re-run; anything already installed is skipped."

# -----------------------------------------------------------------------------
# 1. Xcode Command Line Tools (provides git, clang, etc.)
# -----------------------------------------------------------------------------
bold "[1/4] Xcode Command Line Tools"
if ! xcode-select -p &>/dev/null; then
  echo "Not installed. Triggering Apple's installer..."
  echo "A system dialog will pop up — click \"Install\" and wait for it to finish (5–10 min)."
  xcode-select --install || true
  echo
  yellow "After the Xcode Command Line Tools install completes, re-run this script:"
  echo "    ./scripts/setup-macos.sh"
  exit 0
fi
green "✓ Xcode Command Line Tools already installed"

# -----------------------------------------------------------------------------
# 2. Homebrew
# -----------------------------------------------------------------------------
bold "[2/4] Homebrew"
if ! command -v brew &>/dev/null; then
  echo "Installing Homebrew (you may be asked for your Mac password)..."
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

  # Apple Silicon Macs install brew to /opt/homebrew and need PATH wiring.
  if [[ -x /opt/homebrew/bin/brew ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
    if ! grep -q 'brew shellenv' "${HOME}/.zprofile" 2>/dev/null; then
      echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> "${HOME}/.zprofile"
    fi
  fi
fi
green "✓ Homebrew installed: $(brew --version | head -1)"

# -----------------------------------------------------------------------------
# 3. PlatformIO
# -----------------------------------------------------------------------------
bold "[3/4] PlatformIO"
if ! command -v pio &>/dev/null; then
  echo "Installing PlatformIO via Homebrew..."
  brew install platformio
fi
green "✓ PlatformIO installed: $(pio --version)"

# -----------------------------------------------------------------------------
# 4. Detect the board (informational)
# -----------------------------------------------------------------------------
bold "[4/4] Looking for the ESP32 board"
PORT=$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* 2>/dev/null | head -1 || true)

if [[ -z "${PORT:-}" ]]; then
  yellow "No board detected yet. That's fine if you haven't plugged it in."
  echo
  echo "When you're ready:"
  echo "  1. Plug the ESP32 into your Mac with a USB-C DATA cable."
  echo "     (Many USB-C cables are charge-only and won't work.)"
  echo "  2. If macOS still doesn't see the board, install the CH340 driver:"
  echo "       https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html"
  echo "     Reboot after installing, then re-run this script."
else
  green "✓ Board found at: ${PORT}"
fi

# -----------------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------------
bold "All set."
echo "Plug in the ESP32 (if not already), then run:"
echo
echo "    pio run -t upload"
echo
echo "To watch serial logs after flashing (optional):"
echo
echo "    pio device monitor"
echo
yellow "Tip: if 'pio' isn't found, quit Terminal and open a new window. PATH changes only apply to new shells."
