#include "session.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSet>

namespace prepare {
QJsonObject Inputs::json() const {
    return {{"game", game},     {"firmware", firmware}, {"keys", keys},
            {"update", update}, {"jobs", jobs},         {"upscaler", upscaler}};
}
void Inputs::validate() const {
    require(jobs >= 1 && jobs <= 64, "Compiler jobs must be 1-64.");
    for (const auto &path : {game, firmware})
        require(!path.isEmpty() && QFileInfo(path).isAbsolute() && QFileInfo::exists(path),
                "Choose absolute paths to a game ISO/dump and firmware PUP/dev_flash.");
    require(QFileInfo(game).isDir() || QFileInfo(game).suffix().compare("iso", Qt::CaseInsensitive) == 0,
            "Game input must be an ISO or a dump directory.");
    require(QFileInfo(firmware).isDir() ||
                QFileInfo(firmware).suffix().compare("pup", Qt::CaseInsensitive) == 0,
            "Firmware input must be a PUP or dev_flash directory.");
    if (!keys.isEmpty())
        require(QFileInfo(keys).isAbsolute() && QFileInfo(keys).isFile(),
                "Select an absolute path to a local key file.");
    const auto store = KeyStore::fromFile(keys);
    if (QFileInfo(firmware).isFile()) {
        store.namedKey("pup_hmac", 64);
        require(!store.candidates(3, 0, 0).isEmpty(), "Private key file is missing a firmware package root.");
    }
    if (!update.isEmpty())
        require(QFileInfo(update).isAbsolute() && QFileInfo(update).isFile() &&
                    QStringList{"pkg", "bin", "elf"}.contains(QFileInfo(update).suffix().toLower()),
                "Update input must be a local PKG, EBOOT.BIN or EBOOT.elf.");
}
QJsonObject fingerprint(const QString &path, const Cancel &cancel) {
    const QFileInfo info(path);
    require(info.exists(), "Preparation input is missing: " + path);
    if (info.isDir())
        return {{"path", info.canonicalFilePath()},
                {"sfo", fingerprint(QDir(path).filePath("PS3_GAME/PARAM.SFO"), cancel)},
                {"eboot", fingerprint(QDir(path).filePath("PS3_GAME/USRDIR/EBOOT.BIN"), cancel)}};
    return {{"path", info.canonicalFilePath()}, {"size", info.size()}, {"sha256", fileHash(path, cancel)}};
}
QJsonObject treeFingerprint(const QString &path, const Cancel &cancel) {
    require(QFileInfo(path).isDir(), "Prepared directory is missing.");
    QJsonObject files;
    int count = 0;
    quint64 total = 0;
    QDirIterator iterator(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        cancelled(cancel);
        const auto file = iterator.next();
        rejectLinks(path, file);
        require(++count <= 200000, "Prepared directory exceeds its file-count limit.");
        if (iterator.fileInfo().isFile()) {
            total += quint64(iterator.fileInfo().size());
            require(total <= 64ull * 1024 * 1024 * 1024, "Prepared directory exceeds its size limit.");
            files[QDir(path).relativeFilePath(file)] = fingerprint(file, cancel);
        } else
            require(iterator.fileInfo().isDir(), "Special file in prepared output.");
    }
    return files;
}
QJsonObject importSession(const Inputs &inputs, const QString &requestedSession, bool resume, const Log &log,
                          const Cancel &cancel, const ImportHooks &hooks) {
    inputs.validate();
    cancelled(cancel);
    const auto session = QDir::cleanPath(QFileInfo(requestedSession).absoluteFilePath());
    rejectLinks(QFileInfo(session).absolutePath(), session);
    require(!QFileInfo::exists(session) || resume,
            "Preparation destination already exists. Resume explicitly or choose a new session.");
    require(QDir().mkpath(session), "Cannot create preparation session.");
    QLockFile lock(QDir(session).filePath(".prepare.lock"));
    lock.setStaleLockTime(0);
    require(lock.tryLock(0), "Preparation session is locked or not writable.");
    if (log)
        log("Checking input fingerprints.");
    QJsonObject identities{
        {"game", fingerprint(inputs.game, cancel)},
        {"keys", inputs.keys.isEmpty() ? QJsonValue() : QJsonValue(fingerprint(inputs.keys, cancel))},
        {"firmware",
         QFileInfo(inputs.firmware).isFile()
             ? fingerprint(inputs.firmware, cancel)
             : QJsonObject{{"path", QFileInfo(inputs.firmware).canonicalFilePath()},
                           {"atrac",
                            fingerprint(QDir(inputs.firmware).filePath("sys/external/flashATRAC.pic"),
                                        cancel)}}},
        {"update", inputs.update.isEmpty() ? QJsonValue() : QJsonValue(fingerprint(inputs.update, cancel))}};
    const auto ledger = QDir(session).filePath("state.json");
    rejectLinks(session, ledger);
    QJsonObject state;
    if (QFileInfo::exists(ledger)) {
        require(resume, "Resume this session explicitly.");
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(readFile(ledger, 64 * 1024 * 1024), &error);
        require(error.error == QJsonParseError::NoError && doc.isObject(),
                "Invalid native preparation ledger.");
        state = doc.object();
        require(state["format"] == "buzz-native-preparation" && state["version"].toInt() == 1 &&
                    state["identities"].toObject() == identities,
                "Preparation inputs changed or the session format is incompatible. Start a new session.");
        require(state["done"].isArray() && state["outputs"].isObject(),
                "Invalid native preparation stage records.");
    } else {
        require(!resume, "No native preparation ledger to resume.");
        state = {{"format", "buzz-native-preparation"},
                 {"version", 1},
                 {"inputs", inputs.json()},
                 {"identities", identities},
                 {"done", QJsonArray()},
                 {"outputs", QJsonObject()}};
    }
    auto save = [&] { atomicWrite(session, ledger, QJsonDocument(state).toJson(), cancel); };
    save();
    auto stage = [&](const QString &name, const std::function<void()> &work,
                     const std::function<QJsonObject()> &identity) {
        cancelled(cancel);
        auto done = state["done"].toArray();
        auto outputs = state["outputs"].toObject();
        if (done.contains(name)) {
            require(outputs[name].isObject() && outputs[name].toObject() == identity(),
                    "Prepared stage changed: " + name + ". Start a new session.");
            if (log)
                log("Verified completed stage: " + name);
            return;
        }
        if (log)
            log("Preparing: " + name);
        work();
        cancelled(cancel);
        outputs[name] = identity();
        done.append(name);
        state["outputs"] = outputs;
        state["done"] = done;
        save();
    };
    QString game = inputs.game;
    if (QFileInfo(game).isFile()) {
        DiscImage image(game);
        const auto plan = image.inspect(cancel);
        game = QDir(session).filePath("game");
        rejectLinks(session, game);
        stage(
            "extract ISO",
            [&] {
                if (QFileInfo::exists(game))
                    image.verifyExtraction(plan, game, cancel);
                else
                    image.extract(plan, game, log, cancel);
            },
            [&] { return treeFingerprint(game, cancel); });
    }
    const auto title = titleId(readFile(QDir(game).filePath("PS3_GAME/PARAM.SFO"), 1024 * 1024));
    require(QSet<QString>{"BCES00647", "BCES00644", "BCES00645", "BCUS98209"}.contains(title),
            "Unsupported disc title ID.");
    auto flash = inputs.firmware;
    const auto home = QDir(session).filePath("import");
    rejectLinks(session, home);
    if (QFileInfo(flash).isFile()) {
        flash = QDir(home).filePath("dev_flash");
        stage(
            "install firmware",
            [&] {
                extractFirmware(inputs.firmware, home, KeyStore::fromFile(inputs.keys), log, cancel,
                                hooks.firmware);
            },
            [&] { return treeFingerprint(flash, cancel); });
    }
    hooks.firmware(flash);
    auto executable = QDir(game).filePath("PS3_GAME/USRDIR/EBOOT.BIN");
    if (!inputs.update.isEmpty()) {
        executable = inputs.update;
        if (QFileInfo(inputs.update).suffix().compare("pkg", Qt::CaseInsensitive) == 0) {
            const auto update = QDir(home).filePath("dev_hdd0/game/" + title);
            rejectLinks(session, update);
            stage(
                "install game update", [&] { extractUpdate(inputs.update, update, title, log, cancel); },
                [&] { return treeFingerprint(update, cancel); });
            executable = QDir(update).filePath("USRDIR/EBOOT.BIN");
        }
    }
    const auto elf = QDir(session).filePath("executable/EBOOT.elf");
    rejectLinks(session, elf);
    stage(
        "prepare executable",
        [&] {
            const auto bytes = decryptElf(executable, KeyStore::fromFile(inputs.keys), cancel);
            const auto digest =
                QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            hooks.profile(title, digest);
            atomicWrite(session, elf, bytes, cancel);
        },
        [&] { return fingerprint(elf, cancel); });
    const auto digest = fileHash(elf, cancel), family = hooks.profile(title, digest);
    state["profile"] = QJsonObject{{"title_id", title}, {"elf_sha256", digest}, {"adapter_family", family}};
    state["paths"] = QJsonObject{{"game_root", QFileInfo(game).canonicalFilePath()},
                                 {"dev_flash", QFileInfo(flash).canonicalFilePath()},
                                 {"eboot_elf", QFileInfo(elf).canonicalFilePath()}};
    state["import_complete"] = true;
    save();
    if (log)
        log("Native import complete. Code generation and build are separate stages.");
    return state;
}
} // namespace prepare
