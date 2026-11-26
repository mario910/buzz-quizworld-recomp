#pragma once
#include <QLocale>
#include <QObject>
#include <QPair>
#include <QVector>
#include <QTranslator>

class Localization final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
  public:
    explicit Localization(QObject *parent = nullptr, const QString &initialLanguage = {});
    QString language() const { return language_; }
    static QString defaultLanguage(const QLocale &system);
    // (code, native name) of every interface language.
    static const QVector<QPair<QString, QString>> &languages();
    Q_INVOKABLE void setLanguage(const QString &language);
  signals:
    void languageChanged();

  private:
    QString language_;
    QTranslator application_, qt_;
    bool persist_ = false;
};
