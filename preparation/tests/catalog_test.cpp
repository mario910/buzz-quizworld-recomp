#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QtTest>

// BuzzSetup's interface catalogs: one per Buzz! game language except the
// English source, each complete and keeping placeholders and file filters.
class CatalogTest : public QObject {
    Q_OBJECT

    static QStringList placeholders(const QString& text) {
        static const QRegularExpression tokens("%[1-9][0-9]*|%n|\\*\\.[a-zA-Z0-9.]+|\\(\\*\\)");
        QStringList values;
        auto matches = tokens.globalMatch(text);
        while (matches.hasNext()) values << matches.next().captured();
        values.sort();
        return values;
    }

private slots:
    void catalogsAreCompleteAndKeepPlaceholders() {
        const QDir folder(QFileInfo(QFINDTESTDATA("../i18n/buzzsetup_pl.ts")).absolutePath());
        const auto catalogs = folder.entryList({"buzzsetup_*.ts"}, QDir::Files);
        QCOMPARE(catalogs.size(), 15);
        QSet<QString> reference;
        for (const auto& name : catalogs) {
            QFile catalog(folder.filePath(name));
            QVERIFY(catalog.open(QIODevice::ReadOnly));
            QXmlStreamReader xml(&catalog);
            QString source, translated;
            QSet<QString> sources;
            bool unfinished = false;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QStringLiteral("message")) {
                    source.clear();
                    translated.clear();
                    unfinished = false;
                } else if (xml.isStartElement() && xml.name() == QStringLiteral("source")) {
                    source = xml.readElementText();
                } else if (xml.isStartElement() && xml.name() == QStringLiteral("translation")) {
                    unfinished = xml.attributes().value("type") == QStringLiteral("unfinished");
                    translated = xml.readElementText();
                } else if (xml.isEndElement() && xml.name() == QStringLiteral("message")) {
                    QVERIFY2(!unfinished && !translated.trimmed().isEmpty(), qPrintable(name + ": " + source));
                    QVERIFY2(placeholders(source) == placeholders(translated), qPrintable(name + ": " + translated));
                    QCOMPARE(translated.count('\n'), source.count('\n'));
                    sources.insert(source);
                }
            }
            QVERIFY2(!xml.hasError(), qPrintable(name));
            QVERIFY(sources.size() > 150);
            if (reference.isEmpty()) reference = sources;
            QVERIFY2(sources == reference, qPrintable(name + " translates a different set of messages"));
        }
    }
};

QTEST_GUILESS_MAIN(CatalogTest)
#include "catalog_test.moc"
