// Metadata.cpp : the EXIF / PNG-tEXt / WebP-XMP / JXL-box / AVIF-XMP+Exif
// readers, the metadata viewer window and the in-app Properties dialog.
//
// What this file needs that its header does not already give it:
//   * "Decoding.h" - AvifOpenAndParse is reused by OpenAvifExifMetadataReader
//     and ReadAvifXmpFields: the metadata readers share the AVIF decoder setup
//     with the image loader.
//   * <avif/avif.h> + <jxl/decode.h> - the AVIF Exif/XMP readers use
//     avifDecoder/avifGetExifTiffHeaderOffset and ReadJxlMetadataBoxes drives
//     a JxlDecoder in box mode. A header (and its library) has to be included
//     wherever the code that needs it lives.
//   * <algorithm> (std::transform, std::min), <cstring> (memcmp/memcpy/
//     strcmp), <cstdio> (swprintf_s), <string>, <vector>.
//   * <commctrl.h> + comctl32.lib - ShowImageMetadata reports "no metadata
//     found" with TaskDialog(); the main file has no commctrl.h include of its
//     own, so each module that calls a TaskDialog* brings its own.
//
// The 27 `static` reader helpers keep internal linkage because nothing outside
// this file calls them. AppendRationalField's default argument stays on its
// definition: the definition precedes every call, so a second declaration would
// mean a second default (C2522). The two file-scope POD structs (PngTextField,
// ImagePropertiesFields) belong to the functions that use them.

#include "Metadata.h"
#include "Decoding.h"   // AvifOpenAndParse (AVIF Exif + XMP readers)

// ---- Third-party image codecs (bundled so users don't need OS/Store codecs) ----
// Get these via vcpkg (recommended):
//     vcpkg install libavif dav1d libjxl
#include <avif/avif.h>
#include <jxl/decode.h>

#include <algorithm>
#include <commctrl.h>   // TaskDialog (the "no metadata found" report)
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

// ============================================================================
// EnableDarkTitleBar lives in the main file with the rest of the window
// plumbing. MetadataViewerWndProc calls it to darken the viewer's own title
// bar, so it is declared here.
// ============================================================================
void EnableDarkTitleBar(HWND hwnd);

// ---------------------------------------------------------------------
// Image metadata (EXIF / PNG text chunk) viewer
// ---------------------------------------------------------------------

// WIC stores EXIF RATIONAL values packed into a single UI8: the numerator
// in the high 32 bits, the denominator in the low 32 bits.
static bool RationalFromPackedUI8(ULONGLONG packed, double& outValue)
{
    UINT32 numerator   = static_cast<UINT32>(packed >> 32);
    UINT32 denominator = static_cast<UINT32>(packed & 0xFFFFFFFFull);
    if (denominator == 0)
        return false;
    outValue = static_cast<double>(numerator) / static_cast<double>(denominator);
    return true;
}

// Pulls a displayable string out of the PROPVARIANT types WIC's EXIF/PNG
// metadata readers actually return. Returns false if the variant is empty
// or of a type this viewer doesn't render.
static bool PropVariantToDisplayString(const PROPVARIANT& pv, std::wstring& out)
{
    switch (pv.vt)
    {
    case VT_LPWSTR:
        if (!pv.pwszVal) return false;
        out = pv.pwszVal;
        return !out.empty();

    case VT_LPSTR:  // EXIF ASCII fields (Make, Model, DateTime, ...) come back narrow
    {
        if (!pv.pszVal) return false;
        std::string s(pv.pszVal);
        while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
            s.pop_back();
        out.assign(s.begin(), s.end());
        return !out.empty();
    }

    case VT_UI1: out = std::to_wstring(static_cast<unsigned>(pv.bVal));  return true;
    case VT_UI2: out = std::to_wstring(static_cast<unsigned>(pv.uiVal)); return true;
    case VT_UI4: out = std::to_wstring(static_cast<unsigned long>(pv.ulVal)); return true;
    case VT_I4:  out = std::to_wstring(static_cast<long>(pv.lVal));      return true;
    case VT_UI8: out = std::to_wstring(static_cast<unsigned long long>(pv.uhVal.QuadPart)); return true;

    // Some readers hand back a single-element vector instead of a scalar
    // (ISOSpeedRatings is the classic example).
    case (VT_VECTOR | VT_UI2):
        if (pv.caui.cElems == 0) return false;
        out = std::to_wstring(static_cast<unsigned>(pv.caui.pElems[0]));
        return true;
    case (VT_VECTOR | VT_UI4):
        if (pv.caul.cElems == 0) return false;
        out = std::to_wstring(static_cast<unsigned long>(pv.caul.pElems[0]));
        return true;

    default:
        return false;
    }
}

// Reads one tag by WIC metadata query path and appends "label: value\n" to
// 'out' if present. Returns true if the field was found.
static bool AppendSimpleField(IWICMetadataQueryReader* reader, const wchar_t* path,
                               const wchar_t* label, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        std::wstring val;
        if (PropVariantToDisplayString(pv, val))
        {
            out += std::wstring(label) + L": " + val + L"\n";
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// Same idea but reads a packed EXIF rational and applies custom formatting -
// exposure time as a fraction of a second, f-number/focal length as decimals.
static bool AppendRationalField(IWICMetadataQueryReader* reader, const wchar_t* path,
                                 const wchar_t* label, const wchar_t* fmt,
                                 std::wstring& out, bool asShutterFraction = false)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)) && pv.vt == VT_UI8)
    {
        double value;
        if (RationalFromPackedUI8(pv.uhVal.QuadPart, value) && value > 0.0)
        {
            wchar_t buf[64];
            if (asShutterFraction && value < 1.0)
                swprintf_s(buf, L"1/%.0f s", 1.0 / value);
            else
                swprintf_s(buf, fmt, value);
            out += std::wstring(label) + L": " + buf + L"\n";
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// GPS latitude/longitude are each three packed rationals (degrees, minutes,
// seconds) plus a hemisphere reference ("N"/"S", "E"/"W"). 'ifdGpsRoot' is
// the container-specific root path (e.g. "/app1/ifd/gps" for JPEG,
// "/ifd/gps" for PNG/WebP).
static bool AppendGpsCoordinate(IWICMetadataQueryReader* reader, const wchar_t* ifdGpsRoot, std::wstring& out)
{
    PROPVARIANT latRef, lat, lonRef, lon;
    PropVariantInit(&latRef); PropVariantInit(&lat);
    PropVariantInit(&lonRef); PropVariantInit(&lon);

    std::wstring root = ifdGpsRoot;
    bool ok =
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=1}").c_str(), &latRef)) &&
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=2}").c_str(), &lat))    &&
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=3}").c_str(), &lonRef)) &&
        SUCCEEDED(reader->GetMetadataByName((root + L"/{ushort=4}").c_str(), &lon))    &&
        lat.vt == (VT_VECTOR | VT_UI8) && lat.cauh.cElems == 3 &&
        lon.vt == (VT_VECTOR | VT_UI8) && lon.cauh.cElems == 3;

    if (ok)
    {
        auto toDecimalDegrees = [](const PROPVARIANT& dms) -> double
        {
            double deg = 0.0, min = 0.0, sec = 0.0;
            RationalFromPackedUI8(dms.cauh.pElems[0].QuadPart, deg);
            RationalFromPackedUI8(dms.cauh.pElems[1].QuadPart, min);
            RationalFromPackedUI8(dms.cauh.pElems[2].QuadPart, sec);
            return deg + min / 60.0 + sec / 3600.0;
        };

        double latDec = toDecimalDegrees(lat);
        double lonDec = toDecimalDegrees(lon);

        std::wstring latRefStr, lonRefStr;
        PropVariantToDisplayString(latRef, latRefStr);
        PropVariantToDisplayString(lonRef, lonRefStr);

        if (!latRefStr.empty() && towlower(latRefStr[0]) == L's')
            latDec = -latDec;
        if (!lonRefStr.empty() && towlower(lonRefStr[0]) == L'w')
            lonDec = -lonDec;

        wchar_t buf[128];
        swprintf_s(buf, L"GPS location: %.6f, %.6f\n", latDec, lonDec);
        out += buf;
    }

    PropVariantClear(&latRef); PropVariantClear(&lat);
    PropVariantClear(&lonRef); PropVariantClear(&lon);
    return ok;
}

// Human-readable EXIF Orientation (tag 274) values: 1-8, describing the
// rotation/mirroring needed to display the image upright.
static const wchar_t* OrientationName(unsigned value)
{
    switch (value)
    {
    case 1: return L"Normal";
    case 2: return L"Flipped horizontally";
    case 3: return L"Rotated 180\u00B0";
    case 4: return L"Flipped vertically";
    case 5: return L"Rotated 90\u00B0 CW, then flipped horizontally";
    case 6: return L"Rotated 90\u00B0 CW";
    case 7: return L"Rotated 90\u00B0 CCW, then flipped horizontally";
    case 8: return L"Rotated 90\u00B0 CCW";
    default: return L"Unknown";
    }
}

// ---- Bare-value variants for the "Properties" dialog (IDD_PROPERTIES) ----
// The AppendXField() helpers above build "label: value\n" lines for the
// free-form "View metadata" text dump. The Properties dialog instead has a
// dedicated static control per field, so these return just the value (no
// label, no trailing newline) for SetDlgItemTextW to drop straight in.

static bool ReadDateTakenValue(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
        found = PropVariantToDisplayString(pv, out);
    PropVariantClear(&pv);
    return found;
}

static bool ReadColorSpaceValue(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        unsigned v = (pv.vt == VT_UI2) ? pv.uiVal : (pv.vt == VT_UI4 ? pv.ulVal : 0u);
        if (v == 1)          { out = L"sRGB";                        found = true; }
        else if (v == 0xFFFF) { out = L"Uncalibrated (non-sRGB)";     found = true; }
    }
    PropVariantClear(&pv);
    return found;
}

static bool ReadOrientationValue(IWICMetadataQueryReader* reader, const wchar_t* path, std::wstring& out)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(reader->GetMetadataByName(path, &pv)))
    {
        unsigned v = (pv.vt == VT_UI2) ? pv.uiVal : (pv.vt == VT_UI4 ? pv.ulVal : 0u);
        if (v >= 1 && v <= 8)
        {
            out = OrientationName(v);
            found = true;
        }
    }
    PropVariantClear(&pv);
    return found;
}

// Renders a byte count the way Explorer's Properties dialog does: a
// human-scaled size followed by the exact byte count in parentheses.
static std::wstring FormatFileSizeW(ULONGLONG bytes)
{
    wchar_t buf[96];
    if (bytes < 1024ULL)
        swprintf_s(buf, L"%llu bytes", bytes);
    else if (bytes < 1024ULL * 1024)
        swprintf_s(buf, L"%.1f KB (%llu bytes)", bytes / 1024.0, bytes);
    else if (bytes < 1024ULL * 1024 * 1024)
        swprintf_s(buf, L"%.2f MB (%llu bytes)", bytes / (1024.0 * 1024.0), bytes);
    else
        swprintf_s(buf, L"%.2f GB (%llu bytes)", bytes / (1024.0 * 1024.0 * 1024.0), bytes);
    return buf;
}

// Formats a FILETIME (as returned by GetFileAttributesEx, i.e. UTC) using
// the user's locale short-date and time formats.
static std::wstring FormatFileTimeW(const FILETIME& ftUtc)
{
    if (ftUtc.dwLowDateTime == 0 && ftUtc.dwHighDateTime == 0)
        return L"Unknown";

    FILETIME ftLocal;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ftUtc, &ftLocal) || !FileTimeToSystemTime(&ftLocal, &st))
        return L"Unknown";

    wchar_t dateBuf[64] = {};
    wchar_t timeBuf[64] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, dateBuf, ARRAYSIZE(dateBuf), nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, timeBuf, ARRAYSIZE(timeBuf));
    return std::wstring(dateBuf) + L"  " + timeBuf;
}

// Maps this app's supported file extensions to a display-friendly format
// name. Kept in sync with the extension list in IsSupportedImage.
static const wchar_t* ImageFormatNameFromExtension(const std::wstring& extLower)
{
    if (extLower == L".jpg" || extLower == L".jpeg") return L"JPEG";
    if (extLower == L".png")                          return L"PNG";
    if (extLower == L".bmp")                           return L"BMP";
    if (extLower == L".gif")                           return L"GIF";
    if (extLower == L".tif" || extLower == L".tiff")   return L"TIFF";
    if (extLower == L".webp")                          return L"WebP";
    if (extLower == L".avif")                          return L"AVIF";
    if (extLower == L".jxl")                            return L"JPEG XL";
    return L"Unknown";
}

// ---------------------------------------------------------------------
// Raw PNG text-chunk reader (tEXt / iTXt).
//
// WIC's PNG metadata query reader only surfaces a tEXt/iTXt chunk if you
// query it by its exact keyword, and there's no fixed list of keywords
// to try - AI generation tools (Stable Diffusion, ComfyUI, Automatic1111)
// embed their data under arbitrary keywords like "prompt", "workflow", or
// "parameters" that aren't part of any standard set. Parsing the PNG
// chunk stream directly finds every text chunk regardless of keyword.
// ---------------------------------------------------------------------
struct PngTextField
{
    std::wstring keyword;
    std::wstring value;
};

static std::wstring Latin1BytesToWide(const char* data, size_t len)
{
    // Each Latin-1 byte maps 1:1 to the Unicode code point of the same
    // value, so this widen is exact - no conversion table needed.
    std::wstring w;
    w.reserve(len);
    for (size_t i = 0; i < len; ++i)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(data[i])));
    return w;
}

static std::wstring Utf8BytesToWide(const char* data, size_t len)
{
    if (len == 0) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), &w[0], wlen);
    return w;
}

static bool ReadPngTextChunks(const std::wstring& path, std::vector<PngTextField>& out)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER sizeLI = {};
    if (!GetFileSizeEx(hFile, &sizeLI) || sizeLI.QuadPart <= 0)
    {
        CloseHandle(hFile);
        return false;
    }

    // Metadata chunks are conventionally written right after IHDR, well
    // before the (often large) IDAT pixel data, so capping how much we
    // read keeps this cheap even on big files without missing anything
    // in practice. 8 MB comfortably covers even large embedded workflow
    // JSON blobs from tools like ComfyUI.
    constexpr DWORD kMaxRead = 8 * 1024 * 1024;
    DWORD toRead = static_cast<DWORD>(min(sizeLI.QuadPart, (LONGLONG)kMaxRead));

    std::vector<BYTE> buf(toRead);
    DWORD bytesRead = 0;
    bool ok = ReadFile(hFile, buf.data(), toRead, &bytesRead, nullptr) != 0;
    CloseHandle(hFile);
    if (!ok || bytesRead < 8)
        return false;

    static const BYTE kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (memcmp(buf.data(), kSig, 8) != 0)
        return false;

    auto readBE32 = [&](size_t at) -> UINT32
    {
        return (UINT32(buf[at]) << 24) | (UINT32(buf[at + 1]) << 16) |
               (UINT32(buf[at + 2]) << 8) | UINT32(buf[at + 3]);
    };

    size_t pos = 8;
    while (pos + 8 <= bytesRead)
    {
        UINT32 len = readBE32(pos);
        char type[5] = {};
        memcpy(type, &buf[pos + 4], 4);
        size_t dataStart = pos + 8;

        if (dataStart + len > bytesRead)
            break;  // chunk runs past what we read - stop rather than misread

        if (strcmp(type, "IEND") == 0)
            break;

        const char* data = reinterpret_cast<const char*>(&buf[dataStart]);

        if (strcmp(type, "tEXt") == 0)
        {
            // keyword \0 text - both Latin-1
            size_t nul = 0;
            while (nul < len && data[nul] != '\0') ++nul;
            if (nul < len)
            {
                PngTextField f;
                f.keyword = Latin1BytesToWide(data, nul);
                f.value   = Latin1BytesToWide(data + nul + 1, len - nul - 1);
                out.push_back(std::move(f));
            }
        }
        else if (strcmp(type, "iTXt") == 0)
        {
            // keyword \0 compressionFlag(1) compressionMethod(1) languageTag \0 translatedKeyword \0 text
            size_t nul1 = 0;
            while (nul1 < len && data[nul1] != '\0') ++nul1;
            if (nul1 + 2 < len)
            {
                unsigned char compressionFlag = static_cast<unsigned char>(data[nul1 + 1]);
                size_t i = nul1 + 3;
                size_t nul2 = i;
                while (nul2 < len && data[nul2] != '\0') ++nul2;        // language tag
                size_t nul3 = (nul2 < len) ? nul2 + 1 : nul2;
                while (nul3 < len && data[nul3] != '\0') ++nul3;        // translated keyword
                size_t textStart = (nul3 < len) ? nul3 + 1 : len;

                // Compressed iTXt (compressionFlag == 1) is skipped - the
                // tools that embed generation metadata this way virtually
                // always leave it uncompressed.
                if (compressionFlag == 0 && textStart <= len)
                {
                    PngTextField f;
                    f.keyword = Latin1BytesToWide(data, nul1);
                    f.value   = Utf8BytesToWide(data + textStart, len - textStart);
                    out.push_back(std::move(f));
                }
            }
        }
        // zTXt (zlib-compressed tEXt) intentionally skipped - no zlib
        // dependency in this project to decompress it.

        pos = dataStart + len + 4;  // + 4-byte CRC
    }

    return !out.empty();
}

// ---------------------------------------------------------------------
// Raw WebP XMP-chunk reader.
//
// WIC's WebP decoder doesn't surface the "XMP " RIFF chunk through its
// metadata query reader at all, so any XMP payload - hand-edited, or
// written by a tool that stashes arbitrary fields there the same way
// Stable Diffusion / ComfyUI stash them in PNG tEXt chunks - is otherwise
// invisible. This walks the RIFF chunk list directly, grabs the "XMP "
// chunk's XML payload, and pulls out every child element of every
// rdf:Description block regardless of namespace prefix or tag name - the
// same "don't assume a fixed keyword list" approach as the PNG reader
// above.
// ---------------------------------------------------------------------
static std::wstring DecodeXmlEntities(const std::wstring& in)
{
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); )
    {
        if (in[i] == L'&')
        {
            size_t semi = in.find(L';', i);
            if (semi != std::wstring::npos && semi - i <= 10)
            {
                std::wstring ent = in.substr(i + 1, semi - i - 1);
                if (ent == L"amp")  { out += L'&';  i = semi + 1; continue; }
                if (ent == L"lt")   { out += L'<';  i = semi + 1; continue; }
                if (ent == L"gt")   { out += L'>';  i = semi + 1; continue; }
                if (ent == L"quot") { out += L'"';  i = semi + 1; continue; }
                if (ent == L"apos") { out += L'\''; i = semi + 1; continue; }
                if (ent.size() > 1 && ent[0] == L'#')
                {
                    bool hex = ent.size() > 2 && (ent[1] == L'x' || ent[1] == L'X');
                    wchar_t* end = nullptr;
                    long code = wcstol(ent.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
                    if (code > 0)
                    {
                        out += static_cast<wchar_t>(code);
                        i = semi + 1;
                        continue;
                    }
                }
            }
        }
        out += in[i++];
    }
    return out;
}

// Strips nested tags (e.g. the rdf:Alt/rdf:li wrapper some tools put
// around simple text values) so only the visible text remains.
static std::wstring StripXmlTags(const std::wstring& in)
{
    std::wstring out;
    out.reserve(in.size());
    bool inTag = false;
    for (wchar_t c : in)
    {
        if (c == L'<')      { inTag = true;  continue; }
        else if (c == L'>') { inTag = false; continue; }
        else if (!inTag)    out += c;
    }
    return out;
}

static std::wstring TrimWhitespace(const std::wstring& in)
{
    size_t start = in.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = in.find_last_not_of(L" \t\r\n");
    return in.substr(start, end - start + 1);
}

// Finds the end of an XML opening tag (the '>' that isn't inside a quoted
// attribute value). Returns the index of that '>', or npos.
static size_t FindTagEnd(const std::wstring& xml, size_t from, size_t limit)
{
    bool inQuote = false;
    wchar_t quoteChar = 0;
    for (size_t i = from; i < limit; ++i)
    {
        wchar_t c = xml[i];
        if (inQuote) { if (c == quoteChar) inQuote = false; }
        else if (c == L'"' || c == L'\'') { inQuote = true; quoteChar = c; }
        else if (c == L'>') return i;
    }
    return std::wstring::npos;
}

static bool ReadWebpXmpFields(const std::wstring& path, std::vector<PngTextField>& out)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER sizeLI = {};
    if (!GetFileSizeEx(hFile, &sizeLI) || sizeLI.QuadPart <= 0)
    {
        CloseHandle(hFile);
        return false;
    }

    // XMP payloads carrying large JSON blobs (ComfyUI workflows etc.) can be
    // sizeable; 16 MB comfortably covers them without reading huge amounts
    // of trailing pixel data unnecessarily.
    constexpr DWORD kMaxRead = 16 * 1024 * 1024;
    DWORD toRead = static_cast<DWORD>(min(sizeLI.QuadPart, (LONGLONG)kMaxRead));

    std::vector<BYTE> buf(toRead);
    DWORD bytesRead = 0;
    bool ok = ReadFile(hFile, buf.data(), toRead, &bytesRead, nullptr) != 0;
    CloseHandle(hFile);
    if (!ok || bytesRead < 12)
        return false;

    if (memcmp(&buf[0], "RIFF", 4) != 0 || memcmp(&buf[8], "WEBP", 4) != 0)
        return false;

    auto readLE32 = [&](size_t at) -> UINT32
    {
        return UINT32(buf[at]) | (UINT32(buf[at + 1]) << 8) |
               (UINT32(buf[at + 2]) << 16) | (UINT32(buf[at + 3]) << 24);
    };

    // Walk the RIFF chunk list looking for "XMP " (the standard WebP
    // metadata FourCC for an embedded XMP packet).
    std::string xmpUtf8;
    size_t pos = 12;
    while (pos + 8 <= bytesRead)
    {
        UINT32 chunkLen = readLE32(pos + 4);
        size_t dataStart = pos + 8;

        if (dataStart + chunkLen > bytesRead)
            break;  // chunk runs past what we read - stop rather than misread

        if (memcmp(&buf[pos], "XMP ", 4) == 0)
        {
            xmpUtf8.assign(reinterpret_cast<const char*>(&buf[dataStart]), chunkLen);
            break;
        }

        pos = dataStart + chunkLen + (chunkLen & 1);  // chunks are word-aligned
    }

    if (xmpUtf8.empty())
        return false;

    std::wstring xmp = Utf8BytesToWide(xmpUtf8.data(), xmpUtf8.size());

    // Pull out every child element of every rdf:Description block. (A
    // document can legally have more than one, so keep scanning instead of
    // stopping at the first.)
    size_t searchFrom = 0;
    while (true)
    {
        size_t descStart = xmp.find(L"<rdf:Description", searchFrom);
        if (descStart == std::wstring::npos)
            break;

        size_t openEnd = FindTagEnd(xmp, descStart, xmp.size());
        if (openEnd == std::wstring::npos)
            break;

        bool selfClosing = (openEnd > 0 && xmp[openEnd - 1] == L'/');
        size_t contentStart = openEnd + 1;

        size_t descEnd = xmp.find(L"</rdf:Description>", contentStart);
        if (descEnd == std::wstring::npos)
            descEnd = xmp.size();

        if (!selfClosing)
        {
            size_t p = contentStart;
            while (p < descEnd)
            {
                size_t tagOpen = xmp.find(L'<', p);
                if (tagOpen == std::wstring::npos || tagOpen >= descEnd)
                    break;

                if (xmp[tagOpen + 1] == L'/')  // stray/unexpected close tag
                {
                    p = tagOpen + 1;
                    continue;
                }

                size_t nameEnd = tagOpen + 1;
                while (nameEnd < descEnd && !iswspace(xmp[nameEnd]) &&
                       xmp[nameEnd] != L'>' && xmp[nameEnd] != L'/')
                    ++nameEnd;
                std::wstring tagName = xmp.substr(tagOpen + 1, nameEnd - tagOpen - 1);

                size_t childOpenEnd = FindTagEnd(xmp, nameEnd, descEnd);
                if (childOpenEnd == std::wstring::npos)
                    break;

                bool childSelfClosing = (childOpenEnd > 0 && xmp[childOpenEnd - 1] == L'/');
                size_t innerStart = childOpenEnd + 1;

                if (childSelfClosing || tagName.empty())
                {
                    p = innerStart;
                    continue;
                }

                std::wstring closeTag = L"</" + tagName + L">";
                size_t innerEnd = xmp.find(closeTag, innerStart);
                if (innerEnd == std::wstring::npos || innerEnd > descEnd)
                {
                    p = innerStart;
                    continue;
                }

                std::wstring rawValue = xmp.substr(innerStart, innerEnd - innerStart);
                std::wstring value = TrimWhitespace(DecodeXmlEntities(StripXmlTags(rawValue)));

                if (!value.empty())
                {
                    size_t colon = tagName.find(L':');
                    std::wstring keyword = (colon != std::wstring::npos) ? tagName.substr(colon + 1) : tagName;

                    PngTextField f;
                    f.keyword = keyword;
                    f.value   = value;
                    out.push_back(std::move(f));
                }

                p = innerEnd + closeTag.size();
            }
        }

        searchFrom = descEnd + 1;
    }

    return !out.empty();
}


// Reads JPEG XL container metadata boxes through libjxl. JXL stores Exif in an
// "Exif" box (with a 4-byte big-endian TIFF-header offset) and XMP in an
// "xml " box. Using libjxl for the box/container layer keeps the existing WIC
// TIFF metadata helpers reusable for Exif, just as the AVIF path above does.
//
// NOTE on the Exif offset prefix: per the JXL/HEIF box spec, an "Exif" box's
// payload begins with a 4-byte big-endian offset to the actual TIFF header
// (normally 0, to allow a few bytes of padding before the TIFF data). Some
// tools that hand-edit or "manually insert" Exif into a JXL don't know about
// this prefix and just write a bare TIFF blob starting at byte 0. If we blindly
// trust the first 4 bytes as an offset in that case, we end up reading the
// TIFF header's own magic bytes ("II*\0" / "MM\0*") as a bogus offset, which is
// always way out of bounds - so the whole box gets silently discarded even
// though perfectly good Exif data is sitting right there. To guard against
// that, we sanity-check that the offset actually lands on a valid TIFF header
// before trusting it, and fall back to treating the box as an unprefixed TIFF
// blob otherwise.
static bool ReadJxlMetadataBoxes(
    const std::wstring& path,
    std::vector<BYTE>* outExifTiff,
    std::vector<BYTE>* outXmp)
{
    if (!outExifTiff || !outXmp)
        return false;

    outExifTiff->clear();
    outXmp->clear();

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER sizeLI = {};
    if (!GetFileSizeEx(hFile, &sizeLI) || sizeLI.QuadPart <= 0 ||
        sizeLI.QuadPart > static_cast<LONGLONG>(SIZE_MAX))
    {
        CloseHandle(hFile);
        return false;
    }

    const size_t fileSize = static_cast<size_t>(sizeLI.QuadPart);
    std::vector<uint8_t> fileData(fileSize);
    size_t totalRead = 0;
    while (totalRead < fileSize)
    {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(fileSize - totalRead, 16 * 1024 * 1024));
        DWORD got = 0;
        if (!ReadFile(hFile, fileData.data() + totalRead, chunk, &got, nullptr) || got == 0)
        {
            CloseHandle(hFile);
            return false;
        }
        totalRead += got;
    }
    CloseHandle(hFile);

    JxlDecoder* dec = JxlDecoderCreate(nullptr);
    if (!dec)
        return false;

    bool ok = false;
    std::vector<uint8_t> boxBuffer;

    // Returns true if `data` at `at` looks like the start of a TIFF header
    // ("II*\0" little-endian or "MM\0*" big-endian).
    auto looksLikeTiffHeader = [](const std::vector<uint8_t>& data, size_t at) -> bool
    {
        if (at + 4 > data.size())
            return false;
        const bool little = data[at] == 'I' && data[at + 1] == 'I' &&
                             data[at + 2] == 0x2A && data[at + 3] == 0x00;
        const bool big    = data[at] == 'M' && data[at + 1] == 'M' &&
                             data[at + 2] == 0x00 && data[at + 3] == 0x2A;
        return little || big;
    };

    do
    {
        if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BOX) != JXL_DEC_SUCCESS)
            break;
        if (JxlDecoderSetDecompressBoxes(dec, JXL_TRUE) != JXL_DEC_SUCCESS)
            break;
        if (JxlDecoderSetInput(dec, fileData.data(), fileData.size()) != JXL_DEC_SUCCESS)
            break;
        JxlDecoderCloseInput(dec);

        for (;;)
        {
            JxlDecoderStatus status = JxlDecoderProcessInput(dec);
            if (status == JXL_DEC_ERROR || status == JXL_DEC_NEED_MORE_INPUT)
                break;
            if (status == JXL_DEC_SUCCESS)
            {
                ok = true;
                break;
            }

            if (status != JXL_DEC_BOX)
                continue;

            JxlBoxType type = {};
            if (JxlDecoderGetBoxType(dec, type, JXL_TRUE) != JXL_DEC_SUCCESS)
                break;

            const bool isExif = memcmp(type, "Exif", 4) == 0;
            const bool isXmp  = memcmp(type, "xml ", 4) == 0;
            if (!isExif && !isXmp)
                continue; // Leave non-metadata boxes unbuffered.

            // Compressed "brob" boxes have an unknown decompressed size, so
            // start with a modest buffer and grow it when libjxl asks for more.
            boxBuffer.assign(64 * 1024, 0);
            size_t bytesWritten = 0;
            bool boxComplete = false;

            for (;;)
            {
                if (JxlDecoderSetBoxBuffer(dec, boxBuffer.data() + bytesWritten,
                                            boxBuffer.size() - bytesWritten) != JXL_DEC_SUCCESS)
                    break;

                JxlDecoderStatus bs = JxlDecoderProcessInput(dec);
                size_t remaining = JxlDecoderReleaseBoxBuffer(dec);
                size_t capacity = boxBuffer.size() - bytesWritten;
                size_t wrote = capacity - remaining;
                bytesWritten += wrote;

                if (bs == JXL_DEC_BOX_NEED_MORE_OUTPUT)
                {
                    if (boxBuffer.size() >= 64 * 1024 * 1024)
                        break;
                    size_t newSize = std::min<size_t>(boxBuffer.size() * 2, 64 * 1024 * 1024);
                    boxBuffer.resize(newSize);
                    continue;
                }

                if (bs == JXL_DEC_BOX || bs == JXL_DEC_SUCCESS)
                {
                    boxComplete = true;
                }
                break;
            }

            if (!boxComplete)
                break;

            boxBuffer.resize(bytesWritten);

            if (isExif)
            {
                // Normal case: first 4 bytes are a big-endian offset to the
                // TIFF header (spec-compliant writers usually use 0).
                bool handled = false;
                if (boxBuffer.size() >= 4)
                {
                    uint32_t offset = (uint32_t(boxBuffer[0]) << 24) |
                                      (uint32_t(boxBuffer[1]) << 16) |
                                      (uint32_t(boxBuffer[2]) << 8)  |
                                      uint32_t(boxBuffer[3]);
                    if (offset <= boxBuffer.size() - 4)
                    {
                        size_t tiffStart = 4 + static_cast<size_t>(offset);
                        if (tiffStart < boxBuffer.size() &&
                            looksLikeTiffHeader(boxBuffer, tiffStart))
                        {
                            outExifTiff->assign(boxBuffer.begin() + tiffStart, boxBuffer.end());
                            handled = true;
                        }
                    }
                }

                // Fallback: some hand-edited/manually-inserted Exif boxes skip
                // the offset prefix entirely and start straight with the TIFF
                // header. Detect that directly rather than discarding the box.
                if (!handled && looksLikeTiffHeader(boxBuffer, 0))
                {
                    *outExifTiff = boxBuffer;
                }
            }
            else if (isXmp)
            {
                *outXmp = std::move(boxBuffer);
            }

            if (!outExifTiff->empty() && !outXmp->empty())
                break;
        }
    }
    while (false);

    JxlDecoderDestroy(dec);
    return ok || !outExifTiff->empty() || !outXmp->empty();
}

static HRESULT OpenJxlExifMetadataReader(
    const wchar_t* path,
    IWICImagingFactory* wicFactory,
    ComPtr<IWICBitmapDecoder>* outDecoder,
    ComPtr<IWICBitmapFrameDecode>* outFrame,
    ComPtr<IWICMetadataQueryReader>* outReader)
{
    if (!path || !wicFactory || !outDecoder || !outFrame || !outReader)
        return E_INVALIDARG;

    outDecoder->Reset();
    outFrame->Reset();
    outReader->Reset();

    std::vector<BYTE> exifTiff;
    std::vector<BYTE> xmpUnused;
    if (!ReadJxlMetadataBoxes(path, &exifTiff, &xmpUnused) || exifTiff.empty())
        return S_FALSE;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, exifTiff.size());
    if (!hMem)
        return E_OUTOFMEMORY;

    void* dst = GlobalLock(hMem);
    if (!dst)
    {
        GlobalFree(hMem);
        return E_OUTOFMEMORY;
    }
    memcpy(dst, exifTiff.data(), exifTiff.size());
    GlobalUnlock(hMem);

    IStream* rawStream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &rawStream);
    if (FAILED(hr))
    {
        GlobalFree(hMem);
        return hr;
    }

    ComPtr<IStream> owningStream;
    owningStream.Attach(rawStream);

    hr = wicFactory->CreateDecoderFromStream(
        owningStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
        outDecoder->GetAddressOf());
    if (FAILED(hr))
        return hr;

    hr = (*outDecoder)->GetFrame(0, outFrame->GetAddressOf());
    if (FAILED(hr))
        return hr;

    hr = (*outFrame)->GetMetadataQueryReader(outReader->GetAddressOf());
    return hr;
}

static bool ReadJxlXmpFields(const std::wstring& path, std::vector<PngTextField>& out)
{
    std::vector<BYTE> exifUnused;
    std::vector<BYTE> xmp;
    if (!ReadJxlMetadataBoxes(path, &exifUnused, &xmp) || xmp.empty())
        return false;

    std::wstring xmpText = Utf8BytesToWide(
        reinterpret_cast<const char*>(xmp.data()), xmp.size());
    if (xmpText.empty())
        return false;

    // Same extraction strategy as the AVIF/WebP readers: enumerate every
    // child of every rdf:Description block and keep arbitrary keywords.
    size_t searchFrom = 0;
    while (true)
    {
        size_t descStart = xmpText.find(L"<rdf:Description", searchFrom);
        if (descStart == std::wstring::npos)
            break;

        size_t openEnd = FindTagEnd(xmpText, descStart, xmpText.size());
        if (openEnd == std::wstring::npos)
            break;

        bool selfClosing = openEnd > 0 && xmpText[openEnd - 1] == L'/';
        size_t contentStart = openEnd + 1;
        size_t descEnd = xmpText.find(L"</rdf:Description>", contentStart);
        if (descEnd == std::wstring::npos)
            descEnd = xmpText.size();

        if (!selfClosing)
        {
            size_t p = contentStart;
            while (p < descEnd)
            {
                size_t tagOpen = xmpText.find(L'<', p);
                if (tagOpen == std::wstring::npos || tagOpen >= descEnd)
                    break;
                if (tagOpen + 1 >= descEnd || xmpText[tagOpen + 1] == L'/')
                {
                    p = tagOpen + 1;
                    continue;
                }

                size_t nameEnd = tagOpen + 1;
                while (nameEnd < descEnd && !iswspace(xmpText[nameEnd]) &&
                       xmpText[nameEnd] != L'>' && xmpText[nameEnd] != L'/')
                    ++nameEnd;

                std::wstring tagName = xmpText.substr(
                    tagOpen + 1, nameEnd - tagOpen - 1);
                size_t childOpenEnd = FindTagEnd(xmpText, nameEnd, descEnd);
                if (childOpenEnd == std::wstring::npos)
                    break;

                bool childSelfClosing = childOpenEnd > 0 &&
                                        xmpText[childOpenEnd - 1] == L'/';
                size_t innerStart = childOpenEnd + 1;
                if (childSelfClosing || tagName.empty())
                {
                    p = innerStart;
                    continue;
                }

                std::wstring closeTag = L"</" + tagName + L">";
                size_t innerEnd = xmpText.find(closeTag, innerStart);
                if (innerEnd == std::wstring::npos || innerEnd > descEnd)
                {
                    p = innerStart;
                    continue;
                }

                std::wstring value = TrimWhitespace(DecodeXmlEntities(
                    StripXmlTags(xmpText.substr(innerStart, innerEnd - innerStart))));
                if (!value.empty())
                {
                    size_t colon = tagName.find(L':');
                    PngTextField f;
                    f.keyword = (colon != std::wstring::npos)
                        ? tagName.substr(colon + 1) : tagName;
                    f.value = value;
                    out.push_back(std::move(f));
                }
                p = innerEnd + closeTag.size();
            }
        }
        searchFrom = descEnd + 1;
    }

    return !out.empty();
}

// Opens the Exif payload carried by an AVIF as a WIC TIFF metadata reader.
// libavif exposes Exif as a TIFF/Exif byte sequence; feeding that sequence
// to WIC's TIFF decoder lets the existing metadata helpers below handle AVIF
// without maintaining a second Exif parser.
static HRESULT OpenAvifExifMetadataReader(
    const wchar_t* path,
    IWICImagingFactory* wicFactory,
    ComPtr<IWICBitmapDecoder>* outDecoder,
    ComPtr<IWICBitmapFrameDecode>* outFrame,
    ComPtr<IWICMetadataQueryReader>* outReader)
{
    if (!path || !wicFactory || !outDecoder || !outFrame || !outReader)
        return E_INVALIDARG;

    outDecoder->Reset();
    outFrame->Reset();
    outReader->Reset();

    avifDecoder* avif = AvifOpenAndParse(path);
    if (!avif)
        return E_FAIL;

    HRESULT hr = S_FALSE;

    do
    {
        if (!avif->image || avif->image->exif.size == 0)
            break;

        size_t tiffOffset = 0;
        if (avifGetExifTiffHeaderOffset(
                avif->image->exif.data, avif->image->exif.size, &tiffOffset)
                != AVIF_RESULT_OK ||
            tiffOffset >= avif->image->exif.size)
            break;

        SIZE_T tiffSize = avif->image->exif.size - tiffOffset;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, tiffSize);
        if (!hMem)
        {
            hr = E_OUTOFMEMORY;
            break;
        }

        void* dst = GlobalLock(hMem);
        if (!dst)
        {
            GlobalFree(hMem);
            hr = E_OUTOFMEMORY;
            break;
        }

        memcpy(dst, avif->image->exif.data + tiffOffset, tiffSize);
        GlobalUnlock(hMem);

        // CreateStreamOnHGlobal takes ownership when fDeleteOnRelease is TRUE.
        IStream* rawStream = nullptr;
        hr = CreateStreamOnHGlobal(hMem, TRUE, &rawStream);
        if (FAILED(hr))
        {
            GlobalFree(hMem);
            break;
        }

        // Keep the HGLOBAL alive while WIC opens and caches the metadata.
        ComPtr<IStream> owningStream;
        owningStream.Attach(rawStream);

        hr = wicFactory->CreateDecoderFromStream(
            owningStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
            outDecoder->GetAddressOf());
        if (FAILED(hr))
            break;

        hr = (*outDecoder)->GetFrame(0, outFrame->GetAddressOf());
        if (FAILED(hr))
            break;

        hr = (*outFrame)->GetMetadataQueryReader(outReader->GetAddressOf());
        if (FAILED(hr))
            break;

        // The decoder was created with WICDecodeMetadataCacheOnLoad, so the
        // metadata reader has already consumed the stream. No need to return
        // the IStream itself.
        hr = S_OK;
    }
    while (false);

    avifDecoderDestroy(avif);
    return hr;
}

// AVIF XMP is exposed by libavif as a raw UTF-8 XMP packet. Reuse the same
// XML helper routines used by the WebP reader rather than adding an AVIF
// container-specific XML parser.
static bool ReadAvifXmpFields(const std::wstring& path, std::vector<PngTextField>& out)
{
    avifDecoder* avif = AvifOpenAndParse(path.c_str());
    if (!avif)
        return false;

    bool ok = false;
    if (avif->image && avif->image->xmp.size)
    {
        const char* data =
            reinterpret_cast<const char*>(avif->image->xmp.data);
        std::wstring xmp = Utf8BytesToWide(data, avif->image->xmp.size);

        // Same extraction strategy as ReadWebpXmpFields: enumerate every
        // child of every rdf:Description block and keep arbitrary keywords.
        size_t searchFrom = 0;
        while (true)
        {
            size_t descStart = xmp.find(L"<rdf:Description", searchFrom);
            if (descStart == std::wstring::npos)
                break;

            size_t openEnd = FindTagEnd(xmp, descStart, xmp.size());
            if (openEnd == std::wstring::npos)
                break;

            bool selfClosing = openEnd > 0 && xmp[openEnd - 1] == L'/';
            size_t contentStart = openEnd + 1;
            size_t descEnd = xmp.find(L"</rdf:Description>", contentStart);
            if (descEnd == std::wstring::npos)
                descEnd = xmp.size();

            if (!selfClosing)
            {
                size_t p = contentStart;
                while (p < descEnd)
                {
                    size_t tagOpen = xmp.find(L'<', p);
                    if (tagOpen == std::wstring::npos || tagOpen >= descEnd)
                        break;

                    if (tagOpen + 1 >= descEnd || xmp[tagOpen + 1] == L'/')
                    {
                        p = tagOpen + 1;
                        continue;
                    }

                    size_t nameEnd = tagOpen + 1;
                    while (nameEnd < descEnd && !iswspace(xmp[nameEnd]) &&
                           xmp[nameEnd] != L'>' && xmp[nameEnd] != L'/')
                        ++nameEnd;

                    std::wstring tagName =
                        xmp.substr(tagOpen + 1, nameEnd - tagOpen - 1);

                    size_t childOpenEnd = FindTagEnd(xmp, nameEnd, descEnd);
                    if (childOpenEnd == std::wstring::npos)
                        break;

                    bool childSelfClosing =
                        childOpenEnd > 0 && xmp[childOpenEnd - 1] == L'/';
                    size_t innerStart = childOpenEnd + 1;

                    if (childSelfClosing || tagName.empty())
                    {
                        p = innerStart;
                        continue;
                    }

                    std::wstring closeTag = L"</" + tagName + L">";
                    size_t innerEnd = xmp.find(closeTag, innerStart);
                    if (innerEnd == std::wstring::npos || innerEnd > descEnd)
                    {
                        p = innerStart;
                        continue;
                    }

                    std::wstring value = TrimWhitespace(
                        DecodeXmlEntities(
                            StripXmlTags(xmp.substr(
                                innerStart, innerEnd - innerStart))));

                    if (!value.empty())
                    {
                        size_t colon = tagName.find(L':');
                        PngTextField f;
                        f.keyword = (colon != std::wstring::npos)
                            ? tagName.substr(colon + 1)
                            : tagName;
                        f.value = value;
                        out.push_back(std::move(f));
                    }

                    p = innerEnd + closeTag.size();
                }
            }

            searchFrom = descEnd + 1;
        }

        ok = !out.empty();
    }

    avifDecoderDestroy(avif);
    return ok;
}

void ShowImageMetadata(HWND hWnd)
{
    if (g_image.path.empty() || !g_devices.wicFactory)
        return;

    std::wstring extLower = g_image.path.substr(g_image.path.rfind(L'.'));
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);

    std::wstring body;
    bool foundAny = false;

    // PNG text chunks - parsed directly from the file so arbitrary keywords
    // (Stable Diffusion / ComfyUI's "prompt"/"workflow", Automatic1111's
    // "parameters", etc.) are found regardless of what they're named.
    if (extLower == L".png")
    {
        std::vector<PngTextField> fields;
        if (ReadPngTextChunks(g_image.path, fields))
        {
            // Shown in a scrollable, selectable edit control (see
            // ShowMetadataViewerWindow below), so there's no need to
            // truncate long fields like Stable Diffusion / ComfyUI
            // "prompt"/"workflow" text - show it in full.
            for (auto& f : fields)
            {
                body += f.keyword + L":\n" + f.value + L"\n\n";
                foundAny = true;
            }
        }
    }

    // WebP XMP chunk - same idea as the PNG text chunks above, but WIC's
    // WebP decoder doesn't expose "XMP " through its metadata query reader
    // at all, so this is parsed straight from the RIFF chunk list.
    if (extLower == L".webp")
    {
        std::vector<PngTextField> fields;
        if (ReadWebpXmpFields(g_image.path, fields))
        {
            for (auto& f : fields)
            {
                body += f.keyword + L":\n" + f.value + L"\n\n";
                foundAny = true;
            }
        }
    }

    // EXIF / TIFF-style tags via WIC - present on JPEG and TIFF files from
    // cameras, and occasionally on PNG (eXIf chunk) or WebP (EXIF chunk)
    // files exported from editing tools. JPEG exposes these under
    // "/app1/ifd/..." (the APP1 marker), but PNG/WebP containers expose the
    // same tags under "/ifd/..." instead - no APP1 marker, since that's a
    // JPEG-specific concept - so both roots are tried for every tag.
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> reader;
    HRESULT hr = S_OK;

    if (extLower == L".avif")
    {
        hr = OpenAvifExifMetadataReader(
            g_image.path.c_str(), g_devices.wicFactory.Get(),
            &decoder, &frame, &reader);

        std::vector<PngTextField> xmpFields;
        if (ReadAvifXmpFields(g_image.path, xmpFields))
        {
            for (auto& f : xmpFields)
            {
                body += f.keyword + L":\n" + f.value + L"\n\n";
                foundAny = true;
            }
        }
    }
    else if (extLower == L".jxl")
    {
        hr = OpenJxlExifMetadataReader(
            g_image.path.c_str(), g_devices.wicFactory.Get(),
            &decoder, &frame, &reader);

        std::vector<PngTextField> xmpFields;
        if (ReadJxlXmpFields(g_image.path, xmpFields))
        {
            for (auto& f : xmpFields)
            {
                body += f.keyword + L":\n" + f.value + L"\n\n";
                foundAny = true;
            }
        }
    }
    else
    {
        hr = g_devices.wicFactory->CreateDecoderFromFilename(
            g_image.path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder);

        if (SUCCEEDED(hr))
            hr = decoder->GetFrame(0, &frame);

        if (SUCCEEDED(hr))
            hr = frame->GetMetadataQueryReader(&reader);
    }

    if (SUCCEEDED(hr) && reader)
    {
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=271}",        L"Camera make",   body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=271}",             L"Camera make",   body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=272}",        L"Camera model",  body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=272}",             L"Camera model",  body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/exif/{ushort=36867}", L"Date taken",    body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/exif/{ushort=36867}",      L"Date taken",    body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=306}",        L"Date modified", body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=306}",             L"Date modified", body);
        foundAny |= AppendRationalField(reader.Get(), L"/app1/ifd/exif/{ushort=33434}", L"Exposure time", L"%.3g s", body, /*asShutterFraction=*/true)
                 || AppendRationalField(reader.Get(), L"/ifd/exif/{ushort=33434}",      L"Exposure time", L"%.3g s", body, /*asShutterFraction=*/true);
        foundAny |= AppendRationalField(reader.Get(), L"/app1/ifd/exif/{ushort=33437}", L"F-number",      L"f/%.1f", body)
                 || AppendRationalField(reader.Get(), L"/ifd/exif/{ushort=33437}",      L"F-number",      L"f/%.1f", body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/exif/{ushort=34855}", L"ISO speed",     body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/exif/{ushort=34855}",      L"ISO speed",     body);
        foundAny |= AppendRationalField(reader.Get(), L"/app1/ifd/exif/{ushort=37386}", L"Focal length",  L"%.1f mm", body)
                 || AppendRationalField(reader.Get(), L"/ifd/exif/{ushort=37386}",      L"Focal length",  L"%.1f mm", body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=305}",        L"Software",      body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=305}",             L"Software",      body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=315}",        L"Artist",        body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=315}",             L"Artist",        body);
        foundAny |= AppendSimpleField(reader.Get(),   L"/app1/ifd/{ushort=33432}",      L"Copyright",     body)
                 || AppendSimpleField(reader.Get(),   L"/ifd/{ushort=33432}",           L"Copyright",     body);
        foundAny |= AppendGpsCoordinate(reader.Get(), L"/app1/ifd/gps", body)
                 || AppendGpsCoordinate(reader.Get(), L"/ifd/gps",      body);
    }

    if (!foundAny)
    {
        TaskDialog(hWnd, g_app.instance, L"Image metadata", L"No metadata found",
            L"This image doesn't contain any readable embedded metadata.",
            TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
        return;
    }

    std::wstring windowTitle = L"Image metadata \u2014 " + g_image.fileName;
    ShowMetadataViewerWindow(hWnd, windowTitle, body);
}

// A resizable window with a read-only multiline edit control showing the
// full metadata text. Unlike TaskDialog (used previously), this supports
// mouse-wheel scrolling, text selection/copying, and doesn't truncate long
// content.
LRESULT CALLBACK MetadataViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HFONT  s_font      = nullptr;
    static HBRUSH s_editBrush = nullptr;
    static HBRUSH s_bkBrush   = nullptr;

    switch (message)
    {
    case WM_CREATE:
    {
        HWND hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_LEFT,
            0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(MetadataControlId::EDIT)),
            g_app.instance, nullptr);

        // Remove the ~30k-character default cap so long fields (e.g. full
        // Stable Diffusion / ComfyUI prompt+workflow text) display in full.
        SendMessageW(hEdit, EM_SETLIMITTEXT, 0, 0);

        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        {
            if (s_font) DeleteObject(s_font);
            s_font = CreateFontIndirectW(&ncm.lfMessageFont);
            SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(s_font), TRUE);
        }

        auto* cs   = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* body = reinterpret_cast<const std::wstring*>(cs->lpCreateParams);
        if (body)
            SetWindowTextW(hEdit, body->c_str());

        EnableDarkTitleBar(hWnd);
        break;
    }
    case WM_SIZE:
    {
        HWND hEdit = GetDlgItem(hWnd, static_cast<int>(MetadataControlId::EDIT));
        RECT rc;
        GetClientRect(hWnd, &rc);
        constexpr int margin = 8;
        MoveWindow(hEdit, margin, margin,
            (rc.right - rc.left) - margin * 2,
            (rc.bottom - rc.top) - margin * 2, TRUE);
        break;
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(225, 225, 225));
        SetBkColor(hdc, RGB(32, 32, 32));
        if (!s_editBrush)
            s_editBrush = CreateSolidBrush(RGB(32, 32, 32));
        return reinterpret_cast<LRESULT>(s_editBrush);
    }
    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (!s_bkBrush)
            s_bkBrush = CreateSolidBrush(RGB(24, 24, 24));
        FillRect(hdc, &rc, s_bkBrush);
        return 1;
    }
    case WM_SETFOCUS:
        SetFocus(GetDlgItem(hWnd, static_cast<int>(MetadataControlId::EDIT)));
        break;
    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 360;
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hWnd);
            return 0;
        }
        if (LOWORD(wParam) == static_cast<int>(MetadataControlId::SELECTALL))
        {
            SendMessageW(GetDlgItem(hWnd, static_cast<int>(MetadataControlId::EDIT)), EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (s_font)      { DeleteObject(s_font);      s_font      = nullptr; }
        if (s_editBrush) { DeleteObject(s_editBrush);  s_editBrush = nullptr; }
        if (s_bkBrush)   { DeleteObject(s_bkBrush);    s_bkBrush   = nullptr; }
        g_app.metadataViewerWnd = nullptr;
        break;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

ATOM RegisterMetadataViewerClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex   = {};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = MetadataViewerWndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PICASSOPICTURES));
    wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr; // painted manually in WM_ERASEBKGND for the dark theme
    wcex.lpszClassName = L"MetadataViewerClass";
    wcex.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&wcex);
}

// Shows the metadata text in a resizable, ownerdrawn-dark window with a
// read-only multiline edit control (mouse-wheel scroll, text selection,
// and Ctrl+C copying all work natively; no truncation). Runs its own
// message loop so it behaves like a modal dialog, matching the previous
// TaskDialog behavior.
void ShowMetadataViewerWindow(HWND owner, const std::wstring& title, const std::wstring& body)
{
    static bool s_classRegistered = false;
    if (!s_classRegistered)
    {
        RegisterMetadataViewerClass(g_app.instance);
        s_classRegistered = true;
    }

    if (g_app.metadataViewerWnd)
    {
        DestroyWindow(g_app.metadataViewerWnd);
        g_app.metadataViewerWnd = nullptr;
    }

    // Size relative to the monitor's work area so the window is roomy on
    // large displays instead of the small fixed size used previously.
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    const int workW = mi.rcWork.right  - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;

    int width  = static_cast<int>(workW * 0.75);
    int height = static_cast<int>(workH * 0.85);

    RECT ownerRc;
    GetWindowRect(owner, &ownerRc);
    const int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - width) / 2;
    const int y = ownerRc.top  + ((ownerRc.bottom - ownerRc.top) - height) / 2;

    g_app.metadataViewerWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"MetadataViewerClass", title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        x, y, width, height,
        owner, nullptr, g_app.instance, const_cast<std::wstring*>(&body));

    if (!g_app.metadataViewerWnd)
        return;

    ShowWindow(g_app.metadataViewerWnd, SW_SHOW);
    UpdateWindow(g_app.metadataViewerWnd);

    // Disable the owner and pump our own messages while the viewer is open
    // so it behaves like a modal dialog (same UX as the TaskDialog it
    // replaces), and so Escape / Ctrl+A work no matter which child has
    // focus.
    EnableWindow(owner, FALSE);

    ACCEL accels[] = {
        { FVIRTKEY,            VK_ESCAPE, IDCANCEL },
        { FVIRTKEY | FCONTROL, L'A',      static_cast<WORD>(MetadataControlId::SELECTALL) },
    };
    HACCEL hAccel = CreateAcceleratorTable(accels, ARRAYSIZE(accels));

    MSG msg;
    while (IsWindow(g_app.metadataViewerWnd) && GetMessage(&msg, nullptr, 0, 0))
    {
        if (hAccel && (msg.hwnd == g_app.metadataViewerWnd || IsChild(g_app.metadataViewerWnd, msg.hwnd)) &&
            TranslateAccelerator(g_app.metadataViewerWnd, hAccel, &msg))
            continue;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hAccel)
        DestroyAcceleratorTable(hAccel);

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// Plain data carried into PropertiesDlgProc via DialogBoxParamW's lParam.
// Every field is pre-formatted display text; fields that don't apply to
// the current image (e.g. "Date taken" on a screenshot) are left as an
// em dash rather than the row being hidden, so the dialog's layout is
// completely static - same spirit as IDD_COMMANDBOX.
struct ImagePropertiesFields
{
    std::wstring windowTitle;
    std::wstring fileName;
    std::wstring location;
    std::wstring size;
    std::wstring format;
    std::wstring dimensions;
    std::wstring bitDepth;
    std::wstring transparency;
    std::wstring resolution;
    std::wstring created;
    std::wstring modified;
    std::wstring dateTaken;
    std::wstring colorSpace;
    std::wstring orientation;
};

// Dialog proc for IDD_PROPERTIES - a bespoke, fixed-layout dialog template
// (built the same way as IDD_COMMANDBOX / "Keyboard shortcuts": static
// labels baked into the .rc, values filled in at WM_INITDIALOG) rather than
// the resizable MetadataViewerClass window used by "View metadata". Modal,
// like About() and the shortcuts box, so no manual message pump is needed.
INT_PTR CALLBACK PropertiesDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        const auto* f = reinterpret_cast<const ImagePropertiesFields*>(lParam);
        if (f)
        {
            SetWindowTextW(hDlg, f->windowTitle.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_FILENAME,     f->fileName.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_LOCATION,     f->location.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_SIZE,         f->size.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_FORMAT,       f->format.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_DIMENSIONS,   f->dimensions.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_BITDEPTH,     f->bitDepth.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_TRANSPARENCY, f->transparency.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_RESOLUTION,   f->resolution.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_CREATED,      f->created.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_MODIFIED,     f->modified.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_DATETAKEN,    f->dateTaken.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_COLORSPACE,   f->colorSpace.c_str());
            SetDlgItemTextW(hDlg, IDC_PROP_ORIENTATION,  f->orientation.c_str());
        }
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// Shows an in-app "Properties" dialog for the current image, built as its
// own DIALOGEX template (IDD_PROPERTIES) - a bespoke, fixed-size window in
// the same style as "Keyboard shortcuts", rather than reusing the
// resizable free-text viewer behind "View metadata". Covers the
// file-system-level facts Explorer's own Properties dialog leads with
// (name, location, size, timestamps) plus a handful of quick image-level
// facts (format, dimensions, bit depth, transparency, resolution, color
// space, orientation, date taken). Full embedded EXIF (camera/lens/GPS/
// software/etc.) is intentionally left to "View metadata" rather than
// duplicated here.
void ShowImageProperties(HWND hWnd)
{
    if (g_image.path.empty())
        return;

    std::wstring extLower = g_image.path.substr(g_image.path.rfind(L'.'));
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);

    const wchar_t* kNotAvailable = L"\u2014"; // em dash, shown for fields that don't apply
    ImagePropertiesFields f;

    // ---- File-system properties ----
    f.fileName = g_image.fileName;

    std::wstring location = g_image.path;
    size_t slash = location.find_last_of(L"\\/");
    f.location = (slash != std::wstring::npos) ? location.substr(0, slash) : L".";

    f.size = f.created = f.modified = kNotAvailable;
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    if (GetFileAttributesExW(g_image.path.c_str(), GetFileExInfoStandard, &attr))
    {
        ULARGE_INTEGER size;
        size.HighPart = attr.nFileSizeHigh;
        size.LowPart  = attr.nFileSizeLow;
        f.size     = FormatFileSizeW(size.QuadPart);
        f.created  = FormatFileTimeW(attr.ftCreationTime);
        f.modified = FormatFileTimeW(attr.ftLastWriteTime);
    }

    f.format = ImageFormatNameFromExtension(extLower);

    // ---- Image properties (from the already-decoded WIC source, so this
    // doesn't need to touch disk again) ----
    f.dimensions = f.bitDepth = f.transparency = f.resolution = kNotAvailable;

    if (g_image.width > 0 && g_image.height > 0)
    {
        wchar_t dimBuf[64];
        swprintf_s(dimBuf, L"%d \u00D7 %d pixels", g_image.width, g_image.height);
        f.dimensions = dimBuf;
    }

    if (g_image.source && g_devices.wicFactory)
    {
        WICPixelFormatGUID pf;
        if (SUCCEEDED(g_image.source->GetPixelFormat(&pf)))
        {
            ComPtr<IWICComponentInfo> compInfo;
            if (SUCCEEDED(g_devices.wicFactory->CreateComponentInfo(pf, &compInfo)))
            {
                ComPtr<IWICPixelFormatInfo2> pfInfo;
                if (SUCCEEDED(compInfo.As(&pfInfo)))
                {
                    UINT bpp = 0;
                    if (SUCCEEDED(pfInfo->GetBitsPerPixel(&bpp)) && bpp > 0)
                        f.bitDepth = std::to_wstring(bpp) + L" bits per pixel";

                    BOOL hasAlpha = FALSE;
                    if (SUCCEEDED(pfInfo->SupportsTransparency(&hasAlpha)))
                        f.transparency = hasAlpha ? L"Yes" : L"No";
                }
            }
        }

        double dpiX = 0.0, dpiY = 0.0;
        if (SUCCEEDED(g_image.source->GetResolution(&dpiX, &dpiY)) && dpiX > 0.0 && dpiY > 0.0)
        {
            wchar_t dpiBuf[64];
            if (std::lround(dpiX) == std::lround(dpiY))
                swprintf_s(dpiBuf, L"%.0f DPI", dpiX);
            else
                swprintf_s(dpiBuf, L"%.0f x %.0f DPI", dpiX, dpiY);
            f.resolution = dpiBuf;
        }
    }

    // These fields are read through the same metadata query helpers as the
    // main metadata viewer. AVIF and JXL expose their embedded Exif TIFF
    // payloads through WIC via their respective container readers.
    f.dateTaken = f.colorSpace = f.orientation = kNotAvailable;

    if (g_devices.wicFactory)
    {
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICMetadataQueryReader> reader;
        HRESULT hr = S_OK;

        if (extLower == L".avif")
        {
            hr = OpenAvifExifMetadataReader(
                g_image.path.c_str(), g_devices.wicFactory.Get(),
                &decoder, &frame, &reader);
        }
        else if (extLower == L".jxl")
        {
            hr = OpenJxlExifMetadataReader(
                g_image.path.c_str(), g_devices.wicFactory.Get(),
                &decoder, &frame, &reader);
        }
        else
        {
            hr = g_devices.wicFactory->CreateDecoderFromFilename(
                g_image.path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &decoder);

            if (SUCCEEDED(hr))
                hr = decoder->GetFrame(0, &frame);

            if (SUCCEEDED(hr))
                hr = frame->GetMetadataQueryReader(&reader);
        }

        if (SUCCEEDED(hr) && reader)
        {
            std::wstring val;
            if (ReadDateTakenValue(reader.Get(), L"/app1/ifd/exif/{ushort=36867}", val)
                || ReadDateTakenValue(reader.Get(), L"/ifd/exif/{ushort=36867}", val))
                f.dateTaken = val;

            val.clear();
            if (ReadColorSpaceValue(reader.Get(), L"/app1/ifd/exif/{ushort=40961}", val)
                || ReadColorSpaceValue(reader.Get(), L"/ifd/exif/{ushort=40961}", val))
                f.colorSpace = val;

            val.clear();
            if (ReadOrientationValue(reader.Get(), L"/app1/ifd/{ushort=274}", val)
                || ReadOrientationValue(reader.Get(), L"/ifd/{ushort=274}", val))
                f.orientation = val;
        }
    }

    f.windowTitle = L"Properties \u2014 " + g_image.fileName;

    // Owned by hWnd, so IsOwnedByOurWindow() in WM_ACTIVATE recognizes it
    // automatically - no manual fullscreen-exit suppression needed.
    DialogBoxParamW(g_app.instance, MAKEINTRESOURCE(IDD_PROPERTIES), hWnd, PropertiesDlgProc,
        reinterpret_cast<LPARAM>(&f));
}
