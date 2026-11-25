#pragma once
#include "document.h"

// A project is a folder: name.buzz.json plus the media it uses,
//   media/images/…         pictures
//   media/audio/<lang>/…   question speech per language
//   media/video/…          videos
// with relative links in the JSON. A .buzzproject file is the same folder as
// one ZIP, for moving a project between computers.
namespace studio {
struct MediaReport {
    int copied = 0;  // Files copied into the project's media folder.
    int missing = 0; // Linked files that no longer exist (links are kept).
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// Copies every linked image/audio/video that lives outside projectDir (including
// speech imported from games, which starts in the application cache) into
// projectDir/media and relinks it. Files already inside the project stay put.
MediaReport collectMedia(Document &document, const QString &projectDir);
// Writes the project and all its media into one archive.
MediaReport exportProjectArchive(const Document &document, const QString &archivePath);
bool isProjectArchive(const QString &path);
// Unpacks into a new folder next to the archive and loads the project from there;
// LoadResult::projectPath names the unpacked .buzz.json.
LoadResult openProjectArchive(const QString &archivePath);
} // namespace studio
