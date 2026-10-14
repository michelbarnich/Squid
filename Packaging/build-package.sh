#!/bin/sh
set -eu

project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
products_dir="${PRODUCTS_DIR:-$project_root/Products}"
output_dir="${OUTPUT_DIR:-$project_root/Products}"
staging_app="$project_root/.build/squid-manager-bundle"
output_app="$output_dir/Squid.app"
resource_root="$staging_app/Contents/Resources/Squid"

rm -rf "$staging_app"
mkdir -p "$resource_root/lib" "$output_dir"

if [ ! -d "$products_dir/Squid.app" ] || [ ! -x "$products_dir/squidd" ] || [ ! -f "$products_dir/liblibsquid.dylib" ]; then
    echo "error: build the Release products first, or set PRODUCTS_DIR to a directory containing:" >&2
    echo "       Squid.app, squidd, and liblibsquid.dylib" >&2
    exit 1
fi

/usr/bin/ditto --norsrc --noextattr "$products_dir/Squid.app" "$staging_app"
/usr/bin/ditto --norsrc --noextattr "$project_root/Squid Manager/Squid.icon" "$staging_app/Contents/Resources/Squid.icon"
/bin/cp "$products_dir/squidd" "$resource_root/squidd"
/bin/cp "$products_dir/liblibsquid.dylib" "$resource_root/liblibsquid.dylib"
/bin/cp "$project_root/Packaging/com.squid.squidd.plist" "$resource_root/com.squid.squidd.plist"
/bin/cp "$project_root/Packaging/install-squidd.sh" "$resource_root/install-squidd.command"
/bin/cp "$project_root/Packaging/uninstall-squidd.sh" "$resource_root/uninstall-squidd.command"

/bin/chmod 755 "$resource_root/install-squidd.command" "$resource_root/uninstall-squidd.command"
/usr/bin/xattr -cr "$staging_app" 2>/dev/null || true

rm -rf "$output_app"
/usr/bin/ditto --norsrc --noextattr "$staging_app" "$output_app"
/usr/bin/xattr -cr "$output_app" 2>/dev/null || true

# Embedding resources changes the app bundle, so sign the final bundle.
# Set CODE_SIGN_IDENTITY to a distribution identity for releases; ad hoc signing
# keeps local builds runnable when no identity is supplied.
if [ -n "${CODE_SIGN_IDENTITY:-}" ]; then
    /usr/bin/codesign --force --deep --sign "$CODE_SIGN_IDENTITY" "$output_app"
else
    /usr/bin/codesign --force --deep --sign - "$output_app"
fi

echo "Created $output_app"
echo "To install the root daemon: open '$output_app/Contents/Resources/Squid/install-squidd.command'"
