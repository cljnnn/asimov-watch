#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")" || return; pwd -P)"
BIN_DIR="/usr/local/bin"
BIN_PATH="$BIN_DIR/asimov-watch"
PLIST_NAME="pm.tea.asimov.watch.plist"
LAUNCH_AGENTS_DIR="$HOME/Library/LaunchAgents"
PLIST_DEST="$LAUNCH_AGENTS_DIR/$PLIST_NAME"

echo "Compiling asimov-watch..."
clang++ -std=c++17 -framework CoreServices -O3 "$DIR/main.cpp" -o asimov-watch

# Ensure bin directory exists
if [ ! -d "$BIN_DIR" ]; then
    echo "Creating $BIN_DIR..."
    if [ -w /usr/local ]; then
        mkdir -p "$BIN_DIR"
    else
        sudo mkdir -p "$BIN_DIR"
    fi
fi

echo "Installing binary to $BIN_PATH..."
if [ -w "$BIN_DIR" ]; then
    mv asimov-watch "$BIN_PATH"
else
    sudo mv asimov-watch "$BIN_PATH"
fi

# Ensure LaunchAgents directory exists
if [ ! -d "$LAUNCH_AGENTS_DIR" ]; then
    echo "Creating $LAUNCH_AGENTS_DIR..."
    mkdir -p "$LAUNCH_AGENTS_DIR"
fi

echo "Configuring Launch Agent..."
sed "s|USER_HOME_PLACEHOLDER|$HOME|g" "$DIR/$PLIST_NAME" > "$PLIST_DEST"

echo "Unloading old agent (if any)..."
# Unload just in case
launchctl unload "$PLIST_DEST" 2>/dev/null || true

echo "Loading new agent..."
launchctl load "$PLIST_DEST"

echo "✅ Asimov Watch is safe and running!"
echo "   Logs at /tmp/asimov.watch.log"
