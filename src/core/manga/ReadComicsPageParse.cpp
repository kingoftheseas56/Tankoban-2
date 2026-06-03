// src/core/manga/ReadComicsPageParse.cpp
#include "ReadComicsPageParse.h"
#include <QRegularExpression>
#include <QByteArray>

namespace tankoban::manga::readcomics {

QString applyReplacements(const QString& token,
                          const QList<QPair<QString, QString>>& replacements)
{
    QString s = token;
    for (const auto& r : replacements) s.replace(r.first, r.second);
    return s;
}

QString baeu(const QString& urlIn, const QString& rootIn)
{
    static const QString kBlogspot = QStringLiteral("https://2.bp.blogspot.com");
    const QString root = rootIn.isEmpty() ? kBlogspot : rootIn;

    QString url = urlIn;
    url.replace(QStringLiteral("pw_.g28x"), QStringLiteral("b"));
    url.replace(QStringLiteral("d2pr.x_27"), QStringLiteral("h"));

    if (url.startsWith(QLatin1String("https")))
        return QString(url).replace(kBlogspot, root);

    const int q = url.indexOf(QLatin1Char('?'));
    QString path        = (q < 0) ? url : url.left(q);
    const QString sep   = (q < 0) ? QString() : QStringLiteral("?");
    const QString query = (q < 0) ? QString() : url.mid(q + 1);

    const bool s0 = path.endsWith(QLatin1String("=s0"));   // suffix, not substring
    path.chop(s0 ? 3 : 6);                                  // strip =s0 / =s1600
    path = path.mid(15, 33 - 15) + path.mid(50);            // step1: [15:33] + [50:]
    path = path.left(path.size() - 11) + path.right(2);     // step2: [:-11] + [-2:]
    path = QString::fromUtf8(QByteArray::fromBase64(path.toUtf8()));  // atob
    path = path.left(13) + path.mid(17);                    // [:13] + [17:]
    path = path.left(path.size() - 2) + (s0 ? QStringLiteral("=s0")
                                            : QStringLiteral("=s1600"));
    return root + QStringLiteral("/") + path + sep + query;
}

QList<PageInfo> parseReaderPages(const QString& html)
{
    QList<PageInfo> pages;

    // root passed to baeu(l, '<root>')
    static const QRegularExpression rootRe(QStringLiteral(R"rx(return baeu\(l, '([^']*)')rx"));
    const auto rootM = rootRe.match(html);
    if (!rootM.hasMatch()) return pages;            // not the obfuscated reader -> empty
    const QString root = rootM.captured(1);

    // junk-token replacements: l = l.replace(/X/g, 'Y') (the first is page-randomized)
    QList<QPair<QString, QString>> repls;
    static const QRegularExpression replRe(
        QStringLiteral(R"rx(l = l\.replace\(/([^/]+)/g, ["']([^"']*)["']\))rx"));
    auto rit = replRe.globalMatch(html);
    while (rit.hasNext()) {
        const auto m = rit.next();
        repls.append({m.captured(1), m.captured(2)});
    }

    // PAGE IMAGES: the reader stores each page as a repeated `htp = '<scrambled>';`
    // (hi-res; `pth = '...'` is the lo-res twin) then `_xxx.push(htp)`. Extract
    // every htp assignment and descramble it. The init `var htp = 'rcox'`
    // descrambles to a non-https sentinel and is filtered out. (The earlier
    // split-on-var approach grabbed the wrong, recurring variable and pulled in
    // garbage tokens that hung the downloader — smoke 2026-06-03.)
    static const QRegularExpression htpRe(QStringLiteral(R"rx(htp = '([^']*)')rx"));
    auto it = htpRe.globalMatch(html);
    int idx = 0;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString raw = m.captured(1);
        // The `var htp = 'rcox'` init (and any stray short assignment) is far
        // shorter than a real scrambled blogspot URL (~200 chars) and would
        // otherwise descramble to a bare "blogspot.com/=s1600". Skip by length.
        if (raw.size() < 40) continue;
        const QString url = baeu(applyReplacements(raw, repls), root);
        if (!url.startsWith(QLatin1String("https://2.bp.blogspot.com/"))) continue;
        PageInfo p;
        p.index    = idx++;
        p.imageUrl = url;
        pages.append(p);
    }
    return pages;
}

} // namespace tankoban::manga::readcomics
