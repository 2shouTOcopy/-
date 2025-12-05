#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

#include <cjson/cJSON.h>

// Minimal merge demo that mirrors the IsDisplay copy logic in parse.cpp.
// It keeps the destination IsDisplay node (string or nested object) when
// refreshing other fields from the source template.
bool MergeKeepingIsDisplay(const std::string& srcJson,
                           const std::string& dstJson,
                           std::string& mergedJson)
{
    using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    JsonPtr srcRoot(cJSON_Parse(srcJson.c_str()), cJSON_Delete);
    JsonPtr dstRoot(cJSON_Parse(dstJson.c_str()), cJSON_Delete);
    if (!srcRoot || !dstRoot) { return false; }

    auto srcParamNode = cJSON_GetObjectItem(srcRoot.get(), "EItems");
    auto dstParamNode = cJSON_GetObjectItem(dstRoot.get(), "EItems");
    if (!srcParamNode || !dstParamNode) { return false; }

    int srcParamSize = cJSON_GetArraySize(srcParamNode);
    int dstParamSize = cJSON_GetArraySize(dstParamNode);
    if (srcParamSize < 0 || dstParamSize < 0) { return false; }

    std::unordered_set<std::string> srcNamesList;
    for (int i = 0; i < srcParamSize; ++i)
    {
        auto srcChildNode = cJSON_GetArrayItem(srcParamNode, i);
        if (!srcChildNode) { return false; }
        auto srcNameNode = cJSON_GetObjectItem(srcChildNode, "Name");
        if (!srcNameNode || !srcNameNode->valuestring) { return false; }
        srcNamesList.insert(srcNameNode->valuestring);

        int j = 0;
        for (; j < dstParamSize; ++j)
        {
            auto dstChildNode = cJSON_GetArrayItem(dstParamNode, j);
            auto dstNameNode = cJSON_GetObjectItem(dstChildNode, "Name");
            if (!dstChildNode || !dstNameNode || !dstNameNode->valuestring) { return false; }

            if (0 == std::strncmp(dstNameNode->valuestring, srcNameNode->valuestring, 128))
            {
                auto dstIsDisplayNode = cJSON_GetObjectItem(dstChildNode, "IsDisplay");
                if (!dstIsDisplayNode) { return false; }
                auto dstIsDisplayNodeCopy = cJSON_Duplicate(dstIsDisplayNode, 1);
                if (!dstIsDisplayNodeCopy) { return false; }

                cJSON_DeleteItemFromArray(dstParamNode, j);
                auto newChildNode = cJSON_Duplicate(srcChildNode, 1);
                if (!newChildNode)
                {
                    cJSON_Delete(dstIsDisplayNodeCopy);
                    return false;
                }

                cJSON_DeleteItemFromObject(newChildNode, "IsDisplay");
                cJSON_AddItemToObject(newChildNode, "IsDisplay", dstIsDisplayNodeCopy);
                cJSON_AddItemToArray(dstParamNode, newChildNode);
                break;
            }
        }

        if ((j == dstParamSize) && (j > 0))
        {
            cJSON_AddItemToArray(dstParamNode, cJSON_Duplicate(srcChildNode, 1));
        }
    }

    dstParamSize = cJSON_GetArraySize(dstParamNode);
    for (int j = dstParamSize - 1; j >= 0; --j)
    {
        auto node = cJSON_GetArrayItem(dstParamNode, j);
        auto nameNode = cJSON_GetObjectItem(node, "Name");
        if (!nameNode || !nameNode->valuestring ||
            srcNamesList.find(nameNode->valuestring) == srcNamesList.end())
        {
            cJSON_DeleteItemFromArray(dstParamNode, j);
        }
    }

    std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_Print(dstRoot.get()), cJSON_free);
    if (!text) { return false; }
    mergedJson.assign(text.get());
    return true;
}

void PrintIsDisplayType(const std::string& label, const std::string& jsonText)
{
    using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    JsonPtr root(cJSON_Parse(jsonText.c_str()), cJSON_Delete);
    auto arr = cJSON_GetObjectItem(root.get(), "EItems");
    auto firstItem = cJSON_GetArrayItem(arr, 0);
    auto isDisplay = cJSON_GetObjectItem(firstItem, "IsDisplay");
    std::cout << "== " << label << " ==" << std::endl;
    if (cJSON_IsString(isDisplay))
    {
        std::cout << "IsDisplay type: string, value: " << isDisplay->valuestring << std::endl;
    }
    else if (cJSON_IsObject(isDisplay))
    {
        std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_Print(isDisplay), cJSON_free);
        std::cout << "IsDisplay type: object, content: " << text.get() << std::endl;
    }
    else
    {
        std::cout << "IsDisplay type: unknown" << std::endl;
    }
    std::cout << jsonText << "\n" << std::endl;
}

int main()
{
    const std::string srcJson = R"({
        "EItems": [
          {
            "Features": [{ "Name": "PositionX", "Mapping": "FIXTURE_algo_used_init_point_x", "Value": "0", "ValueType": "float" }],
            "Name": "Init Points",
            "Type": "point",
            "IsDisplay": "true",
            "ControlType": "SupDispCtrl"
          }
        ]
    })";

    const std::string dstJsonIsString = R"({
        "EItems": [
          {
            "Features": [{ "Name": "PositionX", "Mapping": "OLD_MAPPING", "Value": "10", "ValueType": "float" }],
            "Name": "Init Points",
            "Type": "point",
            "IsDisplay": "false",
            "ControlType": "SupDispCtrl"
          }
        ]
    })";

    const std::string dstJsonIsObject = R"({
        "EItems": [
          {
            "Features": [{ "Name": "PositionX", "Mapping": "OLD_MAPPING", "Value": "10", "ValueType": "float" }],
            "Name": "Init Points",
            "Type": "point",
            "IsDisplay": { "MainCam": "false", "AssistCam": "true" },
            "ControlType": "SupDispCtrl"
          }
        ]
    })";

    std::string mergedString;
    std::string mergedObject;

    if (!MergeKeepingIsDisplay(srcJson, dstJsonIsString, mergedString))
    {
        std::cerr << "Merge failed for string IsDisplay" << std::endl;
        return 1;
    }
    if (!MergeKeepingIsDisplay(srcJson, dstJsonIsObject, mergedObject))
    {
        std::cerr << "Merge failed for object IsDisplay" << std::endl;
        return 1;
    }

    PrintIsDisplayType("Merged with string IsDisplay", mergedString);
    PrintIsDisplayType("Merged with object IsDisplay", mergedObject);

    return 0;
}
