#ifndef PROTOCONFIG_H
#define PROTOCONFIG_H

#include <string>
#include <vector>
#include <map>

struct ColumnDef {
    std::wstring name;
    std::wstring type;
    std::wstring flagSet;
};

class ProtoConfig {
public:
    bool load(const std::wstring& yamlPath);
    bool loadFromText(const std::string& yamlText);
    static const char* embeddedItemConfig();
    static const char* embeddedMobConfig();

    const std::vector<ColumnDef>& columns() const { return columns_; }
    const std::vector<std::wstring>& flagList(const std::wstring& setName) const;
    bool isFlagColumn(const std::wstring& colName) const;
    std::wstring flagSetForColumn(const std::wstring& colName) const;

private:
    std::vector<ColumnDef> columns_;
    std::map<std::wstring, std::vector<std::wstring>> flagSets_;
};

#endif
