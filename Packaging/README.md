# Squid distribution

Select the shared **Squid** scheme and choose **Product → Build**. The scheme builds all three targets and automatically assembles the self-contained application bundle in `Products/Squid.app`.

From Terminal, the equivalent one-command build is:

    xcodebuild -project Squid.xcodeproj -scheme Squid -configuration Release -derivedDataPath .build/DerivedData build

The resulting `Products/Squid.app` contains:

- The Squid Manager application
- `Contents/Resources/Squid/squidd`
- `Contents/Resources/Squid/liblibsquid.dylib`
- `Contents/Resources/Squid/com.squid.squidd.plist`
- `Contents/Resources/Squid/install-squidd.command`
- `Contents/Resources/Squid/uninstall-squidd.command`

After moving the app wherever you want, click **Install Squidd** in Squid Manager. The app opens the bundled installer command, which asks for administrator authorization, copies the embedded daemon and library into `/Library/Application Support/Squid`, and registers `com.squid.squidd` in the system launchd domain. The daemon runs as root and restarts automatically if it exits. The command is also available at `Contents/Resources/Squid/install-squidd.command`.

An ordinary macOS app cannot silently install a root daemon; explicit administrator authorization is required. The application bundle is otherwise self-contained and can be moved as one unit.

To remove the installed daemon and support files, run the bundled `uninstall-squidd.command`.
