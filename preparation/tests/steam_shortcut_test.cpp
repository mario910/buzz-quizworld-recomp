#include "steam_shortcut.h"

#include <QtTest>
#include <stdexcept>

using steam::Node;

class SteamShortcutTest : public QObject {
    Q_OBJECT

    static QByteArray existingFile() {
        // One shortcut written by Steam, with a field type BuzzSetup does
        // not create (uint64) and a tag, to prove they survive untouched.
        Node entry{0, "0", {}, {}};
        entry.children.push_back(Node{2, "appid", QByteArray("\x01\x02\x03\x84", 4), {}});
        entry.children.push_back(Node{1, "AppName", "Other Game", {}});
        entry.children.push_back(Node{1, "Exe", "\"/opt/other/run\"", {}});
        entry.children.push_back(Node{7, "Custom64", QByteArray("\x01\x00\x00\x00\x00\x00\x00\x80", 8), {}});
        Node tags{0, "tags", {}, {}};
        tags.children.push_back(Node{1, "0", "Favorites", {}});
        entry.children.push_back(tags);
        Node shortcuts{0, "shortcuts", {}, {}};
        shortcuts.children.push_back(entry);
        Node root;
        root.children.push_back(shortcuts);
        return steam::serialize(root);
    }

    static steam::Shortcut buzz() {
        return {"Buzz! Quiz World", "/home/deck/BuzzQuizWorld/build-files/dist/BuzzQuizWorldRecomp",
                "/home/deck/BuzzQuizWorld/build-files/dist", "/home/deck/BuzzQuizWorld/buzz.png", {}};
    }

private slots:
    void roundTrip() {
        const auto data = existingFile();
        QCOMPARE(steam::serialize(steam::parse(data)), data);
    }

    void addsBesideExistingShortcut() {
        quint32 id = 0;
        const auto updated = steam::upsert(existingFile(), buzz(), &id);
        const auto root = steam::parse(updated);
        const auto* shortcuts = root.find("shortcuts");
        QVERIFY(shortcuts);
        QCOMPARE(shortcuts->children.size(), size_t(2));
        const auto& other = shortcuts->children[0];
        QCOMPARE(other.find("AppName")->value, QByteArray("Other Game"));
        QCOMPARE(other.find("Custom64")->value.size(), 8);
        QCOMPARE(other.find("tags")->children.size(), size_t(1));
        const auto& added = shortcuts->children[1];
        QCOMPARE(added.key, QByteArray("1"));
        QCOMPARE(added.find("AppName")->value, QByteArray("Buzz! Quiz World"));
        QCOMPARE(added.find("Exe")->value, QByteArray("\"/home/deck/BuzzQuizWorld/build-files/dist/BuzzQuizWorldRecomp\""));
        QCOMPARE(added.find("StartDir")->value, QByteArray("\"/home/deck/BuzzQuizWorld/build-files/dist\""));
        QVERIFY(id & 0x80000000u);
        const auto raw = added.find("appid")->value;
        QCOMPARE(quint32(quint8(raw[0])) | quint32(quint8(raw[1])) << 8 | quint32(quint8(raw[2])) << 16 |
                 quint32(quint8(raw[3])) << 24, id);
        QCOMPARE(id, steam::shortcutId(QString::fromUtf8(added.find("Exe")->value), "Buzz! Quiz World"));
    }

    void updatesInsteadOfDuplicating() {
        auto once = steam::upsert(existingFile(), buzz());
        auto moved = buzz();
        moved.exe = "/games/Buzz/dist/BuzzQuizWorldRecomp";
        moved.startDir = "/games/Buzz/dist";
        const auto twice = steam::upsert(once, moved);
        const auto* shortcuts = steam::parse(twice).find("shortcuts");
        QCOMPARE(shortcuts->children.size(), size_t(2));
        QCOMPARE(shortcuts->children[1].find("StartDir")->value, QByteArray("\"/games/Buzz/dist\""));
        QCOMPARE(steam::upsert(twice, moved), twice);
    }

    void createsMissingFile() {
        const auto* shortcuts = steam::parse(steam::upsert({}, buzz())).find("shortcuts");
        QVERIFY(shortcuts);
        QCOMPARE(shortcuts->children.size(), size_t(1));
        QCOMPARE(shortcuts->children[0].key, QByteArray("0"));
    }

    // BUZZ_STEAM_SHORTCUTS_ROUNDTRIP=<shortcuts.vdf>: read-only check that a real
    // Steam file parses and serializes back to the same bytes.
    void roundTripsRealFile() {
        const auto path = qEnvironmentVariable("BUZZ_STEAM_SHORTCUTS_ROUNDTRIP");
        if (path.isEmpty()) QSKIP("BUZZ_STEAM_SHORTCUTS_ROUNDTRIP not set");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto data = file.readAll();
        QCOMPARE(steam::serialize(steam::parse(data)), data);
    }

    void rejectsMalformedFiles() {
        auto data = existingFile();
        QVERIFY_EXCEPTION_THROWN(steam::parse(data.left(data.size() - 3)), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(steam::parse(QByteArray("\x09junk", 5)), std::runtime_error);
    }
};

QTEST_GUILESS_MAIN(SteamShortcutTest)
#include "steam_shortcut_test.moc"
