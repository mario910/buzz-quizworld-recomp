#pragma once
#include "document.h"
#include "languages.h"
#include <atomic>
#include <functional>
#include <memory>

namespace studio {
namespace pack {
class Archive;
}
// One Quiz World / Quiz TV / Brain of... question archive (PAK or decrypted EDAT).
struct BuzzPack {
    // Opens the archive; it may live inside a disc image or a PS3 package.
    std::function<std::unique_ptr<pack::Archive>()> open;
    QString label;    // Shown as the question's source_archive.
    QString identity; // Keys the media cache; changes when the source changes.
    QString language; // Editor code from the file name, or empty when unknown.
    QString sourcePath; // ISO, package or PARAM.SFO used to reopen the pack.
};
// Read-only parsing: retail Lua bytecode is never executed.
LoadResult importBuzz(const QString &path, const QString &language = {}, bool extractSpeech = true);
// Imports every pack that holds the selected language (all packs of a disc or DLC).
LoadResult importBuzzPacks(const QVector<BuzzPack> &packs, const QString &language, bool extractSpeech,
                           const QString &title, bool narrationSample = false);
BuzzPack buzzFilePack(const QString &path);
SourceLanguages buzzLanguages(const QString &path);
QStringList buzzArchiveLanguages(pack::Archive &archive);
bool hasBuzzSourcePicture(const Question &question);
int recoverBuzzPictures(QVector<Question> &questions, const QString &ffmpeg, QStringList &warnings,
                       const std::atomic_bool *cancel);
bool hasBuzzSourceMusic(const Question &question);
void recoverBuzzMusic(QVector<Question> &questions, QStringList &warnings, const std::atomic_bool *cancel = nullptr);
QStringList decodeHuffman(const QByteArray &header, const QByteArray &stream);
} // namespace studio
