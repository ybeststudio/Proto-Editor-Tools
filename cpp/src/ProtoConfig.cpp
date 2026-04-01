#include "ProtoConfig.h"
#include <Windows.h>
#include <fstream>
#include <sstream>

namespace {
    const char* kEmbeddedItemProtoConfig = R"YAML(
# embedded item_proto editor config
columns:
  - name: ITEM_VNUM~RANGE
    type: int
  - name: ITEM_NAME(K)
    type: string
  - name: ITEM_TYPE
    type: enum
  - name: SUB_TYPE
    type: enum
  - name: SIZE
    type: int
  - name: ANTI_FLAG
    type: flag
    flag_set: antiflag
  - name: FLAG
    type: flag
    flag_set: item_flag
  - name: ITEM_WEAR
    type: flag
    flag_set: wearflag
  - name: IMMUNE
    type: flag
    flag_set: immuneflag
  - name: GOLD
    type: int
  - name: SHOP_BY_PRICE
    type: int

antiflag:
  - ANTI_FEMALE
  - ANTI_MALE
  - ANTI_MUSA
  - ANTI_ASSASSIN
  - ANTI_SURA
  - ANTI_MUDANG
  - ANTI_GET
  - ANTI_DROP
  - ANTI_SELL
  - ANTI_EMPIRE_A
  - ANTI_EMPIRE_B
  - ANTI_EMPIRE_C
  - ANTI_SAVE
  - ANTI_GIVE
  - ANTI_PKDROP
  - ANTI_STACK
  - ANTI_MYSHOP
  - ANTI_SAFEBOX
  - ANTI_WOLFMAN
  - ANTI_PET20
  - ANTI_PET21

item_flag:
  - ITEM_TUNABLE
  - ITEM_SAVE
  - ITEM_STACKABLE
  - COUNT_PER_1GOLD
  - ITEM_SLOW_QUERY
  - ITEM_UNIQUE
  - ITEM_MAKECOUNT
  - ITEM_IRREMOVABLE
  - CONFIRM_WHEN_USE
  - QUEST_USE
  - QUEST_USE_MULTIPLE
  - QUEST_GIVE
  - ITEM_QUEST
  - LOG
  - STACKABLE
  - SLOW_QUERY
  - REFINEABLE
  - IRREMOVABLE
  - ITEM_APPLICABLE

wearflag:
  - WEAR_BODY
  - WEAR_HEAD
  - WEAR_FOOTS
  - WEAR_WRIST
  - WEAR_WEAPON
  - WEAR_NECK
  - WEAR_EAR
  - WEAR_SHIELD
  - WEAR_UNIQUE
  - WEAR_ARROW
  - WEAR_ABILITY
  - WEAR_COSTUME_BODY
  - WEAR_COSTUME_HAIR
  - WEAR_COSTUME_MOUNT
  - WEAR_COSTUME_ACCE
  - WEAR_COSTUME_WEAPON
  - WEAR_PET
  - WEAR_PENDANT
  - WEAR_GLOVE
  - WEAR_COSTUME_WING
  - WEAR_COSTUME_AURA
  - WEAR_RING1
  - WEAR_RING2

immuneflag:
  - NONE
  - IMMUNE_STUN
  - IMMUNE_SLOW
  - IMMUNE_FALL
  - IMMUNE_CURSE
  - IMMUNE_POISON
  - IMMUNE_TERROR
  - IMMUNE_REFLECT
)YAML";

    const char* kEmbeddedMobProtoConfig = R"YAML(
# embedded mob_proto editor config
columns:
  - VNUM
  - NAME
  - RANK
  - TYPE
  - BATTLE_TYPE
  - LEVEL
  - SIZE
  - AI_FLAG
  - MOUNT_CAPACITY
  - RACE_FLAG
  - IMMUNE_FLAG
  - EMPIRE
  - FOLDER
  - ON_CLICK
  - ST
  - DX
  - HT
  - IQ
  - DAMAGE_MIN
  - DAMAGE_MAX
  - MAX_HP
  - REGEN_CYCLE
  - REGEN_PERCENT
  - GOLD_MIN
  - GOLD_MAX
  - EXP
  - DEF
  - ATTACK_SPEED
  - MOVE_SPEED
  - AGGRESSIVE_HP_PCT
  - AGGRESSIVE_SIGHT
  - ATTACK_RANGE
  - DROP_ITEM
  - RESURRECTION_VNUM
  - ENCHANT_CURSE
  - ENCHANT_SLOW
  - ENCHANT_POISON
  - ENCHANT_STUN
  - ENCHANT_CRITICAL
  - ENCHANT_PENETRATE
  - RESIST_SWORD
  - RESIST_TWOHAND
  - RESIST_DAGGER
  - RESIST_BELL
  - RESIST_FAN
  - RESIST_BOW
  - RESIST_FIRE
  - RESIST_ELECT
  - RESIST_MAGIC
  - RESIST_WIND
  - RESIST_POISON
  - DAM_MULTIPLY
  - SUMMON
  - DRAIN_SP
  - MOB_COLOR
  - POLYMORPH_ITEM
  - SKILL_LEVEL0
  - SKILL_VNUM0
  - SKILL_LEVEL1
  - SKILL_VNUM1
  - SKILL_LEVEL2
  - SKILL_VNUM2
  - SKILL_LEVEL3
  - SKILL_VNUM3
  - SKILL_LEVEL4
  - SKILL_VNUM4
  - SP_BERSERK
  - SP_STONESKIN
  - SP_GODSPEED
  - SP_DEATHBLOW
  - SP_REVIVE
)YAML";

    std::wstring toWide(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring out(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
        return out;
    }
    std::string trim(const std::string& s) {
        size_t a = 0; while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
        size_t b = s.size(); while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r')) b--;
        return s.substr(a, b - a);
    }
    std::string afterColon(const std::string& line) {
        size_t c = line.find(':');
        if (c == std::string::npos) return "";
        c++;
        while (c < line.size() && (line[c] == ' ' || line[c] == '\t')) c++;
        return trim(line.substr(c));
    }

    bool parseYamlStream(std::istream& input, std::vector<ColumnDef>& columns, std::map<std::wstring, std::vector<std::wstring>>& flagSets) {
        columns.clear();
        flagSets.clear();

        std::string line;
        std::string currentKey;
        std::vector<std::wstring>* currentList = nullptr;
        std::string pendingLine;
        auto getNextLine = [&]() -> bool {
            if (!pendingLine.empty()) {
                line = pendingLine;
                pendingLine.clear();
                return true;
            }
            return static_cast<bool>(std::getline(input, line));
        };

        while (getNextLine()) {
            std::string t = trim(line);
            if (t.empty() || (t.size() > 0 && t[0] == '#')) continue;
            if (t == "columns:") { currentKey = "columns"; currentList = nullptr; continue; }
            if (t == "antiflag:") { currentKey = "antiflag"; currentList = &flagSets[L"antiflag"]; currentList->clear(); continue; }
            if (t == "item_flag:") { currentKey = "item_flag"; currentList = &flagSets[L"item_flag"]; currentList->clear(); continue; }
            if (t == "wearflag:") { currentKey = "wearflag"; currentList = &flagSets[L"wearflag"]; currentList->clear(); continue; }
            if (t == "immuneflag:") { currentKey = "immuneflag"; currentList = &flagSets[L"immuneflag"]; currentList->clear(); continue; }
            if (currentKey == "columns" && t.size() >= 2 && t.substr(0, 2) == "- ") {
                std::string rest = trim(t.substr(2));
                ColumnDef def;
                if (rest.size() >= 5 && rest.substr(0, 5) == "name:") {
                    def.name = toWide(trim(rest.substr(5)));
                    while (std::getline(input, line)) {
                        std::string next = trim(line);
                        if (next.empty() || (next.size() > 0 && next[0] == '#')) continue;
                        if (next.size() >= 2 && next.substr(0, 2) == "- ") { pendingLine = line; break; }
                        if (next.find(':') == std::string::npos) continue;
                        if (next.size() >= 5 && next.substr(0, 5) == "type:") def.type = toWide(afterColon(next));
                        else if (next.size() >= 9 && next.substr(0, 9) == "flag_set:") def.flagSet = toWide(afterColon(next));
                    }
                } else {
                    def.name = toWide(rest);
                }
                if (def.type.empty()) def.type = L"string";
                if (!def.name.empty()) columns.push_back(def);
                continue;
            }
            if (currentList && t.size() >= 2 && t.substr(0, 2) == "- ") {
                std::string val = trim(t.substr(2));
                if (!val.empty()) currentList->push_back(toWide(val));
            }
        }

        return !columns.empty() || !flagSets.empty();
    }
}

bool ProtoConfig::load(const std::wstring& yamlPath) {
    std::ifstream f(yamlPath);
    if (!f) return false;
    return parseYamlStream(f, columns_, flagSets_);
}

bool ProtoConfig::loadFromText(const std::string& yamlText) {
    std::istringstream stream(yamlText);
    return parseYamlStream(stream, columns_, flagSets_);
}

const std::vector<std::wstring>& ProtoConfig::flagList(const std::wstring& setName) const {
    static const std::vector<std::wstring> empty;
    auto it = flagSets_.find(setName);
    if (it != flagSets_.end()) return it->second;
    return empty;
}

bool ProtoConfig::isFlagColumn(const std::wstring& colName) const {
    for (const auto& def : columns_)
        if (def.name == colName && def.type == L"flag") return true;
    return false;
}

std::wstring ProtoConfig::flagSetForColumn(const std::wstring& colName) const {
    for (const auto& def : columns_)
        if (def.name == colName && def.type == L"flag") return def.flagSet;
    return L"";
}

const char* ProtoConfig::embeddedItemConfig() {
    return kEmbeddedItemProtoConfig;
}

const char* ProtoConfig::embeddedMobConfig() {
    return kEmbeddedMobProtoConfig;
}
