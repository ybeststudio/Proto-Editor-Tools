#ifndef TSVFILE_H
#define TSVFILE_H

#include <string>
#include <vector>

class TsvFile {
public:
    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;
    bool saveWithoutHeader(const std::wstring& path) const;
    bool saveSafe(const std::wstring& path) const;

    const std::string& encoding() const { return encoding_; }
    void setEncoding(const std::string& e) { encoding_ = e; }
    const std::vector<std::wstring>& header() const { return header_; }
    std::vector<std::wstring>& header() { return header_; }
    const std::vector<std::vector<std::wstring>>& rows() const { return rows_; }
    std::vector<std::vector<std::wstring>>& rows() { return rows_; }

    void clear();
    void sortByFirstColumnNumeric();
    void removeDuplicatesByColumn(int vnumCol);
    size_t rowCount() const { return rows_.size(); }
    size_t columnCount() const { return header_.size(); }
    const std::wstring& lastError() const { return lastError_; }

private:
    static std::string detectEncoding(const std::vector<char>& raw);
    static std::wstring decodeToWide(const std::vector<char>& raw, const std::string& enc);
    static std::vector<char> encodeFromWide(const std::wstring& text, const std::string& enc);

    std::vector<std::wstring> header_;
    std::vector<std::vector<std::wstring>> rows_;
    std::string encoding_;
    mutable std::wstring lastError_;
};

#endif
