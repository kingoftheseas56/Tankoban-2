#pragma once

#include <QString>
#include <QList>
#include <QMap>

class EpubParser {
public:
    struct Chapter {
        QString title;
        QString href;   // path inside ZIP (relative to OPF dir)
    };

    struct Metadata {
        QString title;
        QString author;
        QString language;
    };

    bool open(const QString& epubPath);

    const Metadata& metadata() const { return m_metadata; }
    int chapterCount() const { return m_spine.size(); }
    const QList<Chapter>& spine() const { return m_spine; }
    const QList<Chapter>& tableOfContents() const { return m_toc; }

    // Returns chapter XHTML with images resolved to absolute temp paths
    QString chapterHtml(int index) const;

private:
    bool parseContainer();
    bool parseOpf();
    bool parseToc();
    QString resolveHref(const QString& href) const;

    QString m_epubPath;
    QString m_opfPath;      // full path inside ZIP to OPF file
    QString m_opfDir;       // directory containing OPF (for resolving relative hrefs)
    Metadata m_metadata;
    QList<Chapter> m_spine;
    QList<Chapter> m_toc;
    QMap<QString, QString> m_manifestById;   // id → href (relative to OPF dir)
    QMap<QString, QString> m_manifestByHref; // href → media-type
    QString m_navHref;      // EPUB3 nav document href
    QString m_ncxHref;      // EPUB2 NCX href

    mutable QString m_tempDir;  // for extracted images
};
