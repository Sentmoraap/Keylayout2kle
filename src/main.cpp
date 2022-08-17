#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <stdint.h>
#include <tinyxml2.h>
#include <unicode/unistr.h>
#include <unicode/brkiter.h>
#include <unicode/normlzr.h>
#include "nlohmann/json.hpp"
#include "StrHash.hpp"

#define ITERATE_CHILDREN(NODE, VAR, STR) for(const tinyxml2::XMLElement *VAR = NODE->FirstChildElement(STR);\
        VAR; VAR = VAR->NextSiblingElement(STR))

const tinyxml2::XMLNode *keyboardNode;
const tinyxml2::XMLNode *actions;

struct ModifierSettings
{
    bool isUsed = false;
    std::string prefix;
};

struct KeyWithLevel
{
    uint8_t mapIndex;
    uint8_t keyCode;
};

struct LegendSettings
{
    uint8_t index;
    uint8_t place;
    uint8_t merge[2];
    enum : uint8_t {NO, SAME, UPPERCASE, LOWERCASE} mergeType = NO;
    std::string color;
};

struct StateSettings
{
    std::string state;
    std::string display;
    std::string legend;
    bool show;
};

std::vector<ModifierSettings> modifierSettings;
std::vector<StateSettings> stateSettings;
std::unordered_map<std::string, const StateSettings*> stateLookup;
std::unordered_map<std::string, std::string> substitutions;

// Legend, isDead
std::pair<const char *, bool> keyOutput(const char *mapName, const char *stateName, uint8_t mapIndex, uint8_t keyCode)
{

    const char *keyAction = nullptr;
    const tinyxml2::XMLElement *foundKeyMap = nullptr;
    ITERATE_CHILDREN(keyboardNode, keyMapSet, "keyMapSet")
    {
        if(!keyMapSet->Attribute("id", mapName)) continue;
        ITERATE_CHILDREN(keyMapSet, keyMap, "keyMap")
        {
            if(keyMap->IntAttribute("index") != mapIndex) continue;
            foundKeyMap = keyMap;
            ITERATE_CHILDREN(keyMap, key, "key")
            {
                if(key->IntAttribute("code") == keyCode)
                {
                    const char *output = key->Attribute("output");
                    if(output) return std::make_pair(output, false);
                    keyAction = key->Attribute("action");
                    break;
                }
            }
            break;
        }
        break;
    }
    if(keyAction) ITERATE_CHILDREN(actions, actionSet, "action")
    {
        if(!actionSet->Attribute("id", keyAction)) continue;
        ITERATE_CHILDREN(actionSet, action, "when")
        {
            if(!action->Attribute("state", stateName)) continue;
            const char *output = action->Attribute("output");
            if(output) return std::make_pair(output, false);
            const char *nextState = action->Attribute("next");
            if(nextState) return std::make_pair(nextState, true);
            break;
        }
        break;
    }
    else if(foundKeyMap)
    {
        const char *baseMapSet = foundKeyMap->Attribute("baseMapSet");
        if(baseMapSet)
        {
            uint8_t baseIndex = static_cast<uint8_t>(foundKeyMap->IntAttribute("baseIndex"));
            return keyOutput(baseMapSet, stateName, baseIndex, keyCode);
        }
    }
    return std::make_pair(nullptr, false);
}


const char *actionState(const char *actionName)
{
    ITERATE_CHILDREN(actions, actionSet, "action")
    {
        if(!actionSet->Attribute("id", actionName)) continue;
        ITERATE_CHILDREN(actionSet, action, "when")
        {
            if(!action->Attribute("state", "none")) continue;
            if(action->Attribute("output")) return "";
            return static_cast<const char*>(action->Attribute("next"));
        }
    }
    return "";
}

std::vector<std::vector<KeyWithLevel>> findStatePath(const char *mapName, const char *stateName,
        const std::unordered_set<std::string> &forbiddenStates = std::unordered_set<std::string>())
{
    // Outer: multiple paths, inner: a path with multiple keys
    std::vector<std::vector<KeyWithLevel>> ret;
    if(!strcmp(stateName, "none"))
    {
        ret.resize(1);
        return ret;
    }
    std::unordered_set<std::string> newForbiddenStates = forbiddenStates;
    newForbiddenStates.insert(stateName);
    auto findKeys = [mapName, stateName, &ret, &forbiddenStates, &newForbiddenStates](bool fromRoot)
    {
        ITERATE_CHILDREN(keyboardNode, keyMapSet, "keyMapSet")
        {
            if(!keyMapSet->Attribute("id", mapName)) continue;
            ITERATE_CHILDREN(keyMapSet, keyMap, "keyMap")
            {
                uint8_t mapIndex = static_cast<uint8_t>(keyMap->IntAttribute("index"));
                if(mapIndex >= modifierSettings.size() || !modifierSettings[mapIndex].isUsed) continue;
                auto processKeyMap = [fromRoot, mapName, stateName, &ret, &forbiddenStates, &newForbiddenStates]
                        (const tinyxml2::XMLElement *keyMap, uint8_t mapIndex)
                {
                    ITERATE_CHILDREN(keyMap, key, "key")
                    {
                        uint8_t keyCode = static_cast<uint8_t>(key->IntAttribute("code"));
                        if(key->Attribute("action"))
                        {
                            const char *actionName = key->Attribute("action");
                            if(fromRoot)
                            {
                                const char *nextState = actionState(actionName);
                                if(!strcmp(stateName, nextState))
                                {
                                    std::vector<KeyWithLevel> newPath;
                                    newPath.push_back(KeyWithLevel{mapIndex, keyCode});
                                    ret.push_back(newPath);
                                }
                            }
                            else
                            {
                                ITERATE_CHILDREN(actions, actionSet, "action")
                                {
                                    if(!actionSet->Attribute("id", actionName)) continue;
                                    ITERATE_CHILDREN(actionSet, action, "when")
                                    {
                                        if(action->Attribute("state", "none")) continue;
                                        if(forbiddenStates.count(action->Attribute("state"))) continue;
                                        if(action->Attribute("output")) continue;
                                        if(action->Attribute("next", stateName))
                                        {
                                            std::vector<std::vector<KeyWithLevel>> paths =
                                                    findStatePath(mapName, action->Attribute("state"),
                                                    newForbiddenStates);
                                            for(std::vector<KeyWithLevel> &vec : paths)
                                            {
                                                ret.push_back(std::move(vec));
                                                ret.back().push_back(KeyWithLevel{mapIndex, keyCode});
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                };
                processKeyMap(keyMap, mapIndex);
                const char *baseMapSet = keyMap->Attribute("baseMapSet");
                if(baseMapSet)
                {
                    uint8_t baseIndex = static_cast<uint8_t>(keyMap->IntAttribute("baseIndex"));
                    ITERATE_CHILDREN(keyboardNode, baseKeyMapSet, "keyMapSet")
                    {
                        if(!baseKeyMapSet->Attribute("id", baseMapSet)) continue;
                        ITERATE_CHILDREN(baseKeyMapSet, baseKeyMap, "keyMap")
                        {
                            if(baseKeyMap->IntAttribute("index") != baseIndex) continue;
                            processKeyMap(baseKeyMap, mapIndex);
                        }
                    }
                }
            }
        }
    };
    findKeys(true);
    if(ret.empty()) findKeys(false);
    return ret;
}

std::string statePath2String(const char *mapName, const std::vector<std::vector<KeyWithLevel>>& paths)
{
    std::string ret;
    uint8_t minLength = 255;
    std::unordered_set<StrHash, StrHashIdentity> displayedPaths; // To remove duplicates
    for(const std::vector<KeyWithLevel>& vec : paths) minLength = std::min(minLength, static_cast<uint8_t>(vec.size()));
    for(const std::vector<KeyWithLevel>& vec : paths) if(vec.size() == minLength)
    {
        std::string pathStr;
        const char *prevPrefix = "";
        for(const KeyWithLevel& key : vec)
        {
            const char *prefix = modifierSettings[key.mapIndex].prefix.c_str();
            if(strcmp(prefix, prevPrefix))
            {
                if(!pathStr.empty()) pathStr += " ";
                pathStr += modifierSettings[key.mapIndex].prefix;
            }
            const char *outStr;
            bool isDead;
            std::tie(outStr, isDead) = keyOutput(mapName, "none", 0, key.keyCode);
            if(isDead) pathStr += stateLookup.find(outStr)->second->legend;
            else
            {
                auto it = substitutions.find(outStr);
                if(it != substitutions.end()) pathStr += it->second;
                else pathStr += outStr;
            }
            prevPrefix = prefix;
        }
        StrHash strHash = StrHash::make(pathStr);
        if(displayedPaths.count(strHash)) continue;
        displayedPaths.insert(strHash);
        if(ret.size()) ret += " | ";
        ret += pathStr;
    }
    return ret;
}

void error(const std::string &err)
{
    std::cerr << err << std::endl;
    exit(-1);
}

int main(int argc, char **argv)
{
    if(argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << "<keyLayout file> <kle json file> <settings json file>" << std::endl;
        return -1;
    }

    tinyxml2::XMLDocument rootNode;
    {
        tinyxml2::XMLError xmlError;
        xmlError = rootNode.LoadFile(argv[1]);
        if(xmlError != tinyxml2::XML_SUCCESS)
        {
            error("Xml parse fail");
        }
    }
    keyboardNode = rootNode.FirstChildElement();
    actions = keyboardNode->FirstChildElement("actions");

    nlohmann::json kleKeyboard = nlohmann::json::parse(std::ifstream(argv[2]));
    nlohmann::json outJson = nlohmann::json::array();

    // Load json settings
    nlohmann::json settings = nlohmann::json::parse(std::ifstream(argv[3]));
    if(!settings.contains("keyMapSet")) error("Settings does not contain keyMapSet. Add a \"keyMapSet\":X where X is a "
            "keyMapSet's node id attribute");
    std::string usedKeyMapSet = settings.at("keyMapSet").get<std::string>();
    if(!settings.contains("legends") || !settings.at("legends").size()) error("Settings does not contain a non-empty "
            "legends array");
    uint8_t numMaps = settings.at("legends").size();
    std::vector<LegendSettings> legendSettings;
    legendSettings.reserve(numMaps);
    uint8_t numLegends = 0;
    for(uint8_t i = 0; i < numMaps; i++)
    {
        const nlohmann::json &mapJson = settings.at("legends").at(i);
        legendSettings.emplace_back();
        LegendSettings &map = legendSettings.back();
        if(!mapJson.contains("place")) error(std::string("maps[") + std::to_string(i) + "] does not contain a place");
        map.place = mapJson.at("place").get<uint8_t>();
        if(mapJson.contains("merge"))
        {
            map.mergeType = LegendSettings::SAME;
            map.merge[0] = mapJson.at("merge").at(0).get<uint8_t>();
            map.merge[1] = mapJson.at("merge").at(1).get<uint8_t>();
            if(mapJson.contains("mergeRule"))
            {
                StrHash mergeHash = StrHash::make(mapJson.at("mergeRule").get<std::string>());
                switch(mergeHash)
                {
                    case "uppercase"_hash:
                        map.mergeType = LegendSettings::UPPERCASE;
                        break;
                    case "lowercase"_hash:
                        map.mergeType = LegendSettings::LOWERCASE;
                        break;
                    default:
                        error(std::string("maps[") + std::to_string(i) + "]: unknown merge type"
                                + mapJson.at("merge").at(2).get<std::string>());
                        break;
                }
            }
        }
        if(map.mergeType == LegendSettings::NO)
        {
            if(!mapJson.contains("index"))
                    error(std::string("maps[") + std::to_string(i) + "] does not contain an index");
            map.index = mapJson.at("index").get<uint8_t>();
        }
        numLegends = std::max<uint8_t>(numLegends, legendSettings[i].place + 1);
        if(mapJson.contains("color")) map.color = mapJson.at("color").get<std::string>();
    }
    uint8_t numModifiers = settings.contains("modifiers") ? settings.at("modifiers").size() : 0;
    for(uint8_t i = 0; i < numModifiers; i++)
    {
        const nlohmann::json &modifierJson = settings.at("modifiers").at(i);
        uint8_t index = modifierJson.at("index").get<uint8_t>();
        if(index >= modifierSettings.size()) modifierSettings.resize(index + 1);
        modifierSettings[index].isUsed = true;
        modifierSettings[index].prefix = modifierJson.at("prefix").get<std::string>();
    }
    if(!settings.contains("states") || !settings.at("states").size()) error("Settings does not contain a non-empty "
            "states array");
    uint8_t numStates = settings.at("states").size();
    stateSettings.reserve(numStates);
    for(uint8_t i = 0; i < numStates; i++)
    {
        const nlohmann::json &stateJson = settings.at("states").at(i);
        stateSettings.emplace_back();
        StateSettings &state = stateSettings.back();
        if(!stateJson.contains("state")) error(std::string("state[") + std::to_string(i) + "] does not contain a state"
                );
        state.state = stateJson.at("state").get<std::string>();
        if(stateJson.contains("display")) state.display = stateJson.at("display").get<std::string>();
        else state.display = state.state;
        if(stateJson.contains("legend")) state.legend = stateJson.at("legend").get<std::string>();
        else state.legend = state.state;
        if(stateJson.contains("show")) state.show = stateJson.at("show").get<bool>();
        else state.show = true;
    }
    if(settings.contains("substitutions"))
            substitutions = settings.at("substitutions").get<std::unordered_map<std::string, std::string>>();
    float stateDy = 0;
    if(settings.contains("stateDy")) stateDy = settings.at("stateDy").get<float>();
    for(const StateSettings &state: stateSettings) stateLookup.emplace(std::make_pair(state.state, &state));
    bool hasIndex = false;
    float indexWidth = 0.f;
    uint8_t indexNumColumns = 1;
    if(settings.contains("index"))
    {
        const nlohmann::json &indexJson = settings.at("index");
        hasIndex = true;
        if(!indexJson.contains("width")) error("index does not contain width");
        indexWidth = settings.at("index").at("width").get<float>();
        if(indexJson.contains("numColumns")) indexNumColumns = indexJson.at("numColumns").get<uint8_t>();
    }

    // Keycodes of ISO keyboards, strings based on UK QWERTY
    std::unordered_map<std::string, uint8_t> name2Keycode =
    {
        {"#`",     0x0A},
        {"#1",     0x12},
        {"#2",     0x13},
        {"#3",     0x14},
        {"#4",     0x15},
        {"#5",     0x17},
        {"#6",     0x16},
        {"#7",     0x1A},
        {"#8",     0x1C},
        {"#9",     0x19},
        {"#0",     0x1D},
        {"#-",     0x1B},
        {"#=",     0x18},
        {"#Q",     0x0C},
        {"#W",     0x0D},
        {"#E",     0x0E},
        {"#R",     0x0F},
        {"#T",     0x11},
        {"#Y",     0x10},
        {"#U",     0x20},
        {"#I",     0x22},
        {"#O",     0x1F},
        {"#P",     0x23},
        {"#[",     0x21},
        {"#]",     0x1E},
        {"#A",     0x00},
        {"#S",     0x01},
        {"#D",     0x02},
        {"#F",     0x03},
        {"#G",     0x05},
        {"#H",     0x04},
        {"#J",     0x26},
        {"#K",     0x28},
        {"#L",     0x25},
        {"#;",     0x29},
        {"#'",     0x27},
        {"##",     0x2A},
        {"#B/",    0x32},
        {"#Z",     0x06},
        {"#X",     0x07},
        {"#C",     0x08},
        {"#V",     0x09},
        {"#B",     0x0B},
        {"#N",     0x2D},
        {"#M",     0x2E},
        {"#,",     0x2B},
        {"#.",     0x2F},
        {"#/",     0x2C},
        {"#SPACE", 0x31}
    };

    // First row isn't keycaps. Output it once here.
    outJson.push_back(kleKeyboard[0]);

    // Index
    float firstStateDy = 0.f;
    if(hasIndex)
    {
        nlohmann::json outRow = nlohmann::json::array();
        uint8_t numShownStates = 0;
        for(const StateSettings &state : stateSettings) if(state.show) numShownStates++;
        std::vector<std::string> leftColumns, rightColumns;
        uint8_t numRows = (numShownStates + indexNumColumns - 1) / indexNumColumns;
        leftColumns.reserve(numRows);
        rightColumns.reserve(numRows);
        uint8_t iState = 0;
        for(const StateSettings &state : stateSettings) if(state.show)
        {
            uint8_t column = iState * indexNumColumns / numShownStates;
            leftColumns[column] += "<p class=\"indexLeft\"><span class=\"legend\">" + state.legend
                    + "</span><span class=\"stateName\">" + state.display + "</span></p>";
            rightColumns[column] += "<p class=\"indexRight\"><span class=\"path\">"
                    + statePath2String(usedKeyMapSet.c_str(),
                    findStatePath(usedKeyMapSet.c_str(), state.state.c_str()))
                    + "</span><span class=\"pageNumber\">" + std::to_string(iState + 1) + "</span></p>";
            iState++;
        }
        for(uint8_t i = 0; i < indexNumColumns; i++)
        {
            outRow[i * 2]["h"] = numRows * 0.25f;
            outRow[i * 2]["w"] = indexWidth;
            outRow[i * 2]["d"] = true;
            outRow[i * 2 + 1] = leftColumns[i] + "\n\n" + rightColumns[i];
        }
        outJson.push_back(outRow);
        firstStateDy = numRows * 0.25f;
    }

    // States legends
    {
        std::vector<std::string> legends, colors;
        std::unordered_set<UChar32> nonGraphics;
        legends.reserve(numLegends);
        colors.reserve(numLegends);
        uint8_t iState = 0;
        for(const StateSettings &state : stateSettings)
        {
            if(!state.show) continue;
            bool firstRow = true;
            for(const nlohmann::json &row : kleKeyboard)
            {
                if(row.type() == nlohmann::json::value_t::array)
                {
                    nlohmann::json outRow = nlohmann::json::array();
                    nlohmann::json keyProperties;
                    bool firstElem = true;
                    for(const nlohmann::json &elem : row)
                    {
                        if(elem.type() == nlohmann::json::value_t::object)
                        {
                            keyProperties = elem;
                            continue;
                        }
                        std::string str = elem.get<std::string>();
                        if(str[0] == '#')
                        {
                            // Labels based on layout
                            auto keyCodeIt = name2Keycode.find(str);
                            if(keyCodeIt != name2Keycode.end())
                            {
                                legends.clear();
                                legends.resize(numLegends);
                                colors.clear();
                                colors.resize(numLegends);
                                uint8_t keyNumLegends = 0;
                                uint8_t keyNumColors = 0;

                                for(uint8_t i = 0; i < numMaps; i++)
                                {
                                    switch(legendSettings[i].mergeType)
                                    {
                                        case LegendSettings::NO:
                                        {
                                            const char *c;
                                            bool isDead;
                                            std::tie(c, isDead) = keyOutput(usedKeyMapSet.c_str(), state.state.c_str(),
                                                    legendSettings[i].index, keyCodeIt->second);
                                            if(c && (!isDead || strcmp(c, state.state.c_str())))
                                            {
                                                keyNumLegends = std::max<uint8_t>(keyNumLegends,
                                                        legendSettings[i].place + 1);
                                                if(isDead)
                                                {
                                                    // Check if it produces other dead keys when pressed multiple times.
                                                    // The legend shows chains and loops.
                                                    const char *deadKeyChain[3];
                                                    deadKeyChain[0] = c;
                                                    uint8_t numDead = 1;
                                                    while(numDead < 3)
                                                    {
                                                        std::tie(c, isDead) = keyOutput(usedKeyMapSet.c_str(), c,
                                                                legendSettings[i].index, keyCodeIt->second);
                                                        if(isDead) deadKeyChain[numDead++] = c;
                                                        else break;
                                                    }
                                                    std::string &legend = legends[legendSettings[i].place];
                                                    bool zeroIs2 = false;
                                                    bool currentIs1 = false;
                                                    if(numDead > 2)
                                                    {
                                                        zeroIs2 = !strcmp(deadKeyChain[0], deadKeyChain[2]);
                                                        currentIs1 = !strcmp(state.state.c_str(), deadKeyChain[1]);
                                                        if(zeroIs2 && !currentIs1)
                                                                legend += "<span class=\"nongraphic\">|</span>";

                                                    }
                                                    auto deadKeyLegend = [&legend](const char *deadKeyStr)
                                                    {
                                                        const char *str = deadKeyStr;
                                                        auto stateIt = stateLookup.find(deadKeyStr);
                                                        if(stateIt != stateLookup.end())
                                                                str = stateIt->second->legend.c_str();
                                                        return std::string(str);
                                                    };
                                                    legend += "<span class=\"deadkey\">"
                                                            + deadKeyLegend(deadKeyChain[0]) + "</span>";
                                                    if(numDead >= 2)
                                                    {
                                                        if(!strcmp(deadKeyChain[1], state.state.c_str())
                                                            || !strcmp(deadKeyChain[1], deadKeyChain[0]))
                                                                legend += "<span class=\"nongraphic\">|</span>";
                                                        else
                                                        {
                                                            legend += "<span class=\"deadkey2\">"
                                                                    + deadKeyLegend(deadKeyChain[1]) + "</span>";
                                                            if(numDead >= 3)
                                                            {
                                                                if(!strcmp(state.state.c_str(),
                                                                        deadKeyChain[2]) || zeroIs2)
                                                                        legend += "<span class=\"nongraphic\">|</span>";
                                                                else legend += "<span class=\"nongraphic\">·</span>";
                                                            }
                                                        }
                                                    }
                                                }
                                                else legends[legendSettings[i].place] = std::string(c);
                                                const std::string &color = legendSettings[i].color;
                                                if(!color.empty())
                                                {
                                                    keyNumColors =
                                                           std::max<uint8_t>(keyNumColors, legendSettings[i].place + 1);
                                                    colors[legendSettings[i].place] = color;
                                                }
                                            }
                                            break;
                                        }
                                        case LegendSettings::SAME:
                                            if(legends[legendSettings[i].merge[0]]
                                                    == legends[legendSettings[i].merge[1]]) goto merge;
                                            break;
                                        case LegendSettings::UPPERCASE:
                                        {
                                            icu::UnicodeString str0(legends[legendSettings[i].merge[0]].c_str());
                                            icu::UnicodeString str1(legends[legendSettings[i].merge[1]].c_str());
                                            icu::UnicodeString str0Down = str0; str0Down.toLower();
                                            icu::UnicodeString str1Up = str1; str1Up.toUpper();
                                            if(!str0.compare(str1) || !str0Down.compare(str1) || !str0.compare(str1Up))
                                                    goto merge;
                                            break;
                                        }
                                        case LegendSettings::LOWERCASE:
                                        {
                                            icu::UnicodeString str0(legends[legendSettings[i].merge[0]].c_str());
                                            icu::UnicodeString str1(legends[legendSettings[i].merge[1]].c_str());
                                            icu::UnicodeString str0Up = str0; str0Up.toUpper();
                                            icu::UnicodeString str1Down = str1; str1Down.toLower();
                                            if(!str0.compare(str1) || !str0Up.compare(str1) || !str0.compare(str1Down))
                                                    goto merge;
                                            break;
                                        }
                                        merge:
                                        {
                                            keyNumLegends = std::max<uint8_t>(keyNumLegends,
                                                    legendSettings[i].place + 1);
                                            legends[legendSettings[i].place] =
                                                    std::move(legends[legendSettings[i].merge[0]]);
                                            legends[legendSettings[i].merge[0]].clear();
                                            legends[legendSettings[i].merge[1]].clear();
                                            const std::string &color = legendSettings[i].color;
                                            if(!color.empty())
                                            {
                                                keyNumColors =
                                                        std::max<uint8_t>(keyNumColors, legendSettings[i].place + 1);
                                                colors[legendSettings[i].place] = color;
                                            }
                                        break;
                                        }

                                    }
                                }
                                str = "";
                                for(uint8_t iLegend = 0; iLegend < keyNumLegends; iLegend++)
                                {
                                    std::string legend = legends[iLegend];
                                    auto it = substitutions.find(legend);
                                    if(it != substitutions.end()) legend = it->second;
                                    icu::UnicodeString us(legend.c_str());
                                    UErrorCode error = U_ZERO_ERROR;
                                    icu::BreakIterator *bi = icu::BreakIterator::createCharacterInstance(
                                            icu::Locale::getDefault(), error);
                                    bi->setText(us);
                                    // Add dotted circle on combining characters
                                    if(us.countChar32() == 1)
                                    {
                                        UChar32 c32 = us.char32At(0);
                                        int8_t charCategory = u_charType(c32);
                                        if(charCategory == U_NON_SPACING_MARK || charCategory == U_ENCLOSING_MARK
                                            || charCategory == U_COMBINING_SPACING_MARK)
                                        {
                                            us.insert(0, "</span>");
                                            us.insert(0, 0x25cc);
                                            us.insert(0, "<span class=\"nongraphic\">");
                                            uint8_t combiningClass = u_getCombiningClass(c32);
                                            // Double diacritic, append another dotted circle
                                            if(combiningClass == 233 || combiningClass == 234) us.append(0x25cc);
                                        }
                                        if(!u_isgraph(c32) && nonGraphics.find(c32) == nonGraphics.end())
                                        {
                                            char charName[256];
                                            u_charName(c32, U_UNICODE_CHAR_NAME, charName, 256, &error);
                                            std::cerr << "Warning: character " << std::hex << c32
                                            << " " << charName << " is non-graphic.";
                                            if(nonGraphics.empty()) std::cerr << " Substitute this character to remove"
                                                    " this warning.";
                                            std::cerr << std::endl;
                                            nonGraphics.insert(c32);
                                        }
                                    }

                                    // Add <span> tags around emojis
                                    for(int32_t p = bi->first(); p != icu::BreakIterator::DONE;)
                                    {
                                        int32_t next = bi->next();
                                        int32_t n = next == icu::BreakIterator::DONE ? us.length() : next;
                                        bool isEmoji = u_stringHasBinaryProperty(us.getBuffer() + p, n - p,
                                                UCHAR_RGI_EMOJI);
                                        if(isEmoji) str += "<span class=\"emoji\">";
                                        us.tempSubString(p, n - p).toUTF8String<std::string>(str);
                                        if(isEmoji) str += "</span>";
                                        p = next;
                                    }
                                    str += '\n';
                                }
                                if(keyNumColors)
                                {
                                    std::string colorStr;
                                    for(uint8_t iColor = 0; iColor < keyNumColors; iColor++)
                                    {
                                        colorStr += colors[iColor];
                                        colorStr += "\n";
                                    }
                                    keyProperties["t"] = colorStr;
                                }
                            }
                        }
                        else
                        {
                            // Find variables to replace
                            for(size_t pos = str.find('$'); pos != std::string::npos; pos = str.find('$', ++pos))
                            {
                                StrHash hash;
                                size_t end = pos + 1;
                                while(str[end] >= 'A' && str[end] <= 'Z')
                                {
                                    hash.hashCharacter(str[end]);
                                    end++;
                                }
                                bool replace = false;
                                std::string replaceString;
                                switch(hash)
                                {
                                    case "PAGE"_hash:
                                        replace = true;
                                        replaceString = std::to_string(iState + 1);
                                        break;
                                    case "PATH"_hash:
                                        replace = true;
                                        replaceString = statePath2String(usedKeyMapSet.c_str(),
                                                findStatePath(usedKeyMapSet.c_str(), state.state.c_str()));
                                        break;
                                    case "LEGEND"_hash:
                                    {
                                        replace = true;
                                        auto it = stateLookup.find(state.state);
                                        replaceString = it == stateLookup.end() ? state.state
                                                : it->second->legend;
                                        break;
                                    }
                                    case "STATE"_hash:
                                        replace = true;
                                        replaceString = state.display;
                                        break;
                                }
                                if(replace) str.replace(pos, end - pos, replaceString);
                            }
                        }

                        if(firstElem && firstRow)
                        {
                            if(iState)
                            {
                                keyProperties["y"] = stateDy;
                                if(!keyProperties.contains("a")) keyProperties["a"] = 4;
                                if(!keyProperties.contains("t")) keyProperties["t"] = "#000000";
                            }
                            else keyProperties["y"] = firstStateDy;
                        }
                        if(keyProperties.type() != nlohmann::json::value_t::null) outRow.push_back(keyProperties);
                        keyProperties = nlohmann::json();
                        outRow.push_back(str);
                        firstElem = false;
                    }
                    outJson.push_back(outRow);
                    firstRow = false;
                }
            }
            iState++;
        }
    }

    std::cout << outJson << std::endl;;
}