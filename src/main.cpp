#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <stdint.h>
#include <libxml/parser.h>
#include <unicode/unistr.h>
#include <unicode/brkiter.h>
#include "nlohmann/json.hpp"
#include "StrHash.hpp"

#define CHAR(X) (reinterpret_cast<const char*>(X))
#define ITERATE_CHILDREN(NODE, VAR, STR) for(const xmlNode *VAR = findNextChild(NODE->children, STR); VAR; \
        VAR = findNextChild(VAR->next, STR))
#define ATTR_IS(NODE, ATTR, VAL) (strcmp(CHAR(xmlGetProp(NODE, ATTR ## _x)), VAL) == 0)

const xmlChar *operator "" _x(const char* s, size_t len)
{
    return reinterpret_cast<const xmlChar*>(s);
}

const xmlNode *keyboardNode;
const xmlNode *actions;

const xmlNode *findNextChild(const xmlNode *node, const char *nodeName)
{
    while(node && (node->type != XML_ELEMENT_NODE || strcmp(CHAR(node->name), nodeName)))
            node = node->next;
    return node;
}

// Legend, isDead
std::pair<const char *, bool> keyOutput(uint8_t keyCode, const char *mapName, const char *stateName, uint8_t mapIndex)
{
    const xmlChar *keyAction = nullptr;
    const xmlNode *foundKeyMap = nullptr;
    ITERATE_CHILDREN(keyboardNode, keyMapSet, "keyMapSet")
    {
        if(!ATTR_IS(keyMapSet, "id", mapName)) continue;
        ITERATE_CHILDREN(keyMapSet, keyMap, "keyMap")
        {
            if(atoi(CHAR(xmlGetProp(keyMap, "index"_x))) != mapIndex) continue;
            foundKeyMap = keyMap;
            ITERATE_CHILDREN(keyMap, key, "key")
            {
                if(atoi(CHAR(xmlGetProp(key, "code"_x))) == keyCode)
                {
                    const xmlChar *output = xmlGetProp(key, "output"_x);
                    if(output) return std::make_pair(CHAR(output), false);
                    keyAction = xmlGetProp(key, "action"_x);
                    break;
                }
            }
            break;
        }
        break;
    }
    if(keyAction) ITERATE_CHILDREN(actions, actionSet, "action")
    {
        if(!ATTR_IS(actionSet, "id", CHAR(keyAction))) continue;
        ITERATE_CHILDREN(actionSet, action, "when")
        {
            if(!ATTR_IS(action, "state", stateName)) continue;
            const xmlChar *output = xmlGetProp(action, "output"_x);
            if(output) return std::make_pair(CHAR(output), false);
            const xmlChar *nextState = xmlGetProp(action, "next"_x);
            if(nextState) return std::make_pair(CHAR(nextState), true);
            // TODO: dead keys strings
            break;
        }
        break;
    }
    else if(foundKeyMap)
    {
        const xmlChar *baseMapSet = xmlGetProp(foundKeyMap, "baseMapSet"_x);
        if(baseMapSet)
        {
            uint8_t baseIndex = static_cast<uint8_t>(atoi(CHAR(xmlGetProp(foundKeyMap, "baseIndex"_x))));
            return keyOutput(keyCode, CHAR(baseMapSet), stateName, baseIndex);
        }
    }
    return std::make_pair(nullptr, false);
}

void error(const std::string &err)
{
    std::cerr << err << std::endl;
    exit(-1);
}

struct LegendSettings
{
    uint8_t index;
    uint8_t place;
    std::string color;
};

struct StateSettings
{
    std::string state;
    std::string display;
    std::string legend;
    bool show;
};

int main(int argc, char **argv)
{
    if(argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << "<keyLayout file> <kle json file> <settings json file>" << std::endl;
        return -1;
    }

    LIBXML_TEST_VERSION

    const xmlDoc *rootNode = xmlReadFile(argv[1], nullptr,
            XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET);
    if(!rootNode) error("Xml parse fail");

    keyboardNode = rootNode->children;
    while(keyboardNode->type != XML_ELEMENT_NODE) keyboardNode = keyboardNode->next;
    actions = findNextChild(keyboardNode->children, "actions");


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
        nlohmann::json mapJson = settings.at("legends").at(i);
        legendSettings.emplace_back();
        LegendSettings &map = legendSettings.back();
        if(!mapJson.contains("index")) error(std::string("maps[") + std::to_string(i) + "] does not contain an index");
        map.index = mapJson.at("index").get<uint8_t>();
        if(!mapJson.contains("place")) error(std::string("maps[") + std::to_string(i) + "] does not contain a place");
        map.place = mapJson.at("place").get<uint8_t>();
        numLegends = std::max<uint8_t>(numLegends, legendSettings[i].place + 1);
        if(mapJson.contains("color")) map.color = mapJson.at("color").get<std::string>();
    }
    std::string deadKeysColor;
    if(settings.contains("deadKeysColor")) deadKeysColor = settings.at("deadKeysColor").get<std::string>();
    if(!settings.contains("states") || !settings.at("states").size()) error("Settings does not contain a non-empty "
            "states array");
    uint8_t numStates = settings.at("states").size();
    std::vector<StateSettings> stateSettings;
    stateSettings.reserve(numStates);
    for(uint8_t i = 0; i < numStates; i++)
    {
        nlohmann::json stateJson = settings.at("states").at(i);
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
    std::unordered_map<std::string, std::string> substitutions;
    if(settings.contains("substitutions"))
            substitutions = settings.at("substitutions").get<std::unordered_map<std::string, std::string>>();
    float stateDy = 0;
    if(settings.contains("stateDy")) stateDy = settings.at("stateDy").get<float>();
    std::unordered_map<std::string, const StateSettings*> stateLookup;
    for(const StateSettings &state: stateSettings) stateLookup.emplace(std::make_pair(state.state, &state));

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
            if(row.type() != nlohmann::json::value_t::array)
            {
                // First row isn't keycaps. Output it only the first time
                if(!iState) outJson.push_back(row);
            }
            else
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
                        auto it = name2Keycode.find(str);
                        if(it != name2Keycode.end())
                        {
                            legends.clear();
                            legends.resize(numLegends);
                            colors.clear();
                            colors.resize(numLegends);
                            uint8_t keyNumLegends = 0;
                            uint8_t keyNumColors = 0;

                            for(uint8_t i = 0; i < numMaps; i++)
                            {
                                const char *c;
                                bool isDead;
                                std::tie(c, isDead) = keyOutput(it->second, usedKeyMapSet.c_str(), state.state.c_str(),
                                        legendSettings[i].index);
                                if(c)
                                {
                                    keyNumLegends = std::max<uint8_t>(keyNumLegends, legendSettings[i].place + 1);
                                    if(isDead)
                                    {
                                        auto it = stateLookup.find(c);
                                        if(it != stateLookup.end()) c = it->second->legend.c_str();
                                    }
                                    legends[legendSettings[i].place] = std::string(c);
                                    const std::string &color = isDead ? deadKeysColor : legendSettings[i].color;
                                    if(!color.empty())
                                    {
                                        keyNumColors = std::max<uint8_t>(keyNumColors, legendSettings[i].place + 1);
                                        colors[legendSettings[i].place] = color;
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
                                icu::BreakIterator *bi =
                                        icu::BreakIterator::createCharacterInstance(icu::Locale::getDefault(), error);
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
                                        if(nonGraphics.empty()) std::cerr << " Substitute this character to remove this"
                                                " warning.";
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
                                case "STATE"_hash:
                                    replace = true;
                                    replaceString = state.display;
                                    break;
                                case "LEGEND"_hash:
                                {
                                    replace = true;
                                    auto it = stateLookup.find(state.state);
                                    replaceString = it == stateLookup.end() ? state.state
                                            : it->second->legend;
                                    break;
                                }
                            }
                            if(replace) str.replace(pos, end - pos, replaceString);
                        }
                    }

                    if(firstElem && firstRow && iState)
                    {
                        keyProperties["y"] = stateDy;
                        if(!keyProperties.contains("a")) keyProperties["a"] = 4;
                        if(!keyProperties.contains("t")) keyProperties["t"] = "#000000";
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


    std::cout << outJson << std::endl;;
}