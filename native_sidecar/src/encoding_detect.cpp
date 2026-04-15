#include "encoding_detect.h"
#include <uchardet/uchardet.h>
#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

// Map uchardet charset name to Windows codepage.
// Returns 0 if unknown (caller should treat as UTF-8).
static unsigned int charset_to_codepage(const std::string& charset) {
    // Normalize to uppercase for matching
    std::string upper = charset;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (upper == "SHIFT_JIS" || upper == "SHIFT-JIS")   return 932;
    if (upper == "EUC-JP")                               return 20932;
    if (upper == "ISO-2022-JP")                          return 50220;
    if (upper == "GB2312" || upper == "GB18030")         return 936;
    if (upper == "GBK")                                  return 936;
    if (upper == "BIG5")                                 return 950;
    if (upper == "EUC-KR")                               return 949;
    if (upper == "ISO-8859-1")                           return 28591;
    if (upper == "ISO-8859-2")                           return 28592;
    if (upper == "ISO-8859-3")                           return 28593;
    if (upper == "ISO-8859-4")                           return 28594;
    if (upper == "ISO-8859-5")                           return 28595;
    if (upper == "ISO-8859-6")                           return 28596;
    if (upper == "ISO-8859-7")                           return 28597;
    if (upper == "ISO-8859-8")                           return 28598;
    if (upper == "ISO-8859-9")                           return 28599;
    if (upper == "ISO-8859-15")                          return 28605;
    if (upper == "WINDOWS-1250")                         return 1250;
    if (upper == "WINDOWS-1251")                         return 1251;
    if (upper == "WINDOWS-1252")                         return 1252;
    if (upper == "WINDOWS-1253")                         return 1253;
    if (upper == "WINDOWS-1254")                         return 1254;
    if (upper == "WINDOWS-1255")                         return 1255;
    if (upper == "WINDOWS-1256")                         return 1256;
    if (upper == "KOI8-R")                               return 20866;
    if (upper == "TIS-620")                              return 874;
    return 0;
}

std::string detect_encoding(const char* data, size_t len) {
    if (!data || len == 0)
        return "UTF-8";

    uchardet_t ud = uchardet_new();
    if (!ud)
        return "UTF-8";

    int rc = uchardet_handle_data(ud, data, len);
    uchardet_data_end(ud);

    std::string result;
    if (rc == 0) {
        const char* cs = uchardet_get_charset(ud);
        if (cs && cs[0] != '\0')
            result = cs;
    }

    uchardet_delete(ud);

    if (result.empty())
        return "UTF-8";

    // Normalize ASCII to UTF-8 (ASCII is a subset)
    std::string upper = result;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (upper == "ASCII")
        return "UTF-8";

    return result;
}

std::string transcode_to_utf8(const char* data, size_t len) {
    if (!data || len == 0)
        return {};

    std::string encoding = detect_encoding(data, len);

    // Normalize for comparison
    std::string upper = encoding;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (upper == "UTF-8")
        return std::string(data, len);

#ifdef _WIN32
    unsigned int codepage = charset_to_codepage(encoding);
    if (codepage == 0)
        return std::string(data, len);

    // Step 1: multibyte -> wide char
    int wlen = MultiByteToWideChar(codepage, 0, data, static_cast<int>(len), nullptr, 0);
    if (wlen <= 0)
        return std::string(data, len);

    std::wstring wide(wlen, L'\0');
    MultiByteToWideChar(codepage, 0, data, static_cast<int>(len), &wide[0], wlen);

    // Step 2: wide char -> UTF-8
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0)
        return std::string(data, len);

    std::string utf8(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, &utf8[0], u8len, nullptr, nullptr);
    return utf8;
#else
    // Non-Windows: return as-is (iconv support can be added later)
    return std::string(data, len);
#endif
}
