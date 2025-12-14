# Asimov Watch

A lightweight, real-time daemon for macOS that automatically prevents development dependencies (like `node_modules`, `vendor`) from being backed up by Time Machine.

## Inspiration

This project is a real-time evolution of the original [Asimov](https://github.com/stevegrunwell/asimov) by Steve Grunwell. While the original Asimov scans your system periodically (e.g., daily), **Asimov Watch** listens for file system events and applies exclusions **the instant** a dependency directory is created.

## Why?

Daily scans leave a "Backup Window": if you create a heavy `node_modules` folder at 10:00 AM and Time Machine runs at 11:00 AM, that folder gets backed up before the daily scan runs. Asimov Watch closes this gap.

## Features

*   **⚡️ Real-time**: Uses macOS native `FSEvents` API to detect file creations instantly.
*   **🚀 Extremely Fast**: Written in pure C. Compiles to a tiny binary with negligible footprint.
*   **🧠 Smart Optimization**:
    *   **Xattr Caching**: Checks filesystem Extended Attributes to avoid redundant operations.
    *   **Prefix Ignore**: Automatically ignores events inside already-excluded directories (perfect for `npm install` storms).
    *   **Noise Filtering**: Ignores high-traffic folders like `~/Library` and `~/.Trash`.
*   **🔍 Initial Scan**: Performs a background scan on startup to catch anything missed while the daemon was off.

## Supported Dependencies

*   **Node.js**: `node_modules` (via `package.json`)
*   **PHP/Composer**: `vendor` (via `composer.json`)
*   **Python**: `venv` (via `requirements.txt`)
*   **Ruby**: `vendor` (via `Gemfile`)
*   **Rust**: `target` (via `Cargo.toml`)

## Installation

```bash
cd asimov-watch
sh install-watch.sh
```

This script will:
1.  Compile the source code (`main.cpp`).
2.  Install the binary to `/usr/local/bin/asimov-watch`.
3.  Register a Launch Agent to keep it running in the background (starting at login).

## Configuration

By default, Asimov Watch ignores the `Library` and `.Trash` directories in your home folder.

You can customize this by editing `~/Library/LaunchAgents/pm.tea.asimov.watch.plist` and adding arguments to the `ProgramArguments` array:

```xml
<array>
    <string>/usr/local/bin/asimov-watch</string>
    <string>/Users/yourname</string>
    <string>Library</string>
    <string>.Trash</string>
    <string>Downloads</string> <!-- Added -->
    <string>Public</string>    <!-- Added -->
</array>
```

Then reload the agent:
```bash
launchctl unload ~/Library/LaunchAgents/pm.tea.asimov.watch.plist
launchctl load ~/Library/LaunchAgents/pm.tea.asimov.watch.plist
```

## Logs

You can view the logs (including what gets excluded) at:

```bash
cat /tmp/asimov.watch.log
```

## License

MIT
