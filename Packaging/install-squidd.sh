#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    exec /usr/bin/sudo "$0" "$@"
fi

resource_root="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
install_root="/Library/Application Support/Squid"
plist_path="/Library/LaunchDaemons/com.squid.squidd.plist"

/usr/bin/install -d "$install_root/lib"
/bin/cp "$resource_root/squidd" "$install_root/squidd"
/bin/cp "$resource_root/liblibsquid.dylib" "$install_root/lib/liblibsquid.dylib"
/bin/cp "$resource_root/com.squid.squidd.plist" "$plist_path"

/usr/sbin/chown -R root:wheel "$install_root"
/bin/chmod 755 "$install_root" "$install_root/squidd"
/bin/chmod 755 "$install_root/lib"
/bin/chmod 644 "$install_root/lib/liblibsquid.dylib" "$plist_path"
/usr/sbin/chown root:wheel "$plist_path"

/bin/launchctl bootout system/com.squid.squidd 2>/dev/null || true
/bin/launchctl bootstrap system "$plist_path"
/bin/launchctl enable system/com.squid.squidd

echo "Squidd is installed and running as the root LaunchDaemon com.squid.squidd."
