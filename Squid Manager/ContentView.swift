import AppKit
import SwiftUI

struct TweakEntry: Identifiable, Codable, Equatable {
    var identifier: String
    var targetBundleID: String
    var osVersion: String
    var libraryPath: String
    var enabled: Bool
    var fileName: String = ""

    var id: String { fileName.isEmpty ? identifier : fileName }

    private enum CodingKeys: String, CodingKey {
        case identifier = "Identifier"
        case targetBundleID = "TargetBundleID"
        case osVersion = "OSVersion"
        case libraryPath = "LibraryPath"
        case enabled = "Enabled"
    }
}

@MainActor
final class TweakStore {
    private let fileManager = FileManager.default
    private let encoder: PropertyListEncoder = {
        let encoder = PropertyListEncoder()
        encoder.outputFormat = .xml
        return encoder
    }()
    private let decoder = PropertyListDecoder()

    var directoryURL: URL {
        fileManager.homeDirectoryForCurrentUser
            .appendingPathComponent(".squid", isDirectory: true)
    }

    func load() throws -> [TweakEntry] {
        try fileManager.createDirectory(
            at: directoryURL,
            withIntermediateDirectories: true
        )

        return try fileManager.contentsOfDirectory(
            at: directoryURL,
            includingPropertiesForKeys: nil
        )
        .filter { $0.pathExtension.lowercased() == "plist" }
        .compactMap { url in
            guard let data = try? Data(contentsOf: url),
                  var entry = try? decoder.decode(TweakEntry.self, from: data)
            else {
                return nil
            }
            entry.fileName = url.lastPathComponent
            return entry
        }
        .sorted { $0.identifier.localizedStandardCompare($1.identifier) == .orderedAscending }
    }

    func save(_ entry: TweakEntry) throws {
        try fileManager.createDirectory(
            at: directoryURL,
            withIntermediateDirectories: true
        )

        let fileName = entry.fileName.isEmpty
            ? Self.safeFileName(for: entry.identifier)
            : entry.fileName
        let destination = directoryURL.appendingPathComponent(fileName)
        let data = try encoder.encode(entry)
        try data.write(to: destination, options: .atomic)
    }

    func remove(_ entry: TweakEntry) throws {
        let fileName = entry.fileName.isEmpty
            ? Self.safeFileName(for: entry.identifier)
            : entry.fileName
        try fileManager.removeItem(
            at: directoryURL.appendingPathComponent(fileName)
        )
    }

    private static func safeFileName(for identifier: String) -> String {
        let name = identifier.isEmpty ? "tweak" : identifier
        let safeName = name.map { character in
            character.isLetter || character.isNumber || character == "-" || character == "_"
                ? character
                : "-"
        }
        return String(safeName) + ".plist"
    }
}

@MainActor
struct ContentView: View {
    @State private var entries: [TweakEntry] = []
    @State private var editorEntry: TweakEntry?
    @State private var errorMessage: String?
    @State private var shouldOfferInstallation = false
    @State private var installationMessage: String?
    private let store = TweakStore()

    var body: some View {
        VStack(spacing: 0) {
            if entries.isEmpty {
                VStack(spacing: 16) {
                    ContentUnavailableView(
                        "No Tweaks Configured",
                        systemImage: "shippingbox",
                        description: Text("Add a plist-backed tweak to ~/.squid.")
                    )

                    Button {
                        shouldOfferInstallation = true
                    } label: {
                        Label("Install Squidd", systemImage: "lock.shield")
                    }
                    .buttonStyle(.borderedProminent)
                }
            } else {
                List {
                    ForEach(entries) { entry in
                        Button {
                            editorEntry = entry
                        } label: {
                            HStack {
                                Image(systemName: entry.enabled ? "checkmark.circle.fill" : "pause.circle")
                                    .foregroundStyle(entry.enabled ? .green : .secondary)
                                VStack(alignment: .leading) {
                                    Text(entry.identifier)
                                        .font(.headline)
                                    Text(entry.targetBundleID)
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                Text(entry.osVersion.isEmpty ? "All macOS versions" : entry.osVersion)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        .buttonStyle(.plain)
                    }
                    .onDelete(perform: delete)
                }
            }
        }
        .frame(minWidth: 650, minHeight: 360)
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    editorEntry = TweakEntry(
                        identifier: "",
                        targetBundleID: "",
                        osVersion: "",
                        libraryPath: "",
                        enabled: true
                    )
                } label: {
                    Label("Add Tweak", systemImage: "plus")
                }
            }
            ToolbarItem {
                Button {
                    shouldOfferInstallation = true
                } label: {
                    Label("Install Squidd", systemImage: "lock.shield")
                }
            }
            ToolbarItem {
                Button {
                    reload()
                } label: {
                    Label("Refresh", systemImage: "arrow.clockwise")
                }
            }
        }
        .task {
            reload()
            offerInstallationIfNeeded()
        }
        .sheet(item: $editorEntry) { entry in
            TweakEditor(entry: entry) { updatedEntry in
                do {
                    try store.save(updatedEntry)
                    reload()
                } catch {
                    errorMessage = error.localizedDescription
                }
            }
        }
        .alert("Install Squidd?", isPresented: $shouldOfferInstallation) {
            Button("Install", action: authorizeAndInstall)
            Button("Not Now", role: .cancel) {}
        } message: {
            Text("Squid needs administrator authorization to install its root LaunchDaemon. macOS may offer Touch ID or your administrator password.")
        }
        .alert("Squidd Installation", isPresented: Binding(
            get: { installationMessage != nil },
            set: { if !$0 { installationMessage = nil } }
        )) {
            Button("OK", role: .cancel) {
                installationMessage = nil
            }
        } message: {
            Text(installationMessage ?? "")
        }
        .alert(
            "Could Not Update Tweaks",
            isPresented: Binding(
                get: { errorMessage != nil },
                set: { if !$0 { errorMessage = nil } }
            )
        ) {
            Button("OK") {
                errorMessage = nil
            }
        } message: {
            Text(errorMessage ?? "")
        }
    }

    private func offerInstallationIfNeeded() {
        if !FileManager.default.fileExists(
            atPath: "/Library/LaunchDaemons/com.squid.squidd.plist"
        ) {
            shouldOfferInstallation = true
        }
    }

    private func authorizeAndInstall() {
        guard let installerURL = Bundle.main.url(
            forResource: "install-squidd",
            withExtension: "command",
            subdirectory: "Squid"
        ) else {
            installationMessage = "The bundled Squidd installer is missing."
            return
        }

        let status = SquidAuthorizeAndInstall(installerURL.path)
        if status == 0 {
            installationMessage = "Squidd was installed successfully."
        } else {
            installationMessage = "Administrator authorization was cancelled, denied, or the installer failed (error \(status))."
        }
    }

    private func reload() {
        do {
            entries = try store.load()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func delete(at offsets: IndexSet) {
        for index in offsets {
            do {
                try store.remove(entries[index])
            } catch {
                errorMessage = error.localizedDescription
            }
        }
        reload()
    }
}

@MainActor
struct TweakEditor: View {
    @Environment(\.dismiss) private var dismiss
    @State private var entry: TweakEntry
    let onSave: (TweakEntry) -> Void

    init(entry: TweakEntry, onSave: @escaping (TweakEntry) -> Void) {
        self.entry = entry
        self.onSave = onSave
    }

    var body: some View {
        Form {
            TextField("Identifier", text: $entry.identifier)
            TextField("Target bundle ID", text: $entry.targetBundleID)
            TextField("OS version (blank for all)", text: $entry.osVersion)
            TextField("Dylib path", text: $entry.libraryPath)
            Toggle("Enabled", isOn: $entry.enabled)
        }
        .padding()
        .frame(width: 480)
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Cancel") {
                    dismiss()
                }
            }
            ToolbarItem(placement: .confirmationAction) {
                Button("Save") {
                    onSave(entry)
                    dismiss()
                }
                .disabled(
                    entry.identifier.isEmpty ||
                    entry.targetBundleID.isEmpty ||
                    entry.libraryPath.isEmpty
                )
            }
        }
    }
}

#Preview {
    ContentView()
}
