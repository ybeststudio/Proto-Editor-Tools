#include "TsvFile.h"
#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>
#include <cwchar>
#include <Windows.h>

namespace {
    std::wstring utf8ToWide(const std::string& utf8) {
        if (utf8.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring out(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], n);
        return out;
    }
    std::wstring cpToWide(const std::string& s, UINT cp) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring out(n, 0);
        MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), &out[0], n);
        return out;
    }
    std::string wideToCp(const std::wstring& w, UINT cp) {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "";
        std::string out(n, 0);
        WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr, nullptr);
        return out;
    }
    std::string wideToUtf8(const std::wstring& w) {
        return wideToCp(w, CP_UTF8);
    }
}

std::string TsvFile::detectEncoding(const std::vector<char>& raw) {
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        return "UTF-8-BOM";
    int invalid = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = raw[i];
        if (c >= 0x80) {
            if (c >= 0xC2 && c <= 0xF4 && i + 1 < raw.size()) {
                unsigned char c2 = raw[i + 1];
                if (c <= 0xDF && c2 >= 0x80 && c2 <= 0xBF) { i++; continue; }
                if (c >= 0xE0 && c <= 0xEF && i + 2 < raw.size()) {
                    unsigned char c3 = raw[i + 2];
                    if (c2 >= 0x80 && c2 <= 0xBF && c3 >= 0x80 && c3 <= 0xBF) { i += 2; continue; }
                }
                if (c >= 0xF0 && c <= 0xF4 && i + 3 < raw.size()) {
                    unsigned char c2 = raw[i+1], c3 = raw[i+2], c4 = raw[i+3];
                    if (c2 >= 0x80 && c2 <= 0xBF && c3 >= 0x80 && c3 <= 0xBF && c4 >= 0x80 && c4 <= 0xBF) { i += 3; continue; }
                }
            }
            invalid++;
        }
    }
    if (invalid == 0) return "UTF-8";
    return "CP1254";
}

std::wstring TsvFile::decodeToWide(const std::vector<char>& raw, const std::string& enc) {
    if (raw.empty()) return L"";
    size_t offset = 0;
    if (enc == "UTF-8-BOM" && raw.size() >= 3) {
        offset = 3;
    }
    std::string s(raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.end());
    if (enc == "UTF-8" || enc == "UTF-8-BOM") return utf8ToWide(s);
    UINT cp = (enc == "CP1254") ? 1254 : (enc == "CP1252") ? 1252 : 1252;
    return cpToWide(s, cp);
}

std::vector<char> TsvFile::encodeFromWide(const std::wstring& text, const std::string& enc) {
    std::string s = (enc == "UTF-8" || enc == "UTF-8-BOM") ? wideToUtf8(text) : wideToCp(text, (enc == "CP1254") ? 1254 : 1252);
    std::vector<char> out;
    if (enc == "UTF-8-BOM") {
        out.push_back(static_cast<char>(0xEF));
        out.push_back(static_cast<char>(0xBB));
        out.push_back(static_cast<char>(0xBF));
    }
    out.insert(out.end(), s.begin(), s.end());
    return out;
}

void TsvFile::clear() {
    header_.clear();
    rows_.clear();
    encoding_.clear();
    lastError_.clear();
}

namespace {
    bool numericLess(const std::wstring& a, const std::wstring& b) {
        long long va = 0, vb = 0;
        const wchar_t* pa = a.c_str();
        const wchar_t* pb = b.c_str();
        while (*pa && (*pa == L' ' || *pa == L'\t')) pa++;
        while (*pb && (*pb == L' ' || *pb == L'\t')) pb++;
        if (*pa) { wchar_t* end; va = std::wcstoll(pa, &end, 10); }
        if (*pb) { wchar_t* end; vb = std::wcstoll(pb, &end, 10); }
        if (va != vb) return va < vb;
        return a < b;
    }
}

void TsvFile::sortByFirstColumnNumeric() {
    if (rows_.empty()) return;
    std::sort(rows_.begin(), rows_.end(), [](const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
        std::wstring va = a.empty() ? L"" : a[0];
        std::wstring vb = b.empty() ? L"" : b[0];
        return numericLess(va, vb);
    });
}

void TsvFile::removeDuplicatesByColumn(int vnumCol) {
    if (rows_.empty() || vnumCol < 0) return;
    std::set<std::wstring> seen;
    std::vector<std::vector<std::wstring>> kept;
    for (auto& row : rows_) {
        std::wstring key = (vnumCol < (int)row.size()) ? row[vnumCol] : L"";
        if (seen.find(key) == seen.end()) {
            seen.insert(key);
            kept.push_back(std::move(row));
        }
    }
    rows_ = std::move(kept);
}

bool TsvFile::load(const std::wstring& path) {
    lastError_.clear();
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        lastError_ = L"Could not open file.";
        return false;
    }
    size_t size = (size_t)f.tellg();
    f.seekg(0);
    std::vector<char> raw(size);
    if (!f.read(raw.data(), size)) {
        lastError_ = L"Could not read file.";
        return false;
    }
    encoding_ = detectEncoding(raw);
    std::wstring text = decodeToWide(raw, encoding_);
    if (text.empty() && !raw.empty()) {
        lastError_ = L"Encoding error.";
        return false;
    }
    if (!text.empty() && text.front() == 0xFEFF) {
        text.erase(text.begin());
    }
    header_.clear();
    rows_.clear();
    std::wstring line;
    std::vector<std::wstring> lines;
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n') { lines.push_back(line); line.clear(); continue; }
        line += c;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) return true;
    for (const wchar_t* p = lines[0].c_str(); *p; ) {
        const wchar_t* start = p;
        while (*p && *p != L'\t') p++;
        header_.push_back(std::wstring(start, p));
        if (*p == L'\t') p++;
    }
    size_t colCount = header_.size();
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::wstring& ln = lines[i];
        if (ln.find_first_not_of(L" \t") == std::wstring::npos) continue;
        std::vector<std::wstring> row;
        for (const wchar_t* p = ln.c_str(); *p; ) {
            const wchar_t* start = p;
            while (*p && *p != L'\t') p++;
            row.push_back(std::wstring(start, p));
            if (*p == L'\t') p++;
        }
        while (row.size() < colCount) row.push_back(L"");
        row.resize(colCount);
        rows_.push_back(row);
    }
    return true;
}

bool TsvFile::save(const std::wstring& path) const {
    lastError_.clear();
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        lastError_ = L"Could not create file.";
        return false;
    }
    std::wstring line;
    for (size_t i = 0; i < header_.size(); ++i) {
        if (i) line += L'\t';
        line += header_[i];
    }
    line += L"\r\n";
    std::vector<char> out = encodeFromWide(line, encoding_.empty() ? "UTF-8" : encoding_);
    f.write(out.data(), out.size());
    for (const auto& row : rows_) {
        line.clear();
        for (size_t i = 0; i < row.size(); ++i) {
            if (i) line += L'\t';
            line += row[i];
        }
        line += L"\r\n";
        out = encodeFromWide(line, encoding_.empty() ? "UTF-8" : encoding_);
        f.write(out.data(), out.size());
    }
    return true;
}

bool TsvFile::saveWithoutHeader(const std::wstring& path) const {
    lastError_.clear();
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        lastError_ = L"Could not create file.";
        return false;
    }
    std::wstring line;
    for (const auto& row : rows_) {
        line.clear();
        for (size_t i = 0; i < row.size(); ++i) {
            if (i) line += L'\t';
            line += row[i];
        }
        line += L"\r\n";
        std::vector<char> out = encodeFromWide(line, encoding_.empty() ? "UTF-8" : encoding_);
        f.write(out.data(), out.size());
    }
    return true;
}

bool TsvFile::saveSafe(const std::wstring& path) const {
    std::wstring tmp = path + L".tmp";
    if (!save(tmp)) return false;
    if (DeleteFileW(path.c_str()) == 0 && GetLastError() != ERROR_FILE_NOT_FOUND) {
        lastError_ = L"Could not replace original file.";
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (MoveFileW(tmp.c_str(), path.c_str()) == 0) {
        lastError_ = L"Could not rename temp file.";
        return false;
    }
    return true;
}
