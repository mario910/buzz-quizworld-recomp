#include "omnivoice.h"
#include "languages.h"
#include "media_tools.h"
#include "omnivoice_runtime.h"
#include "pack_builder.h"
#include "pack_format.h"
#include "pack_media.h"
#include "ps2_media.h"
#include "ps3_source.h"
#include "psp_media.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>

static void initializeOmniVoiceWorker() { Q_INIT_RESOURCE(studio_tts); }

namespace studio {
namespace {
QString T(const char *text) { return QCoreApplication::translate("OmniVoice", text); }
QString profileKey(const QString &language) { return "omnivoice/profiles/" + languageCode(language); }
bool writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile output(path);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}

QString managedRoot() {
    const auto manifest = managedOmniVoiceManifest();
    return manifest.isEmpty() ? QString() : QFileInfo(manifest).absolutePath();
}

QString environmentPython(const QDir &environment) {
#ifdef Q_OS_WIN
    return environment.filePath("Scripts/python.exe");
#else
    return environment.filePath("bin/python");
#endif
}

QString copyWorker(const QTemporaryDir &temp) {
    initializeOmniVoiceWorker();
    const auto path = temp.filePath("studio_voice_worker.py");
    QFile worker(":/tts/omnivoice.py");
    return temp.isValid() && worker.open(QIODevice::ReadOnly) && writeFile(path, worker.readAll()) ? path : QString();
}

struct SetupCommand {
    bool ok = false;
    QByteArray output;
};

SetupCommand runSetupCommand(const QString &program, const QStringList &arguments, const QDir &root,
                             const QProcessEnvironment &environment, const std::atomic_bool &cancel,
                             int timeoutMs = -1) {
    if (cancel.load())
        return {};
    QProcess process;
    process.setWorkingDirectory(root.path());
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(5000))
        return {false, process.errorString().toUtf8()};
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray output;
    while (process.state() != QProcess::NotRunning && !cancel.load() &&
           (timeoutMs < 0 || !elapsed.hasExpired(timeoutMs))) {
        process.waitForReadyRead(200);
        output = (output + process.readAll()).right(8000);
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(5000);
        return {false, (output + process.readAll() + "\nProcess interrupted or timed out").right(8000)};
    }
    return {!cancel.load() && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            (output + process.readAll()).right(8000)};
}

SetupCommand downloadUv(const QString &destination, const std::atomic_bool &cancel) {
    // Pin the official PyPI wheels and verify the archive before extracting the executable.
#ifdef Q_OS_WIN
    const QString url = "https://files.pythonhosted.org/packages/cf/e4/d0704c116ada58e8450890e2defaaf2f07e8d264cf94669210e0823fbcb2/"
                        "uv-0.13.10-py3-none-win_amd64.whl";
    const QByteArray hash = "d87b9cf8165f2b3cc596e856b5bf794da9cfad6d1b110ca9d87eed42a15b3684";
    const QString member = "uv-0.13.10.data/scripts/uv.exe";
#else
    const QString url = "https://files.pythonhosted.org/packages/8b/99/fad906311e48fce22e4db71ee4c3df29bdb17b29eb36878f52b8dcd79237/"
                        "uv-0.13.10-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl";
    const QByteArray hash = "7cc9c41b43d583e34c781fd3446405b06e9c5fa2db468ce2a534d353db6429e5";
    const QString member = "uv-0.13.10.data/scripts/uv";
#endif
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(30000);
    auto *reply = network.get(request);
    QEventLoop loop;
    QTimer timer;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (cancel.load() || reply->bytesAvailable() > 32 * 1024 * 1024)
            reply->abort();
    });
    timer.start(100);
    if (!reply->isFinished())
        loop.exec();
    if (reply->error() != QNetworkReply::NoError)
        return {false, reply->errorString().toUtf8()};
    const auto wheel = reply->readAll();
    if (QCryptographicHash::hash(wheel, QCryptographicHash::Sha256).toHex() != hash)
        return {false, "uv wheel SHA-256 mismatch"};
    try {
        pack::Archive archive(wheel);
        if (!writeFile(destination, archive.readDeflated(member)))
            return {false, "Could not write uv"};
#ifndef Q_OS_WIN
        if (!QFile::setPermissions(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner))
            return {false, "Could not make uv executable"};
#endif
    } catch (const std::exception &error) {
        return {false, error.what()};
    }
    return {true, {}};
}
} // namespace

bool VoiceProfile::ready() const { return QFileInfo(recording).isFile() && !transcript.trimmed().isEmpty(); }
QString managedOmniVoicePython() {
    const auto root = managedRoot();
    if (root.isEmpty())
        return {};
    const auto installed = managedOmniVoiceRuntime();
    const auto python = installed.value("python").toString();
    if (QFileInfo(python).isExecutable() && QFileInfo(installed.value("ffmpeg").toString()).isExecutable())
        return python;
    const QDir legacy(QDir(root).filePath("environment-v1"));
    const auto legacyPython = environmentPython(legacy);
    if (QFileInfo(legacy.filePath("ffmpeg-path")).isFile() && QFileInfo(legacyPython).isExecutable() &&
        !managedMediaFfmpeg().isEmpty())
        return legacyPython;
    return {};
}

QString managedOmniVoiceBackend() {
    return managedOmniVoiceRuntime().value("backend").toString();
}

QString omniVoicePython() {
    const auto custom = QSettings().value("omnivoice/python").toString();
    return QFileInfo(custom).isExecutable() ? custom : managedOmniVoicePython();
}

bool canInstallOmniVoice() {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return QSysInfo::buildCpuArchitecture() == "x86_64";
#else
    return false;
#endif
}

SpeechResult omnivoice::installRuntime(const std::atomic_bool &cancel, const std::function<void(int)> &progress,
                                       std::optional<Backend> preferred) {
    const auto path = managedRoot();
    const QDir root(path);
    if (!canInstallOmniVoice() || path.isEmpty() || !QDir().mkpath(path))
        return {{}, T("Nie można przygotować lokalnego zadania OmniVoice.")};
    QLockFile lock(root.filePath("install.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock())
        return {{}, T("Instalacja OmniVoice już trwa w innym oknie.")};

    const QTemporaryDir temp;
    const auto script = copyWorker(temp);
    if (script.isEmpty())
        return {{}, T("Nie można przygotować lokalnego zadania OmniVoice.")};
    progress(0);

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("UV_PYTHON_INSTALL_DIR", root.filePath("python"));
    environment.remove("IMAGEIO_FFMPEG_EXE");
    environment.insert("HF_HUB_DISABLE_TELEMETRY", "1");
    // AppImage bundle libraries belong to Qt. The standalone Python wheels load their own.
    if (environment.contains("APPIMAGE"))
        environment.remove("LD_LIBRARY_PATH");

    const auto selectBackend = [&] {
        const auto smi = QStandardPaths::findExecutable("nvidia-smi");
        const auto driver = smi.isEmpty() ? SetupCommand{} : runSetupCommand(
            smi, {"--query-gpu=driver_version", "--format=csv,noheader"}, root, environment, cancel, 5000);
#ifdef Q_OS_WIN
        constexpr int cudaDriver = 572;
#else
        constexpr int cudaDriver = 570;
#endif
        if (driver.ok && driver.output.trimmed().split('.').first().toInt() >= cudaDriver)
            return Backend::Cuda;
        // The PyTorch probe checks the driver even when DXGI or KFD lists the card.
        return hasRocmGpu() ? Backend::Rocm : Backend::Cpu;
    };
    auto backend = preferred ? *preferred : selectBackend();
    const auto probe = [&](const QString &python, Backend requested) {
        return runSetupCommand(python, {"-I", "-X", "utf8", "-u", script, "--check-device", backendId(requested)},
                               root, environment, cancel, 60000);
    };
    const auto saveRuntime = [&](const QString &python, const QString &ffmpeg, Backend selected) -> SpeechResult {
        const QJsonObject runtime{{"python", python}, {"ffmpeg", ffmpeg}, {"backend", backendId(selected)}};
        if (!cancel.load() && writeFile(managedOmniVoiceManifest(), QJsonDocument(runtime).toJson()))
            return {python, {}};
        return {{}, T("Nie można przygotować lokalnego zadania OmniVoice.")};
    };
    const auto existingPython = managedOmniVoicePython();
    if (!existingPython.isEmpty() && probe(existingPython, backend).ok)
        return saveRuntime(existingPython, managedMediaFfmpeg(), backend);

    const auto uv = temp.filePath("uv.exe");
    const auto bootstrap = downloadUv(uv, cancel);
    if (!bootstrap.ok)
        return {{}, QString::fromUtf8(bootstrap.output)};

    const auto runUv = [&](QStringList arguments) {
        arguments.prepend("--no-progress");
        arguments.prepend("--no-config");
        return runSetupCommand(uv, arguments, root, environment, cancel);
    };

    progress(1);
    QString python;
    const auto prepareBackend = [&](Backend requested) {
        const QDir directory(root.filePath("environment-" + backendId(requested) + "-v1"));
        python = environmentPython(directory);
        if (!QFileInfo(python).isExecutable()) {
            const auto venv = runUv({"venv", "--managed-python", "--python", "3.12", "--allow-existing",
                                     directory.path()});
            if (!venv.ok)
                return venv;
        }
        QStringList arguments{"pip", "install", "--python", python};
        arguments += torchPackages(requested);
        arguments += QStringList{"transformers==5.24.1", "omnivoice==0.2.1", "imageio-ffmpeg==0.8.2"};
        const auto packages = runUv(arguments);
        return packages.ok ? probe(python, requested) : packages;
    };
    auto ready = prepareBackend(backend);
    if (!ready.ok && backend != Backend::Cpu && !cancel.load()) {
        backend = Backend::Cpu;
        if (!existingPython.isEmpty() && probe(existingPython, backend).ok)
            return saveRuntime(existingPython, managedMediaFfmpeg(), backend);
        ready = prepareBackend(backend);
    }
    if (!ready.ok)
        return {{}, QString::fromUtf8(ready.output)};

    progress(2);
    const auto check = runSetupCommand(python, {"-I", "-X", "utf8", "-u", script, "--prepare"}, root, environment, cancel);
    if (!check.ok)
        return {{}, QString::fromUtf8(check.output)};
    for (const auto &line : check.output.split('\n')) {
        const QByteArray prefix("buzz-omnivoice-ffmpeg:");
        if (line.startsWith(prefix)) {
            const auto ffmpeg = QString::fromUtf8(line.mid(prefix.size()).trimmed());
            if (QFileInfo(ffmpeg).isExecutable())
                return saveRuntime(python, ffmpeg, backend);
        }
    }
    return {{}, T("Nie można przygotować lokalnego zadania OmniVoice.")};
}

SpeechResult installOmniVoice(const std::atomic_bool &cancel, const std::function<void(int)> &progress) {
    return omnivoice::installRuntime(cancel, progress);
}

void setOmniVoicePython(const QString &path) { QSettings().setValue("omnivoice/python", path); }
VoiceProfile omniVoiceProfile(const QString &language) {
    const auto profile = QSettings().value(profileKey(language)).toMap();
    return {profile.value("recording").toString(), profile.value("transcript").toString()};
}
SpeechResult saveOmniVoiceProfile(const QString &language, const VoiceProfile &profile) {
    if (languageCode(language).isEmpty() || !profile.ready())
        return {{}, T("Dodaj nagranie referencyjne i wpisz jego transkrypcję.")};
    QFile source(profile.recording);
    if (!source.open(QIODevice::ReadOnly) || source.size() <= 0 || source.size() > 64 * 1024 * 1024)
        return {{}, T("Nie można odczytać nagrania referencyjnego.")};
    const auto bytes = source.readAll();
    if (bytes.size() != source.size())
        return {{}, T("Nie można odczytać nagrania referencyjnego.")};
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto directory = QDir(root).filePath("voice-profiles");
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    const auto suffix = QFileInfo(source).suffix().toLower();
    const auto path = QDir(directory).filePath(QString::fromLatin1(hash) + "." + suffix);
    if (root.isEmpty() || !QDir().mkpath(directory) || (!QFileInfo(path).isFile() && !writeFile(path, bytes)))
        return {{}, T("Nie można zapisać lokalnego profilu lektora.")};
    QSettings settings;
    settings.setValue(profileKey(language), QVariantMap{{"recording", path}, {"transcript", profile.transcript.trimmed()}});
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return {{}, T("Nie można zapisać lokalnego profilu lektora.")};
    return {path, {}};
}

SpeechResult omniVoiceReference(const Question &question) {
    if (QFileInfo(question.speech).isFile())
        return {question.speech, {}};
    try {
        const auto path = hasPspNarration(question) ? recoverPspNarration(question) : recoverPs2Narration(question);
        if (QFileInfo(path).isFile())
            return {path, {}};
    } catch (const std::exception &error) {
        return {{}, QString::fromUtf8(error.what())};
    }
    return {{}, T("Nie znaleziono lektora wybranego pytania.")};
}

VoiceProfile omniVoiceInstalledProfile(const QString &language) {
    const auto targetLanguage = languageCode(language);
    if (targetLanguage.isEmpty())
        return {};
    // Use the particular game selected by BuzzSetup even when another runtime is remembered.
    const auto game = gameDirectory();
    const auto runtime = game.isEmpty() ? suggestBuildSettings(targetLanguage).value("runtimeRoot").toString()
                                        : QDir(game).filePath("runtime");
    if (runtime.isEmpty())
        return {};
    const auto disc = QDir(runtime).filePath("disc/PS3_GAME/PARAM.SFO");
    if (!QFileInfo(disc).isFile())
        return {};
    const auto result = ps3::importPs3(disc, targetLanguage, false, true);
    if (!result.ok() || result.document.questions.isEmpty())
        return {};
    const auto &question = result.document.questions.first();
    return {question.speech, question.text};
}

VoiceBatch generateOmniVoice(const QString &python, const QString &ffmpeg, const QString &language,
                             const VoiceProfile &profile, const QVector<VoiceJob> &jobs,
                             const std::atomic_bool *cancel, const std::function<void(int, int)> &progress) {
    VoiceBatch result;
    if (jobs.isEmpty())
        return result;
    if (!QFileInfo(python).isExecutable()) {
        result.errors << T("Wybierz interpreter Python z zainstalowanym OmniVoice.");
        return result;
    }
    if (!profile.ready()) {
        result.errors << T("Dodaj nagranie referencyjne i wpisz jego transkrypcję.");
        return result;
    }

    const QTemporaryDir temp;
    const auto scriptPath = copyWorker(temp);
    if (scriptPath.isEmpty()) {
        result.errors << T("Nie można przygotować lokalnego zadania OmniVoice.");
        return result;
    }
    auto reference = profile.recording;
    try {
        const auto extension = QFileInfo(reference).suffix().toLower();
        if (extension == "rsf" || extension == "wem") {
            const auto wrapped = temp.filePath(extension == "wem" ? "source.at9" : "source.wav");
            if (!writeFile(wrapped, pack::previewAudioWave(reference)))
                throw std::runtime_error(T("Nie można przygotować lokalnego zadania OmniVoice.").toStdString());
            reference = wrapped;
        }
        if (extension != "wav" || !ffmpeg.isEmpty()) {
            if (ffmpeg.isEmpty())
                throw std::runtime_error(missingMediaFfmpeg().toStdString());
            const auto wav = temp.filePath("reference.wav");
            pack::runMedia(ffmpeg, {"-v", "error", "-nostdin", "-protocol_whitelist", "file,pipe", "-i", reference,
                                    "-ac", "1", "-ar", "24000", "-c:a", "pcm_s16le", wav}, cancel, wav);
            reference = wav;
        }
    } catch (const std::exception &error) {
        result.cancelled = cancel && cancel->load();
        if (!result.cancelled)
            result.errors << QString::fromUtf8(error.what());
        return result;
    }

    QJsonArray texts;
    for (const auto &job : jobs)
        texts.append(job.text);
    const auto manifest = temp.filePath("request.json");
    const QJsonObject request{{"language", languageCode(language).section('-', 0, 0)},
                              {"recording", reference}, {"transcript", profile.transcript}, {"texts", texts}};
    if (!writeFile(manifest, QJsonDocument(request).toJson(QJsonDocument::Compact))) {
        result.errors << T("Nie można przygotować lokalnego zadania OmniVoice.");
        return result;
    }
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("HF_HUB_DISABLE_TELEMETRY", "1");
    if (environment.contains("APPIMAGE"))
        environment.remove("LD_LIBRARY_PATH");
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(temp.path());
    process.start(python, {"-I", "-u", scriptPath, manifest});
    if (!process.waitForStarted(5000)) {
        result.errors << T("Nie można uruchomić OmniVoice.") + "\n" + process.errorString();
        return result;
    }
    QByteArray pending, diagnostics;
    int completed = 0;
    const auto readProgress = [&] {
        pending += process.readAllStandardOutput();
        diagnostics = (diagnostics + process.readAllStandardError()).right(3000);
        for (qsizetype end; (end = pending.indexOf('\n')) >= 0;) {
            const auto line = pending.left(end).trimmed();
            pending.remove(0, end + 1);
            if (line == "buzz-omnivoice:" + QByteArray::number(completed)) {
                ++completed;
                if (progress)
                    progress(completed, int(jobs.size()));
            }
        }
    };
    while (process.state() != QProcess::NotRunning) {
        if (cancel && cancel->load()) {
            result.cancelled = true;
            process.kill();
            process.waitForFinished(5000);
            break;
        }
        process.waitForReadyRead(100);
        readProgress();
    }
    readProgress();
    for (qsizetype index = 0; index < jobs.size(); ++index) {
        QFile wave(temp.filePath(QString::number(index) + ".wav"));
        if (!wave.open(QIODevice::ReadOnly) || wave.size() <= 44 || wave.size() > 64 * 1024 * 1024)
            continue;
        const auto bytes = wave.readAll();
        if (bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WAVE" && writeFile(jobs[index].destination, bytes))
            result.written << index;
    }
    if (!result.cancelled && result.written.size() != jobs.size()) {
        result.errors << T("OmniVoice nie wygenerował wszystkich nagrań. Sprawdź instalację i nagranie referencyjne.");
        if (!diagnostics.trimmed().isEmpty())
            result.errors << QString::fromUtf8(diagnostics.trimmed());
    }
    return result;
}
} // namespace studio
