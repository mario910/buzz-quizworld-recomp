#include "tts.h"
#include "languages.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantMap>
#include <algorithm>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <sapi.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
struct ComSession {
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComSession() {
        if (SUCCEEDED(status))
            CoUninitialize();
    }
};
QString comError(HRESULT hr) {
    return QCoreApplication::translate("Speech", "Windows SAPI: błąd 0x%1.")
        .arg(quint32(hr), 8, 16, QLatin1Char('0'));
}
} // namespace
#endif

namespace studio {
QVariantList speechVoices() {
    QVariantList voices;
#ifdef Q_OS_WIN
    ComSession session;
    ComPtr<ISpObjectTokenCategory> category;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(category.GetAddressOf()))) ||
        FAILED(category->SetId(SPCAT_VOICES, FALSE)))
        return voices;
    ComPtr<IEnumSpObjectTokens> tokens;
    if (FAILED(category->EnumTokens(nullptr, nullptr, &tokens)))
        return voices;
    ComPtr<ISpObjectToken> token;
    while (tokens->Next(1, &token, nullptr) == S_OK) {
        wchar_t *id = nullptr, *name = nullptr;
        if (SUCCEEDED(token->GetId(&id)) && SUCCEEDED(token->GetStringValue(nullptr, &name))) {
            QString language;
            ComPtr<ISpDataKey> attributes;
            wchar_t *tag = nullptr;
            if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes)) &&
                SUCCEEDED(attributes->GetStringValue(L"Language", &tag))) {
                const auto lcid = QString::fromWCharArray(tag).section(';', 0, 0).toUInt(nullptr, 16);
                wchar_t iso[16]{};
                if (GetLocaleInfoW(lcid, LOCALE_SISO639LANGNAME, iso, 16))
                    language = QString::fromWCharArray(iso);
            }
            CoTaskMemFree(tag);
            voices << QVariantMap{{"id", QString::fromWCharArray(id)},
                                  {"name", QString::fromWCharArray(name)},
                                  {"language", language}};
        }
        CoTaskMemFree(id);
        CoTaskMemFree(name);
        token.Reset();
    }
#else
    const auto engine = QStandardPaths::findExecutable("espeak-ng");
    if (!engine.isEmpty()) {
        QProcess probe;
        probe.start(engine, {"--voices"});
        if (!probe.waitForFinished(3000)) {
            probe.kill();
            probe.waitForFinished(1000);
            return voices;
        }
        QSet<QString> codes;
        const auto lines = QString::fromUtf8(probe.readAllStandardOutput()).split('\n');
        for (const auto &line : lines.mid(1)) {
            const auto columns = line.simplified().split(' ');
            if (columns.size() >= 4 && !languageCode(columns[1]).isEmpty())
                codes.insert(columns[1]);
        }
        auto sorted = codes.values();
        sorted.sort();
        for (const auto &code : sorted)
            voices << QVariantMap{{"id", code},
                                  {"name", languageName(code) + " · eSpeak NG"},
                                  {"language", languageCode(code)}};
    }
#endif
    return voices;
}

SpeechResult synthesizeSpeech(const QString &text, const QString &voiceId, int rate, const QString &path) {
    SpeechResult result;
    if (text.trimmed().isEmpty() || text.size() > 10000) {
        result.error =
            QCoreApplication::translate("Speech", "Tekst lektora musi mieć od 1 do 10 000 znaków.");
        return result;
    }
    if (voiceId.isEmpty() || path.isEmpty()) {
        result.error = QCoreApplication::translate("Speech", "Wybierz głos i plik docelowy WAV.");
        return result;
    }
    QTemporaryDir temp;
    if (!temp.isValid()) {
        result.error = QCoreApplication::translate("Speech", "Nie można utworzyć tymczasowego pliku audio.");
        return result;
    }
    const auto wav = temp.filePath("speech.wav");
#ifdef Q_OS_WIN
    ComSession session;
    ComPtr<ISpVoice> voice;
    ComPtr<ISpObjectToken> token;
    ComPtr<ISpStream> stream;
    HRESULT hr =
        CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(voice.GetAddressOf()));
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(token.GetAddressOf()));
    if (SUCCEEDED(hr))
        hr = token->SetId(nullptr, reinterpret_cast<const wchar_t *>(voiceId.utf16()), FALSE);
    if (SUCCEEDED(hr))
        hr = voice->SetVoice(token.Get());
    if (SUCCEEDED(hr))
        hr = voice->SetRate(std::clamp(rate, -5, 5));
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(stream.GetAddressOf()));
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0};
    if (SUCCEEDED(hr))
        hr = stream->BindToFile(reinterpret_cast<const wchar_t *>(wav.utf16()), SPFM_CREATE_ALWAYS,
                                &SPDFID_WaveFormatEx, &format, 0);
    if (SUCCEEDED(hr))
        hr = voice->SetOutput(stream.Get(), TRUE);
    if (SUCCEEDED(hr))
        hr = voice->Speak(reinterpret_cast<const wchar_t *>(text.utf16()), SPF_IS_NOT_XML, nullptr);
    voice.Reset();
    if (stream)
        stream->Close();
    stream.Reset();
    if (FAILED(hr)) {
        result.error = comError(hr);
        return result;
    }
#else
    const auto engine = QStandardPaths::findExecutable("espeak-ng");
    if (engine.isEmpty()) {
        result.error = QCoreApplication::translate(
            "Speech", "Zainstaluj eSpeak NG (pakiet espeak-ng), aby generować głos lokalnie.");
        return result;
    }
    QProcess process;
    // Arguments are passed directly. Imported text is stdin, never shell code.
    process.start(engine, {"-v", voiceId, "-s", QString::number(175 + std::clamp(rate, -5, 5) * 20), "-w",
                           wav, "--stdin"});
    if (!process.waitForStarted(5000)) {
        result.error = process.errorString();
        return result;
    }
    process.write(text.toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(5000);
        result.error = QCoreApplication::translate("Speech", "Silnik TTS przekroczył limit czasu.");
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QCoreApplication::translate("Speech", "eSpeak NG: ") +
                       QString::fromUtf8(process.readAllStandardError()).left(1000);
        return result;
    }
#endif
    QFile input(wav);
    if (!input.open(QIODevice::ReadOnly) || input.size() <= 44 || input.size() > 64 * 1024 * 1024) {
        result.error =
            QCoreApplication::translate("Speech", "Silnik TTS nie wygenerował poprawnego pliku WAV.");
        return result;
    }
    const auto bytes = input.readAll();
    if (bytes.left(4) != "RIFF" || bytes.mid(8, 4) != "WAVE") {
        result.error = QCoreApplication::translate("Speech", "Silnik TTS zwrócił plik inny niż WAV.");
        return result;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        result.error = output.errorString();
        return result;
    }
    result.path = QFileInfo(path).absoluteFilePath();
    return result;
}
} // namespace studio
