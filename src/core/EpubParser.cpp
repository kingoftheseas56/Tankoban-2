#include "EpubParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QUrl>
#include <QDebug>

#ifdef HAS_QT_ZIP
#include <private/qzipreader_p.h>
#endif

// ── Helpers ────────────────────────────────────────────────────────────────────

static QByteArray zipFileData(const QString& zipPath, const QString& innerPath)
{
#ifdef HAS_QT_ZIP
    QZipReader zip(zipPath);
    if (!zip.exists()) return {};
    return zip.fileData(innerPath);
#else
    Q_UNUSED(zipPath); Q_UNUSED(innerPath);
    return {};
#endif
}

static QString normalizePath(const QString& base, const QString& relative)
{
    if (relative.startsWith('/')) return relative.mid(1);
    if (base.isEmpty()) return relative;
    return QDir::cleanPath(base + '/' + relative);
}

// ── open ───────────────────────────────────────────────────────────────────────

bool EpubParser::open(const QString& epubPath)
{
    m_epubPath = epubPath;
    m_spine.clear();
    m_toc.clear();
    m_manifestById.clear();
    m_manifestByHref.clear();
    m_metadata = {};
    m_tempDir.clear();

    if (!parseContainer()) return false;
    if (!parseOpf()) return false;
    parseToc(); // optional — don't fail if missing
    return !m_spine.isEmpty();
}

// ── parseContainer ─────────────────────────────────────────────────────────────

bool EpubParser::parseContainer()
{
    QByteArray data = zipFileData(m_epubPath, "META-INF/container.xml");
    if (data.isEmpty()) return false;

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"rootfile") {
            m_opfPath = xml.attributes().value("full-path").toString();
            break;
        }
    }

    if (m_opfPath.isEmpty()) return false;

    int slash = m_opfPath.lastIndexOf('/');
    m_opfDir = (slash >= 0) ? m_opfPath.left(slash) : QString();
    return true;
}

// ── parseOpf ───────────────────────────────────────────────────────────────────

bool EpubParser::parseOpf()
{
    QByteArray data = zipFileData(m_epubPath, m_opfPath);
    if (data.isEmpty()) return false;

    QXmlStreamReader xml(data);

    // Temporary maps
    QMap<QString, QString> idToHref;
    QMap<QString, QString> idToMedia;
    QMap<QString, QString> idToProperties;
    QList<QString> spineRefs;

    enum Section { None, MetadataSection, ManifestSection, SpineSection };
    Section section = None;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            auto name = xml.name();

            if (name == u"metadata") { section = MetadataSection; continue; }
            if (name == u"manifest") { section = ManifestSection; continue; }
            if (name == u"spine")    { section = SpineSection; continue; }

            if (section == MetadataSection) {
                if (name == u"title" || xml.name() == u"title") {
                    if (m_metadata.title.isEmpty())
                        m_metadata.title = xml.readElementText();
                } else if (name == u"creator") {
                    if (m_metadata.author.isEmpty())
                        m_metadata.author = xml.readElementText();
                } else if (name == u"language") {
                    if (m_metadata.language.isEmpty())
                        m_metadata.language = xml.readElementText();
                }
            }

            if (section == ManifestSection && name == u"item") {
                QString id    = xml.attributes().value("id").toString();
                QString href  = xml.attributes().value("href").toString();
                QString media = xml.attributes().value("media-type").toString();
                QString props = xml.attributes().value("properties").toString();
                idToHref[id]  = href;
                idToMedia[id] = media;
                if (!props.isEmpty())
                    idToProperties[id] = props;
            }

            if (section == SpineSection && name == u"itemref") {
                QString idref = xml.attributes().value("idref").toString();
                spineRefs.append(idref);
            }
        }

        if (xml.isEndElement()) {
            auto name = xml.name();
            if (name == u"metadata" || name == u"manifest" || name == u"spine")
                section = None;
        }
    }

    // Build manifest maps with full paths
    for (auto it = idToHref.constBegin(); it != idToHref.constEnd(); ++it) {
        QString fullHref = resolveHref(it.value());
        m_manifestById[it.key()] = fullHref;
        m_manifestByHref[fullHref] = idToMedia.value(it.key());

        // Detect nav/ncx
        QString props = idToProperties.value(it.key());
        if (props.contains("nav"))
            m_navHref = fullHref;
        if (idToMedia.value(it.key()) == "application/x-dtbncx+xml")
            m_ncxHref = fullHref;
    }

    // Build spine
    for (const QString& idref : spineRefs) {
        if (!m_manifestById.contains(idref)) continue;
        Chapter ch;
        ch.href = m_manifestById[idref];
        ch.title = QFileInfo(ch.href).completeBaseName(); // fallback title
        m_spine.append(ch);
    }

    return !m_spine.isEmpty();
}

// ── parseToc ───────────────────────────────────────────────────────────────────

bool EpubParser::parseToc()
{
    // Try EPUB3 nav first, then EPUB2 NCX
    if (!m_navHref.isEmpty()) {
        QByteArray data = zipFileData(m_epubPath, m_navHref);
        if (!data.isEmpty()) {
            QXmlStreamReader xml(data);
            bool inNav = false;
            bool inOl = false;
            int depth = 0;

            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name() == u"nav") {
                        QString type = xml.attributes().value("epub:type").toString();
                        if (type.isEmpty())
                            type = xml.attributes().value("type").toString();
                        if (type == "toc") inNav = true;
                    }
                    if (inNav && xml.name() == u"ol") { inOl = true; depth++; }
                    if (inNav && inOl && xml.name() == u"a" && depth <= 2) {
                        Chapter ch;
                        QString href = xml.attributes().value("href").toString();
                        // Remove fragment
                        int hash = href.indexOf('#');
                        QString base = (hash >= 0) ? href.left(hash) : href;
                        // Resolve relative to nav document
                        int navSlash = m_navHref.lastIndexOf('/');
                        QString navDir = (navSlash >= 0) ? m_navHref.left(navSlash) : QString();
                        ch.href = normalizePath(navDir, base);
                        ch.title = xml.readElementText();
                        if (!ch.title.isEmpty())
                            m_toc.append(ch);
                    }
                }
                if (xml.isEndElement()) {
                    if (xml.name() == u"nav") inNav = false;
                    if (inNav && xml.name() == u"ol") depth--;
                }
            }
            if (!m_toc.isEmpty()) return true;
        }
    }

    // EPUB2 NCX fallback
    if (!m_ncxHref.isEmpty()) {
        QByteArray data = zipFileData(m_epubPath, m_ncxHref);
        if (!data.isEmpty()) {
            QXmlStreamReader xml(data);
            QString currentTitle;
            bool inNavPoint = false;

            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name() == u"navPoint") inNavPoint = true;
                    if (inNavPoint && xml.name() == u"text")
                        currentTitle = xml.readElementText();
                    if (inNavPoint && xml.name() == u"content") {
                        Chapter ch;
                        QString src = xml.attributes().value("src").toString();
                        int hash = src.indexOf('#');
                        QString base = (hash >= 0) ? src.left(hash) : src;
                        int ncxSlash = m_ncxHref.lastIndexOf('/');
                        QString ncxDir = (ncxSlash >= 0) ? m_ncxHref.left(ncxSlash) : QString();
                        ch.href = normalizePath(ncxDir, base);
                        ch.title = currentTitle;
                        if (!ch.title.isEmpty())
                            m_toc.append(ch);
                    }
                }
                if (xml.isEndElement() && xml.name() == u"navPoint")
                    inNavPoint = false;
            }
        }
    }

    return !m_toc.isEmpty();
}

// ── chapterHtml ────────────────────────────────────────────────────────────────

QString EpubParser::chapterHtml(int index) const
{
    if (index < 0 || index >= m_spine.size()) return {};

    const QString& href = m_spine[index].href;
    QByteArray data = zipFileData(m_epubPath, href);
    if (data.isEmpty()) return "<p>Chapter not found.</p>";

    QString html = QString::fromUtf8(data);

    // Create temp dir for images if not already created
    if (m_tempDir.isEmpty()) {
        auto* tmpDir = new QTemporaryDir();
        tmpDir->setAutoRemove(true);
        m_tempDir = tmpDir->path();
    }

    // Find the chapter's base directory for resolving relative paths
    int slash = href.lastIndexOf('/');
    QString chapterDir = (slash >= 0) ? href.left(slash) : QString();

    // Extract and resolve images
    static QRegularExpression imgRe(R"(<img\s[^>]*src\s*=\s*["']([^"']+)["'])");
    auto it = imgRe.globalMatch(html);
    QMap<QString, QString> replacements;

    while (it.hasNext()) {
        auto match = it.next();
        QString imgSrc = match.captured(1);
        if (imgSrc.startsWith("data:")) continue; // inline data URIs are fine

        QString imgPath = normalizePath(chapterDir, imgSrc);
        QByteArray imgData = zipFileData(m_epubPath, imgPath);
        if (imgData.isEmpty()) continue;

        // Save to temp dir
        QString ext = QFileInfo(imgSrc).suffix();
        QString tempFile = m_tempDir + '/' + QString::number(qHash(imgPath)) + '.' + ext;
        if (!QFile::exists(tempFile)) {
            QFile f(tempFile);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(imgData);
                f.close();
            }
        }

        replacements[imgSrc] = QUrl::fromLocalFile(tempFile).toString();
    }

    // Also handle CSS background images and SVG image tags
    static QRegularExpression svgImgRe(R"(xlink:href\s*=\s*["']([^"']+)["'])");
    auto svgIt = svgImgRe.globalMatch(html);
    while (svgIt.hasNext()) {
        auto match = svgIt.next();
        QString imgSrc = match.captured(1);
        if (imgSrc.startsWith("data:") || imgSrc.startsWith("http")) continue;

        QString imgPath = normalizePath(chapterDir, imgSrc);
        QByteArray imgData = zipFileData(m_epubPath, imgPath);
        if (imgData.isEmpty()) continue;

        QString ext = QFileInfo(imgSrc).suffix();
        QString tempFile = m_tempDir + '/' + QString::number(qHash(imgPath)) + '.' + ext;
        if (!QFile::exists(tempFile)) {
            QFile f(tempFile);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(imgData);
                f.close();
            }
        }
        replacements[imgSrc] = QUrl::fromLocalFile(tempFile).toString();
    }

    // Apply replacements
    for (auto it = replacements.constBegin(); it != replacements.constEnd(); ++it) {
        html.replace(it.key(), it.value());
    }

    // Strip XML namespaces — QTextBrowser doesn't understand them
    static QRegularExpression nsRe(R"(\s+xmlns(?::\w+)?="[^"]*")");
    html.replace(nsRe, QString());

    // Strip XML declarations and DOCTYPE
    static QRegularExpression xmlDeclRe(R"(<\?xml[^?]*\?>)");
    html.replace(xmlDeclRe, QString());
    static QRegularExpression doctypeRe(R"(<!DOCTYPE[^>]*>)", QRegularExpression::CaseInsensitiveOption);
    html.replace(doctypeRe, QString());

    // Strip <style> blocks — EPUB CSS fights our dark theme
    static QRegularExpression styleRe(R"(<style[^>]*>[\s\S]*?</style>)", QRegularExpression::CaseInsensitiveOption);
    html.replace(styleRe, QString());

    // Strip <link rel="stylesheet"> tags
    static QRegularExpression linkCssRe(R"(<link[^>]*rel=["']stylesheet["'][^>]*/?>)", QRegularExpression::CaseInsensitiveOption);
    html.replace(linkCssRe, QString());

    // Strip inline style attributes — they override our theme
    static QRegularExpression inlineStyleRe(R"(\s+style="[^"]*")", QRegularExpression::CaseInsensitiveOption);
    html.replace(inlineStyleRe, QString());

    // Extract <body> content only — avoid nested <html> when we wrap it later
    static QRegularExpression bodyRe(R"(<body[^>]*>([\s\S]*)</body>)", QRegularExpression::CaseInsensitiveOption);
    auto bodyMatch = bodyRe.match(html);
    if (bodyMatch.hasMatch()) {
        html = bodyMatch.captured(1);
    }

    return html;
}

// ── resolveHref ────────────────────────────────────────────────────────────────

QString EpubParser::resolveHref(const QString& href) const
{
    return normalizePath(m_opfDir, href);
}
