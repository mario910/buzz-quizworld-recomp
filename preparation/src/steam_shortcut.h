// Steam non-Steam-game shortcuts (userdata/<account>/config/shortcuts.vdf).
// Lets BuzzSetup put the prepared game into the Steam library, so it starts
// from Steam Deck Game Mode like any other game.
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>

namespace steam {

// One binary-VDF node. Types: 0 map, 1 string, 2 int32, 3 float32, 7 uint64.
struct Node {
    quint8 type = 0;
    QByteArray key;
    QByteArray value;           // string bytes, or the raw little-endian number
    std::vector<Node> children; // maps only
    Node* find(const QByteArray& name);
    const Node* find(const QByteArray& name) const;
};

Node parse(const QByteArray& data);     // throws std::runtime_error on malformed input
QByteArray serialize(const Node& root);

struct Shortcut {
    QString name, exe, startDir, icon, launchOptions;
};

// Steam's id for a shortcut: CRC-32 of the quoted exe and the name, high bit set.
quint32 shortcutId(const QString& quotedExe, const QString& name);

// Adds the shortcut, or updates the entry with the same name (case-insensitive)
// or executable. Returns the new file contents and the shortcut's id.
QByteArray upsert(const QByteArray& existing, const Shortcut& shortcut, quint32* id = nullptr);

// userdata/<account>/config directories of every local Steam account
// (native and Flatpak Steam).
QStringList accountConfigDirs();
// True while a Steam client process is running (it rewrites shortcuts.vdf on exit).
bool clientRunning();
// Writes the shortcut into one account's shortcuts.vdf, atomically, keeping
// the previous file as shortcuts.vdf.buzz-backup.
quint32 install(const QString& configDir, const Shortcut& shortcut);

} // namespace steam
