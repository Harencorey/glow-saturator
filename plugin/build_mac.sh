#!/bin/bash
# Build Glow (AU + VST3 + Standalone) on macOS and install the plug-ins.
#   bash build_mac.sh                 -> downloads JUCE 8.0.15 automatically
#   bash build_mac.sh /path/to/JUCE   -> uses your own JUCE checkout instead
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "CMake not found. Install it with:  brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode command line tools missing. Run:  xcode-select --install"; exit 1; }

JUCE_ARG=()
if [ $# -ge 1 ]; then JUCE_ARG=(-DJUCE_DIR="$1"); fi

# Plain Makefiles: works with just the command line tools (no full Xcode needed)
cmake -B build-mac -DCMAKE_BUILD_TYPE=Release "${JUCE_ARG[@]+"${JUCE_ARG[@]}"}"
cmake --build build-mac --config Release --target Glow_AU Glow_VST3 Glow_Standalone -j "$(sysctl -n hw.ncpu)"

# COPY_PLUGIN_AFTER_BUILD already installed them; make sure Logic/GarageBand see the new AU right away
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Installed:"
ls -d ~/Library/Audio/Plug-Ins/Components/Glow.component ~/Library/Audio/Plug-Ins/VST3/Glow.vst3 2>/dev/null || true
echo "Standalone app: $(pwd)/build-mac/Glow_artefacts/Release/Standalone/Glow.app"
echo "Rescan plug-ins in your DAW and look for Norka DSP > Glow.  (Logic: auval -v aufx Glw1 Nrka)"
