#include <iostream>
#include <fstream>
#include <unordered_map>
#include <stdint.h>
#include <libxml/parser.h>
#include "nlohmann/json.hpp"

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

const char *keyOutput(uint8_t keyCode, const char *mapName, uint8_t mapIndex)
{
    const char *usedState = "none"; // To be replaced with a setting
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
                    if(output) return CHAR(output);
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
            if(!ATTR_IS(action, "state", usedState)) continue;
            const xmlChar *output = xmlGetProp(action, "output"_x);
            if(output) return CHAR(output);
            // TODO: dead keys
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
            return keyOutput(keyCode, CHAR(baseMapSet), baseIndex);
        }
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    if(argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << "<keyLayout file> <kle json file>" << std::endl;
        return -1;
    }

    LIBXML_TEST_VERSION

    const xmlDoc *rootNode = xmlReadFile(argv[1], nullptr,
            XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET);
    if(!rootNode)
    {
        std::cerr << "Xml parse fail" << std::endl;
        return -1;
    }
    keyboardNode = rootNode->children;
    while(keyboardNode->type != XML_ELEMENT_NODE) keyboardNode = keyboardNode->next;
    actions = findNextChild(keyboardNode->children, "actions");


    nlohmann::json kleKeyboard = nlohmann::json::parse(std::ifstream(argv[2]));
    nlohmann::json outJson = nlohmann::json::array();

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

    for(const nlohmann::json &row : kleKeyboard)
    {
        if(row.type() != nlohmann::json::value_t::array) outJson.push_back(row); // Not keycaps
        else
        {
            nlohmann::json outRow = nlohmann::json::array();
            nlohmann::json keyProperties;
            for(const nlohmann::json &elem : row)
            {
                if(elem.type() == nlohmann::json::value_t::object)
                {
                    keyProperties = elem;
                    continue;
                }
                std::string str = elem;
                auto it = name2Keycode.find(str);
                if(it != name2Keycode.end())
                {
                    const char *c = keyOutput(it->second, "ISO", 0);
                    if(c) str = c;
                }
                if(keyProperties.type() != nlohmann::json::value_t::null) outRow.push_back(keyProperties);
                keyProperties = nlohmann::json();
                outRow.push_back(str);
            }
            outJson.push_back(outRow);
        }
    }

    std::cout << outJson << std::endl;;
}