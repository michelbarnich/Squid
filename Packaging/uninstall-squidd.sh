#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    exec /usr/bin/sudo "$0" "$@"
fi

plist_path="/Library/LaunchDaemons/com.squid.squidd.plist"
install_root="/Library/Application Support/Squid"

/bin/launchctl bootout system/com.squid.squidd 2>/dev/null || true
/bin/rm -f "$plist_path"
/bin/rm -rf "$install_root"

/usr/bin/logger "Squid squidd LaunchDaemon removed"
echo "Squidd and its root LaunchDaemon have been removed."
