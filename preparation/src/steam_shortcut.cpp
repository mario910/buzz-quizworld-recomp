#include "steam_shortcut.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <stdexcept>
#include <zlib.h>

namespace steam {
namespace {

int numberSize(quint8 type) {
    switch (type) {
    case 2: case 3: return 4;
    case 7: return 8;
    default: return -1;
    }
}

class Reader {
public:
    explicit Reader(const QByteArray& data) : data_(data) {}
    std::vector<Node> map(bool top) {
        std::vector<Node> nodes;
        for (;;) {
            if (pos_ >= data_.size()) {
                if (top) return nodes;
                throw std::runtime_error("shortcuts.vdf ends inside a map");
            }
            const auto type = quint8(data_[pos_++]);
            if (type == 8) return nodes;
            Node node;
            node.type = type;
            node.key = cstring();
            if (type == 0) {
                node.children = map(false);
            } else if (type == 1) {
                node.value = cstring();
            } else if (const int size = numberSize(type); size > 0) {
                if (pos_ + size > data_.size()) throw std::runtime_error("shortcuts.vdf number is truncated");
                node.value = data_.mid(pos_, size);
                pos_ += size;
            } else {
                throw std::runtime_error("shortcuts.vdf has an unsupported value type");
            }
            nodes.push_back(std::move(node));
        }
    }
    bool atEnd() const { return pos_ >= data_.size(); }

private:
    QByteArray cstring() {
        const auto end = data_.indexOf('\0', pos_);
        if (end < 0) throw std::runtime_error("shortcuts.vdf string is not terminated");
        auto text = data_.mid(pos_, end - pos_);
        pos_ = end + 1;
        return text;
    }
    const QByteArray& data_;
    qsizetype pos_ = 0;
};

void write(QByteArray& out, const Node& node) {
    out.append(char(node.type));
    out.append(node.key);
    out.append('\0');
    if (node.type == 0) {
        for (const auto& child : node.children) write(out, child);
        out.append(char(8));
    } else if (node.type == 1) {
        out.append(node.value);
        out.append('\0');
    } else {
        out.append(node.value);
    }
}

Node text(const char* key, const QString& value) {
    return Node{1, key, value.toUtf8(), {}};
}

Node integer(const char* key, quint32 value) {
    QByteArray raw(4, '\0');
    for (int i = 0; i < 4; ++i) raw[i] = char((value >> (8 * i)) & 0xff);
    return Node{2, key, raw, {}};
}

void set(Node& entry, Node value) {
    if (auto* existing = entry.find(value.key)) {
        // Keep the field where Steam put it; a changed type is replaced.
        *existing = std::move(value);
    } else {
        entry.children.push_back(std::move(value));
    }
}

QString quoted(const QString& path) { return '"' + path + '"'; }

} // namespace

Node* Node::find(const QByteArray& name) {
    for (auto& child : children)
        if (child.key.compare(name, Qt::CaseInsensitive) == 0) return &child;
    return nullptr;
}
const Node* Node::find(const QByteArray& name) const {
    return const_cast<Node*>(this)->find(name);
}

Node parse(const QByteArray& data) {
    Node root;
    if (data.isEmpty()) {
        root.children.push_back(Node{0, "shortcuts", {}, {}});
        return root;
    }
    Reader reader(data);
    root.children = reader.map(true);
    if (!reader.atEnd()) throw std::runtime_error("shortcuts.vdf has trailing data");
    return root;
}

QByteArray serialize(const Node& root) {
    QByteArray out;
    for (const auto& child : root.children) write(out, child);
    out.append(char(8));
    return out;
}

quint32 shortcutId(const QString& quotedExe, const QString& name) {
    const auto bytes = (quotedExe + name).toUtf8();
    return quint32(crc32(0L, reinterpret_cast<const Bytef*>(bytes.constData()), uInt(bytes.size()))) | 0x80000000u;
}

QByteArray upsert(const QByteArray& existing, const Shortcut& shortcut, quint32* id) {
    auto root = parse(existing);
    auto* shortcuts = root.find("shortcuts");
    if (!shortcuts) {
        root.children.push_back(Node{0, "shortcuts", {}, {}});
        shortcuts = &root.children.back();
    }
    if (shortcuts->type != 0) throw std::runtime_error("shortcuts.vdf has no shortcut list");
    const auto exe = quoted(shortcut.exe);
    Node* entry = nullptr;
    for (auto& candidate : shortcuts->children) {
        if (candidate.type != 0) continue;
        const auto* name = candidate.find("AppName");
        const auto* path = candidate.find("Exe");
        if ((name && QString::fromUtf8(name->value).compare(shortcut.name, Qt::CaseInsensitive) == 0) ||
            (path && QString::fromUtf8(path->value) == exe)) {
            entry = &candidate;
            break;
        }
    }
    if (!entry) {
        // Entries are keyed "0", "1", ...: use the next free index.
        int next = 0;
        for (const auto& candidate : shortcuts->children) next = std::max(next, candidate.key.toInt() + 1);
        shortcuts->children.push_back(Node{0, QByteArray::number(next), {}, {}});
        entry = &shortcuts->children.back();
        set(*entry, integer("IsHidden", 0));
        set(*entry, integer("AllowDesktopConfig", 1));
        set(*entry, integer("AllowOverlay", 1));
        set(*entry, integer("OpenVR", 0));
        set(*entry, integer("Devkit", 0));
        set(*entry, text("DevkitGameID", {}));
        set(*entry, integer("DevkitOverrideAppID", 0));
        set(*entry, integer("LastPlayTime", 0));
        set(*entry, text("FlatpakAppID", {}));
        set(*entry, Node{0, "tags", {}, {}});
    }
    const auto appId = shortcutId(exe, shortcut.name);
    set(*entry, integer("appid", appId));
    set(*entry, text("AppName", shortcut.name));
    set(*entry, text("Exe", exe));
    set(*entry, text("StartDir", quoted(shortcut.startDir)));
    set(*entry, text("icon", shortcut.icon));
    set(*entry, text("ShortcutPath", {}));
    set(*entry, text("LaunchOptions", shortcut.launchOptions));
    if (id) *id = appId;
    return serialize(root);
}

QStringList accountConfigDirs() {
    const auto home = QDir::homePath();
    QStringList roots{home + "/.local/share/Steam", home + "/.steam/steam",
                      home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"};
    QStringList result, seen;
    for (const auto& root : roots) {
        const QDir userdata(root + "/userdata");
        if (!userdata.exists()) continue;
        const auto canonical = QFileInfo(userdata.absolutePath()).canonicalFilePath();
        if (seen.contains(canonical)) continue;
        seen << canonical;
        for (const auto& account : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool numeric = false;
            if (account.toULongLong(&numeric) == 0 || !numeric) continue; // "0" is not an account
            const auto config = userdata.filePath(account + "/config");
            if (QFileInfo(config).isDir()) result << config;
        }
    }
    return result;
}

bool clientRunning() {
#ifdef Q_OS_LINUX
    const QDir proc("/proc");
    for (const auto& pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        pid.toUInt(&numeric);
        if (!numeric) continue;
        QFile comm(proc.filePath(pid + "/comm"));
        if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "steam") return true;
    }
#endif
    return false;
}

quint32 install(const QString& configDir, const Shortcut& shortcut) {
    const auto path = QDir(configDir).filePath("shortcuts.vdf");
    QByteArray existing;
    if (QFileInfo(path).isFile()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read Steam's shortcuts.vdf");
        existing = file.readAll();
    }
    quint32 id = 0;
    const auto updated = upsert(existing, shortcut, &id);
    if (!existing.isEmpty()) {
        QSaveFile backup(path + ".buzz-backup");
        if (!backup.open(QIODevice::WriteOnly) || backup.write(existing) != existing.size() || !backup.commit())
            throw std::runtime_error("Cannot back up Steam's shortcuts.vdf");
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(updated) != updated.size() || !out.commit())
        throw std::runtime_error("Cannot write Steam's shortcuts.vdf");
    return id;
}

} // namespace steam
