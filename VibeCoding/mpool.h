/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author luyi
 * @date 2020/10/27
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2020/10/27  |V1.0.0  |luyi                |new
 * @warning 
 */
#ifndef __MPOOL_H
#define __MPOOL_H

#include <stddef.h>
#include <stdint.h>

/*返回值约定：0-成功 非0-失败*/
typedef int32_t (*MP_MALLOC)(size_t size, uint32_t alignment, void** vir_addr, void** phy_addr, char* zoon_name);   //说明： 如果分配出来只有1种地址，另外一种地址赋值为相同值
typedef int32_t (*MP_FREE)(uint32_t alignment, void* vir_addr, void* phy_addr);

typedef int32_t (*MP_LOCK)(void* mtx);
typedef int32_t (*MP_UNLOCK)(void* mtx);

#define MP_STAT_ERR_NUM_MAX   (32)
#define MAX_BUCKET_USE_CNT    (1024)

typedef enum
{
	MEM_OS  = 0,            // 系统内存，默认
	MEM_MMZ_WITH_CACHE,     // 带cache智能MMZ内存
	MEM_MMZ_NO_CACHE,       // 不带cache智能MMZ内存
	MEM_SPACE_MAX
} MP_SPACE;

typedef struct _MP_TAB
{
	char      name[32];     // 内存空间名称
	MP_SPACE  space;        // 内存空间

	MP_MALLOC malloc;       // 内存分配回调
	MP_FREE   free;         // 内存回收回调（销毁内存池时使用）

	void*     mtx;          // 临界区锁
	MP_LOCK   lock;         // 进入临界区回调
	MP_UNLOCK unlock;       // 离开临界区回调	
} MP_TAB;

typedef struct _MP_INIT_PARAM
{
	size_t       size;      // 管理内存大小
	uint32_t     align;     // 内存对齐大小
	MP_TAB       tab;       // 内存管理表
} MP_INIT_PARAM;

typedef struct _MP_STAT
{
	size_t       tatol_size;        // 总空间
	size_t       free_size;         // 空闲
	uint32_t     block_num;         // 空闲内存块数
	size_t       max_block_size;    // 最大空闲块大小
	uint32_t     err_cnt;           // 异常地址个数
	size_t       err_addr[MP_STAT_ERR_NUM_MAX];      // 异常地址（虚拟地址）集合，最大记录32个
	char         err_name[MP_STAT_ERR_NUM_MAX][32];  // 异常地址标识名称
} MP_STAT;

typedef struct _BUCKET_USE
{
	uint8_t   used;             // 是否分配
	size_t    size;             // 内存大小
	char      name[32];         // 内存块标识名称
} BUCKET_USE;

typedef struct _MP_STATISTICS
{
	size_t        tatol_size;        // 总空间
	size_t        use_size;          // 已使用空间
	uint32_t      bucket_use_cnt;    // 内存已用块数
	BUCKET_USE*   bucket_use;        // 内存已用块信息
} MP_STATISTICS;

typedef enum
{
	MP_OK = 0,               // 正常
	MP_NULLPTR = -1,         // 空指针
	MP_CONFLICT = -2,        // 地址冲突
	MP_NOTENOUGH = -3,       // 空间不足
	MP_REFREE = -4,          // 重复释放
	MP_ADDRESS = -5,         // 地址不存在
	MP_NOTINIT = -6,         // 内存池未初始化
	MP_PARAM = -7,           // 参数错误
	MP_MISMATCH = -8,        // 虚拟和物理地址不匹配
	MP_MEMINIT = -9,         // 初始化内存失败
	MP_OVERRANGE = -10,      // 地址越界
	MP_NOHEAPUSE = -11,      // 内存记录耗尽
} MP_STATUS;

#ifdef __cplusplus
extern "C" 
{
#endif 

/**
 * @brief 创建内存池
 * @param[in] init_param  初始化参数
 * @param[in] handle    内存池资源句柄
 * @return   0-成功 非0-失败，详见MP_STATUS定义
 */
MP_STATUS mp_init(MP_INIT_PARAM* init_param, void** handle);

/**
 * @brief 内存申请
 * @param[in] handle 内存池资源句柄
 * @param[in] size 申请内存大小
 * @param[in] align 内存对齐大小
 * @param[in] vir_addr 虚拟地址
 * @param[in] phy_addr 物理地址，可以为null，为null表示不需要物理地址
 * @param[in] bref 是否允许复用 
 * @return  0-成功 非0-失败，详见MP_STATUS定义
 */
MP_STATUS mp_malloc(void* handle, size_t size, uint32_t align, int32_t bref, void** vir_addr, void** phy_addr, char* name);

/**
 * @brief 内存再申请
 * @param[in] handle 内存池资源句柄
 * @param[in] size 之前申请的内存大小
 * @param[in] size 需重新申请的内存大小 
 * @param[in] align 内存对齐大小
  * @param[in] vir_addr 虚拟地址
 * @param[in] phy_addr 物理地址，可以为null，为null表示不需要物理地址
 * @return  0-成功 非0-失败，详见MP_STATUS定义
 */
MP_STATUS mp_realloc(void* handle, size_t size, size_t resize, uint32_t align, void** vir_addr, void** phy_addr, char* name);

/**
 * @brief 内存释放
 * @param[in] handle 内存池资源句柄
 * @param[in] vir_addr 虚拟地址
 * @param[in] phy_addr 物理地址，可以为null
 * @param[in] size 地址大小
 * @return 
 */
MP_STATUS mp_free(void* handle, void* vir_addr, void* phy_addr, size_t size);

/**
 * @brief 一次释放所有的内存
 * @param[in] handle 内存池资源句柄
 * @return 
 */
MP_STATUS mp_free_all(void* handle);

/**
 * @brief 根据虚拟地址获取物理地址
 * @param[in] handle 内存池资源句柄
  * @param[in] vir_addr 虚拟地址
 * @param[in] phy_addr 物理地址，可以为null，为null表示不需要物理地址
 * @return  0-成功 非0-失败，详见MP_STATUS定义
 */
MP_STATUS mp_get_phy_addr(void* handle, void* vir_addr, void** phy_addr);

/**
 * @brief 根据地址名称获取地址信息
 * @param[in] handle 内存池资源句柄
 * @param[in] name 地址名称
 * @param[out] addr 地址
 * @param[out] size 长度
 * @return  0-成功 非0-失败，详见MP_STATUS定义
 */
MP_STATUS mp_get_ref_addr_by_name(void* handle, char* name, void** addr, void** phy, size_t* size);

/**
 * @brief 销毁内存池
 * @param[in] handle 内存池资源句柄
 * @return 
 */
MP_STATUS mp_deinit(void* handle);

/**
 * @brief 打印内存池信息
 * @param[in] handle 内存池资源句柄
 * @param[in] prt_buf 打印缓冲区
 * @param[in] buf_len 缓冲区长度
 * @return 
 */
MP_STATUS mp_print(void* handle, char* prt_buf, uint32_t buf_len);

/**
 * @brief 内存碎片整理
 * @param[in] handle 内存池资源句柄
 * @return 
 */
MP_STATUS mp_clean(void* handle);

/**
 * @brief 查询内存池状态
 * @param[in] handle 内存池资源句柄
 * @return 
 */
MP_STATUS mp_stat(void* handle, MP_STAT* stat);

/**
 * @brief 统计内存池信息
 * @param[in] handle 内存池资源句柄
 * @return 
 */
MP_STATUS mp_statistics(void* handle, MP_STATISTICS *info);

/**
 * @brief 内存快照使能控制
 * @param[in] handle 内存池资源句柄
 * @param[in] en 0-禁用 非0-使能
 * @return 
 */
void mp_snapshot(void* handle, int32_t en);

/**
 * @brief 内存快照使能控制
 * @param[in] handle 内存池资源句柄
 * @param[out] size 总内存大小
 * @return 
 */
int32_t mp_get_total_size(void* handle, size_t *size);

/**
 * @brief 内存快照使能控制
 * @param[in] handle 内存池资源句柄
 * @param[out] size 使用内存大小
 * @return 
 */
int32_t mp_get_use_size(void* handle, size_t *size);

/**
 * @brief 内存快照使能控制
 * @param[in] handle 内存池资源句柄
 * @param[out] size 空闲内存大小
 * @return 
 */
int32_t mp_get_free_size(void* handle, size_t *size);

/**
 * @brief 内存地址操作场地校验
 * @param[in] handle 内存池资源句柄
 * @param[in] vir_addr 尝试操作的内存地址 
 * @param[in] size 尝试操作的内存大小
 * @return 
 */
int32_t mp_addr_check(void* handle, void* vir_addr, size_t size);

/**
 * @brief 获取内存池管理一个池子的内存消耗
 * @return 
 */
uint32_t mp_get_pool_size();

#ifdef __cplusplus
}
#endif 

#endif /* __MPOOL_H */

