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

    const bool s0 = path.contains(QLatin1String("=s0"));
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

    // array var name: after `var pth = '...';` comes `var <NAME> = '`
    static const QRegularExpression varRe(QStringLiteral(R"rx(var pth = '[^']*';\s*var (\w+)\s*=\s*')rx"));
    const auto varM = varRe.match(html);
    if (!varM.hasMatch()) return pages;
    const QString var = varM.captured(1);

    // junk-token replacements: l = l.replace(/X/g, 'Y')
    QList<QPair<QString, QString>> repls;
    static const QRegularExpression replRe(
        QStringLiteral(R"rx(l = l\.replace\(/([^/]+)/g, ["']([^"']*)["']\))rx"));
    auto rit = replRe.globalMatch(html);
    while (rit.hasNext()) {
        const auto m = rit.next();
        repls.append({m.captured(1), m.captured(2)});
    }

    // each token: html.split(var)[2:], take the `= '...'` string
    static const QRegularExpression tokRe(QStringLiteral(R"rx(= '([^']*)')rx"));
    const QStringList parts = html.split(var);
    int idx = 0;
    for (int i = 2; i < parts.size(); ++i) {
        const auto m = tokRe.match(parts.at(i));
        if (!m.hasMatch()) continue;
        const QString url = baeu(applyReplacements(m.captured(1), repls), root);
        if (!url.startsWith(QLatin1String("https://"))) continue;   // skip junk
        PageInfo p;
        p.index    = idx++;
        p.imageUrl = url;
        pages.append(p);
    }
    return pages;
}

} // namespace tankoban::manga::readcomics
