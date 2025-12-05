#include <cstring>
#include <exception>
#include <unordered_set>

#include "osal_file.h"
#include "adapter/ScheErrorCodeDefine.h"
#include "VmModuleDef.h"
#include "log/log.h"
#include "json_tool.h"
#include "osal_dir.h"

#include "dispmanage.h"

CDispManage::CDispManage()
{

}

int CDispManage::Init(std::string moduleName, std::string pwd)
{
	int ret = IMVS_EC_OK;
	defaultJsonPath = std::string(ALGO_ROOT_DIR)+ moduleName + "/json/" + ALGO_DISP_JSON_NAME ;
	UpdateWorkJsonPath(pwd);

	if (!osal_is_file_exist(workJsonPath.c_str()))
	{
		ret = osal_copy(defaultJsonPath.c_str(),workJsonPath.c_str());
	}
	else
	{
		if("image" == moduleName)
		{
			ret = osal_copy(defaultJsonPath.c_str(),workJsonPath.c_str());
		}
		else
		{
			ret = UpdateJson(defaultJsonPath,workJsonPath);
		}
       
	}
	if(ret != IMVS_EC_OK)
	{
		LOGE("Dispconf file init Failed %d\n\n", ret);
		return ret;
	}
	return ret;
}
int CDispManage::UpdateJson(const std::string& srcJsonPath,const std::string& dstJsonPath)
{
	if(srcJsonPath.empty() || dstJsonPath.empty()) { return IMVS_EC_FILE_NOT_FOUND;};

	std::unique_ptr<cJSON,std::function<void(cJSON*)>> srcJsonRoot(read_desc_js(srcJsonPath.c_str()),
		[=](cJSON* ptr)
		{
			if (nullptr != ptr)
			{
				cJSON_Delete(ptr);
				ptr = nullptr;
			}		
		}
	);


	std::unique_ptr<cJSON,std::function<void(cJSON*)>> dstJsonRoot(read_desc_js(dstJsonPath.c_str()),
		[=](cJSON* ptr)
		{
			if (nullptr != ptr)
			{
				cJSON_Delete(ptr);
				ptr = nullptr;
			}		
		}
	);

	if(srcJsonRoot == nullptr || dstJsonRoot == nullptr) { return IMVS_EC_OUTOFMEMORY;};

	auto srcParamNode = cJSON_GetObjectItem(srcJsonRoot.get(), "EItems");
	if(srcParamNode == nullptr) {return IMVS_EC_JSON_NODE_NOEXIST;};
	int srcParamSize = cJSON_GetArraySize(srcParamNode);
	if(srcParamSize < 0) {return IMVS_EC_JSON_NODE_NOEXIST;};
    
	auto dstParamNode = cJSON_GetObjectItem(dstJsonRoot.get(), "EItems");
	if(dstParamNode == nullptr) {return IMVS_EC_JSON_NODE_NOEXIST;};
	int dstParamSize = cJSON_GetArraySize(dstParamNode);
	if(dstParamSize < 0) {return IMVS_EC_JSON_NODE_NOEXIST;};

    int j = 0;
	std::unordered_set<std::string> srcNamesList;
	for(int i=0; i< srcParamSize; i++)
	{	
		auto srcChildNode = cJSON_GetArrayItem(srcParamNode, i);
		if(nullptr == srcChildNode) {return IMVS_EC_JSON_NODE_NOEXIST;};
		auto srcNameNode = cJSON_GetObjectItem(srcChildNode, "Name");
		if(nullptr == srcNameNode) {return IMVS_EC_JSON_NODE_NOEXIST;};
		srcNamesList.insert(srcNameNode->valuestring);

		for (j= 0; j < dstParamSize; j++)
		{
			auto dstChildNode = cJSON_GetArrayItem(dstParamNode, j);
			if(nullptr == dstChildNode) {return IMVS_EC_JSON_NODE_NOEXIST;};
			auto dstNameNode = cJSON_GetObjectItem(dstChildNode, "Name");
			if(dstNameNode == nullptr 
			|| dstNameNode->valuestring == nullptr)
			{ return IMVS_EC_JSON_NODE_NOEXIST;};

			if (0 == std::strncmp(dstNameNode->valuestring, srcNameNode->valuestring, MAX_PARAM_NAME_LEN))
			{	
				auto dstIsDisplayNode = cJSON_GetObjectItem(dstChildNode, "IsDisplay");
				if(dstIsDisplayNode == nullptr)
				{ return IMVS_EC_JSON_NODE_NOEXIST;};
				auto dstIsDisplayNodeCopy = cJSON_Duplicate(dstIsDisplayNode, 1);
				if(dstIsDisplayNodeCopy == nullptr)
				{ return IMVS_EC_OUTOFMEMORY;};
		
                 cJSON_DeleteItemFromArray(dstParamNode, j); 
				 auto newChildNode = cJSON_Duplicate(srcChildNode, 1);
				 if(newChildNode == nullptr)
				 {
					cJSON_Delete(dstIsDisplayNodeCopy);
					return IMVS_EC_OUTOFMEMORY;
				 }

				 cJSON_DeleteItemFromObject(newChildNode, "IsDisplay"); // 删除复制的IsDisplay 
				 cJSON_AddItemToObject(newChildNode, "IsDisplay",dstIsDisplayNodeCopy);
				 cJSON_AddItemToArray(dstParamNode, newChildNode);
				 break;
			}
		}
        /*没找到相同节点*/
		if ((j == dstParamSize) && (j > 0))
		{
			cJSON_AddItemToArray(dstParamNode, cJSON_Duplicate(srcChildNode, 1));
		}	

	}

	dstParamSize = cJSON_GetArraySize(dstParamNode);
	for(int j = dstParamSize - 1; j >= 0; j--)
	{
		auto node = cJSON_GetArrayItem(dstParamNode, j);
		auto nameNode = cJSON_GetObjectItem(node, "Name");
		
		if(!nameNode || !nameNode->valuestring || 
			srcNamesList.find(nameNode->valuestring) == srcNamesList.end())
		{
			cJSON_DeleteItemFromArray(dstParamNode, j);
		}
	}
	return  save_json_into_file(dstJsonPath.c_str(), dstJsonRoot.get());
}
int CDispManage::GetDisplayFileData(char* pDisplayBuf, int nBufSize, int* pnOutLen)
{
	int ret = IMVS_EC_OK;
	if (!osal_is_file_exist(workJsonPath.c_str()))
	{
		LOGE("get json data Failed %d\n\n", ret);
		return IMVS_EC_FILE_NOT_FOUND;
	}
	
	ret = get_json_binay_data(workJsonPath.c_str(), pDisplayBuf, nBufSize, pnOutLen);
	if(ret != IMVS_EC_OK)
	{
		LOGE("get json data Failed %d\n\n", ret);
		return ret;
	}
	
	return ret;
}

int CDispManage::SetDisplayFileData(char* pDisplayBuf, int nBufLen)
{
	return IMVS_EC_NOT_IMPLEMENTED;
}
