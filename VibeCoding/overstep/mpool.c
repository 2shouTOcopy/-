/**@file
 * @note Hangzhou Hikvision Digital Technology Co., Ltd. All rights reserved.
 * @brief 
 *
 * @author yeyuankun
 * @date 2020/10/27
 *
 * @version
 *  date        |version |author              |message
 *  :----       |:----   |:----               |:------
 *  2020/10/27  |V1.0.0  |yeyuankun           |创建代码文档
 * @warning 
 */
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>

#include "mpool.h"

#define MEM_HEAP_PADING_BYTE (0x77)  // padding填充内容
#ifdef SC1000
#define MAX_HEAP_USE_CNT     (256)   // 内存记录个数，等同于最大malloc次数
#else
#define MAX_HEAP_USE_CNT     (1024)  // 内存记录个数，等同于最大malloc次数
#endif
#define MAX_REF_CNT          (8)     // 内存最大可复用次数

#define MEM_BORDER_SIZE      (sizeof(size_t))
#define MEM_BORDER_PADING    (0x5A)

#define LOG_TAG "MPOOL"
#include "log/log.h"

#define traceMALLOC(res, addr, size) LOGD("heap %s mp_malloc size %ld addr %p\r\n", res->heap_info.name, size, addr)
#define traceREALLOC(res, addr, size) LOGD("heap %s mp_realloc size %ld addr %p\r\n", res->heap_info.name, size, addr)
#define traceFREE(res, addr, size) LOGD("heap %s mp_free addr %p size %ld\r\n", res->heap_info.name, addr, size)
#define MAX(a, b) (a > b ? a : b)

#define ENTER_MTX(res) res->heap_info.lock(p_mp_res->heap_info.mtx)
#define LEAVE_MTX(res) res->heap_info.unlock(p_mp_res->heap_info.mtx)

#define MP_DUMP_INFO_PATH  "/mnt/data/mp_dump"
#define MP_DIAG_LOG_PATH   "/mnt/data/mp_diag.log"
#define MP_DIAG_RING_SIZE  (256U)

#define MP_BLOCK_MIN_PADDING (sizeof(A_BLOCK_LINK) + sizeof(uint8_t *) + sizeof(uint8_t *))

/* Define the linked list structure.  This is used to link free blocks in order of their size. */
typedef struct _A_BLOCK_LINK
{
	struct _A_BLOCK_LINK *pnext;       // 下一个block
	size_t               padding_size; // 填充长度
	size_t               size;         // 可用长度
} A_BLOCK_LINK;

typedef struct _HEAP_USE
{
	uint8_t   used;                    // 是否分配
	uint8_t   bref;                    // 是否允许复用
	uint8_t   ref_cnt;                 // 内存引用计数
	void*     addr;                    // 虚拟地址
	void*     phy;                     // 物理地址
	size_t    size;                    // 内存实际大小
	uint32_t  align;	               // 默认对齐值
	size_t    ref_size[MAX_REF_CNT];   // 引用内存大小
	char      name[32];                // 内存块标识名称
} HEAP_USE;

typedef struct _HEAP_INFO
{
	char      name[32];                // 地址空间名称
	char      space[32];               // 内存空间
	MP_MALLOC malloc;                  // 内存分配回调
	MP_FREE   free;                    // 内存回收回调（销毁内存池时使用）
	void*     mtx;                     // 临界区锁
	MP_LOCK   lock;                    // 进入临界区回调
	MP_UNLOCK unlock;                  // 离开临界区回调		
	void*     phy_base;                // 物理基址
	void*     vir_base;                // 虚拟基址
	size_t    start;                   // 可用开始地址
	size_t    end;			           // 可用结束地址
	uint32_t  align;	               // 默认对齐值
} HEAP_INFO;

typedef struct _A_MP_RES
{
	HEAP_INFO     heap_info;           // 内存池信息
	uint32_t      heap_use_cnt;        // 内存块记录个数
	uint32_t      heap_used_cnt;       // 已经使用的内存块记录个数
	HEAP_USE*     heap_use;            // 内存块记录地址
	A_BLOCK_LINK  start;               // 空闲内存块头结点
	A_BLOCK_LINK  end;                 // 空闲内存块尾结点
	size_t        freebytes_remaining; // 剩余内存
	size_t        max_free_blocksize;  // 最大空闲内存块大小
	size_t        min_freebytes;       // 最小剩余内存
	size_t        min_malloc_fail_size;// 最小申请内存失败大小
	int32_t       initialised;         // 是否可用
	int32_t       snapshot_en;         // 是否使能内存快照
} A_MP_RES;

typedef struct _MP_DIAG_EVENT
{
	uint64_t      seq;
	uint64_t      ts_ms;
	char          op[24];
	void*         handle;
	void*         block;
	void*         prev;
	void*         next;
	size_t        size;
	uint32_t      align;
	int32_t       ret;
} MP_DIAG_EVENT;

typedef struct _MP_DIAG_RING
{
	uint32_t      next_idx;
	uint64_t      seq;
	MP_DIAG_EVENT events[MP_DIAG_RING_SIZE];
} MP_DIAG_RING;

static MP_DIAG_RING g_mp_diag_ring = {0};

static size_t los_get_heap_max_freesize(A_MP_RES* p_mp_res);
static int32_t los_heap_fragment_clean(A_MP_RES* p_mp_res, A_BLOCK_LINK *p_start_block, A_BLOCK_LINK *p_end_block);
static MP_STATUS mp_reduce_alloc(void* handle, size_t size, size_t resize, uint32_t align, void** vir_addr, void** phy_addr, char* name);
static int32_t mp_validate_freelist(const A_MP_RES* p_mp_res, const char* where);
static int32_t mp_validate_allocated_block(const A_MP_RES* p_mp_res, const void* vir_addr, A_BLOCK_LINK** pp_block, const char* where);
static void mp_diag_record(const char* op, const A_MP_RES* p_mp_res, const A_BLOCK_LINK* block, const A_BLOCK_LINK* prev, const A_BLOCK_LINK* next, size_t size, uint32_t align, int32_t ret);

static uint64_t mp_diag_now_ms(void)
{
	struct timespec ts = {0};
	uint64_t now_ms = 0;

	clock_gettime(CLOCK_REALTIME, &ts);
	now_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;

	return now_ms;
}

static void mp_diag_dump(const A_MP_RES* p_mp_res, const char* reason, const void* suspect)
{
	int fd = -1;
	char buf[256] = {0};
	int len = 0;
	uint32_t i = 0;

	fd = open(MP_DIAG_LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
	if (fd < 0)
	{
		return;
	}

	len = snprintf(buf, sizeof(buf),
		"\n=== mp_diag reason=%s suspect=%p handle=%p ts=%llu ===\n",
		reason ? reason : "unknown", suspect, p_mp_res, (unsigned long long)mp_diag_now_ms());
	if (len > 0)
	{
		write(fd, buf, (size_t)len);
	}

	if (p_mp_res)
	{
		len = snprintf(buf, sizeof(buf),
			"heap[%s] range=[0x%zx,0x%zx) free=%zu max=%zu start.next=%p end=%p\n",
			p_mp_res->heap_info.name,
			p_mp_res->heap_info.start,
			p_mp_res->heap_info.end,
			p_mp_res->freebytes_remaining,
			p_mp_res->max_free_blocksize,
			p_mp_res->start.pnext,
			(void*)&p_mp_res->end);
		if (len > 0)
		{
			write(fd, buf, (size_t)len);
		}
	}

	for (i = 0; i < MP_DIAG_RING_SIZE; i++)
	{
		const MP_DIAG_EVENT* e = &g_mp_diag_ring.events[(g_mp_diag_ring.next_idx + i) % MP_DIAG_RING_SIZE];
		if (e->seq == 0)
		{
			continue;
		}

		len = snprintf(buf, sizeof(buf),
			"seq=%llu ts=%llu op=%s ret=%d h=%p b=%p prev=%p next=%p size=%zu align=%u\n",
			(unsigned long long)e->seq,
			(unsigned long long)e->ts_ms,
			e->op,
			e->ret,
			e->handle,
			e->block,
			e->prev,
			e->next,
			e->size,
			e->align);
		if (len > 0)
		{
			write(fd, buf, (size_t)len);
		}
	}

	close(fd);
}

static void mp_diag_record(const char* op, const A_MP_RES* p_mp_res, const A_BLOCK_LINK* block, const A_BLOCK_LINK* prev, const A_BLOCK_LINK* next, size_t size, uint32_t align, int32_t ret)
{
	uint32_t idx = __sync_fetch_and_add(&g_mp_diag_ring.next_idx, 1U) % MP_DIAG_RING_SIZE;
	uint64_t seq = __sync_add_and_fetch(&g_mp_diag_ring.seq, 1U);
	MP_DIAG_EVENT* e = &g_mp_diag_ring.events[idx];

	memset(e, 0, sizeof(*e));
	e->seq = seq;
	e->ts_ms = mp_diag_now_ms();
	e->handle = (void*)p_mp_res;
	e->block = (void*)block;
	e->prev = (void*)prev;
	e->next = (void*)next;
	e->size = size;
	e->align = align;
	e->ret = ret;
	snprintf(e->op, sizeof(e->op), "%s", op ? op : "unknown");
}

static int32_t mp_ptr_in_heap_range(const A_MP_RES* p_mp_res, const void* ptr)
{
	size_t addr = (size_t)ptr;

	if ((NULL == p_mp_res) || (NULL == ptr))
	{
		return 0;
	}

	return (addr >= p_mp_res->heap_info.start) && (addr < p_mp_res->heap_info.end);
}

static int32_t mp_validate_block_header(const A_MP_RES* p_mp_res, const A_BLOCK_LINK* p_block, const char* where)
{
	size_t block_addr = 0;
	size_t block_span = 0;

	if (NULL == p_mp_res || NULL == p_block)
	{
		return -1;
	}

	if (!mp_ptr_in_heap_range(p_mp_res, p_block))
	{
		LOGE("heap %s validate block %s failed, block=%p out of range [0x%zx,0x%zx)\r\n",
			p_mp_res->heap_info.name, where, p_block, p_mp_res->heap_info.start, p_mp_res->heap_info.end);
		mp_diag_record("block_oob", p_mp_res, p_block, NULL, NULL, 0, 0, -1);
		mp_diag_dump(p_mp_res, where, p_block);
		return -1;
	}

	block_addr = (size_t)p_block;
	if (p_block->padding_size < MP_BLOCK_MIN_PADDING)
	{
		LOGE("heap %s validate block %s failed, block=%p padding=%zu too small\r\n",
			p_mp_res->heap_info.name, where, p_block, p_block->padding_size);
		mp_diag_record("pad_small", p_mp_res, p_block, NULL, p_block->pnext, p_block->size, 0, -1);
		mp_diag_dump(p_mp_res, where, p_block);
		return -1;
	}

	block_span = p_block->padding_size + p_block->size;
	if ((block_span < p_block->padding_size) || (block_addr + block_span > p_mp_res->heap_info.end))
	{
		LOGE("heap %s validate block %s failed, block=%p span=%zu overflow end=0x%zx\r\n",
			p_mp_res->heap_info.name, where, p_block, block_span, p_mp_res->heap_info.end);
		mp_diag_record("span_bad", p_mp_res, p_block, NULL, p_block->pnext, p_block->size, 0, -1);
		mp_diag_dump(p_mp_res, where, p_block);
		return -1;
	}

	if (NULL == p_block->pnext)
	{
		LOGE("heap %s validate block %s failed, block=%p next is NULL\r\n",
			p_mp_res->heap_info.name, where, p_block);
		mp_diag_record("next_null", p_mp_res, p_block, NULL, NULL, p_block->size, 0, -1);
		mp_diag_dump(p_mp_res, where, p_block);
		return -1;
	}

	if (p_block->pnext != &p_mp_res->end)
	{
		if (!mp_ptr_in_heap_range(p_mp_res, p_block->pnext))
		{
			LOGE("heap %s validate block %s failed, block=%p next=%p invalid\r\n",
				p_mp_res->heap_info.name, where, p_block, p_block->pnext);
			mp_diag_record("next_oob", p_mp_res, p_block, NULL, p_block->pnext, p_block->size, 0, -1);
			mp_diag_dump(p_mp_res, where, p_block->pnext);
			return -1;
		}

		if ((size_t)p_block->pnext <= (size_t)p_block)
		{
			LOGE("heap %s validate block %s failed, block=%p next=%p not ascending\r\n",
				p_mp_res->heap_info.name, where, p_block, p_block->pnext);
			mp_diag_record("next_order", p_mp_res, p_block, NULL, p_block->pnext, p_block->size, 0, -1);
			mp_diag_dump(p_mp_res, where, p_block->pnext);
			return -1;
		}
	}

	return 0;
}

static int32_t mp_validate_freelist(const A_MP_RES* p_mp_res, const char* where)
{
	A_BLOCK_LINK* p_block = NULL;
	size_t steps = 0;
	size_t max_steps = 0;

	if (NULL == p_mp_res)
	{
		return -1;
	}

	if (NULL == p_mp_res->start.pnext)
	{
		LOGE("heap %s validate %s failed, start.next is NULL\r\n", p_mp_res->heap_info.name, where);
		mp_diag_record("start_null", p_mp_res, NULL, NULL, NULL, 0, 0, -1);
		mp_diag_dump(p_mp_res, where, NULL);
		return -1;
	}

	if ((p_mp_res->start.pnext != &p_mp_res->end) && !mp_ptr_in_heap_range(p_mp_res, p_mp_res->start.pnext))
	{
		LOGE("heap %s validate %s failed, start.next=%p out of heap\r\n", p_mp_res->heap_info.name, where, p_mp_res->start.pnext);
		mp_diag_record("start_oob", p_mp_res, NULL, &p_mp_res->start, p_mp_res->start.pnext, 0, 0, -1);
		mp_diag_dump(p_mp_res, where, p_mp_res->start.pnext);
		return -1;
	}

	max_steps = (p_mp_res->heap_info.end > p_mp_res->heap_info.start)
		? (p_mp_res->heap_info.end - p_mp_res->heap_info.start) / MP_BLOCK_MIN_PADDING + 2
		: 2;

	p_block = p_mp_res->start.pnext;
	while (p_block != &p_mp_res->end)
	{
		if (steps++ > max_steps)
		{
			LOGE("heap %s validate %s failed, freelist loop detected\r\n", p_mp_res->heap_info.name, where);
			mp_diag_record("loop", p_mp_res, p_block, NULL, NULL, steps, 0, -1);
			mp_diag_dump(p_mp_res, where, p_block);
			return -1;
		}

		if (0 != mp_validate_block_header(p_mp_res, p_block, where))
		{
			return -1;
		}

		p_block = p_block->pnext;
	}

	return 0;
}

static int32_t mp_validate_allocated_block(const A_MP_RES* p_mp_res, const void* vir_addr, A_BLOCK_LINK** pp_block, const char* where)
{
	const uint8_t* pvc = (const uint8_t*)vir_addr;
	A_BLOCK_LINK* block = NULL;
	size_t block_addr = 0;
	size_t border_addr = 0;
	uint32_t i = 0;

	if ((NULL == p_mp_res) || (NULL == vir_addr))
	{
		return -1;
	}

	if (!mp_ptr_in_heap_range(p_mp_res, vir_addr))
	{
		LOGE("heap %s validate alloc %s failed, vir_addr=%p out of range [0x%zx,0x%zx)\r\n",
			p_mp_res->heap_info.name, where, vir_addr, p_mp_res->heap_info.start, p_mp_res->heap_info.end);
		mp_diag_record("alloc_oob", p_mp_res, NULL, NULL, NULL, 0, 0, -1);
		mp_diag_dump(p_mp_res, where, vir_addr);
		return -1;
	}

	if ((size_t)pvc < (p_mp_res->heap_info.start + sizeof(uint8_t*) + sizeof(uint8_t*)))
	{
		LOGE("heap %s validate alloc %s failed, vir_addr=%p too close to heap start\r\n",
			p_mp_res->heap_info.name, where, vir_addr);
		mp_diag_record("alloc_hdr", p_mp_res, NULL, NULL, NULL, 0, 0, -1);
		mp_diag_dump(p_mp_res, where, vir_addr);
		return -1;
	}

	block_addr = *(size_t*)((size_t)pvc - sizeof(uint8_t*));
	border_addr = *(size_t*)((size_t)pvc - sizeof(uint8_t*) - sizeof(uint8_t*));
	block = (A_BLOCK_LINK*)block_addr;

	if (0 != mp_validate_block_header(p_mp_res, block, where))
	{
		return -1;
	}

	if ((size_t)block + block->padding_size != (size_t)pvc)
	{
		LOGE("heap %s validate alloc %s failed, vir_addr=%p block=%p padding=%zu mismatch\r\n",
			p_mp_res->heap_info.name, where, vir_addr, block, block->padding_size);
		mp_diag_record("alloc_pad", p_mp_res, block, NULL, block->pnext, block->size, 0, -1);
		mp_diag_dump(p_mp_res, where, vir_addr);
		return -1;
	}

	if ((border_addr < (size_t)pvc) || (border_addr + MEM_BORDER_SIZE < border_addr) || (border_addr + MEM_BORDER_SIZE > p_mp_res->heap_info.end))
	{
		LOGE("heap %s validate alloc %s failed, border_addr=0x%zx invalid\r\n",
			p_mp_res->heap_info.name, where, border_addr);
		mp_diag_record("alloc_baddr", p_mp_res, block, NULL, block->pnext, block->size, 0, -1);
		mp_diag_dump(p_mp_res, where, (void*)border_addr);
		return -1;
	}

	for (i = 0; i < MEM_BORDER_SIZE; i++)
	{
		if (*((uint8_t*)border_addr + i) != MEM_BORDER_PADING)
		{
			LOGE("heap %s validate alloc %s failed, border corrupted vir=%p border=0x%zx idx=%u val=0x%02x\r\n",
				p_mp_res->heap_info.name, where, vir_addr, border_addr, i, *((uint8_t*)border_addr + i));
			mp_diag_record("alloc_border", p_mp_res, block, NULL, block->pnext, block->size, 0, -1);
			mp_diag_dump(p_mp_res, where, (void*)border_addr);
			return -1;
		}
	}

	if (pp_block)
	{
		*pp_block = block;
	}

	return 0;
}

static void los_dump_info_2_flash(const char* file_path, void* dump_buf, uint32_t dump_len)
{
	FILE *fp = fopen(file_path, "w+");
	if (fp)
	{
		fwrite(dump_buf, 1, dump_len, fp);
		fclose(fp);
	}
}

static void insert_block_into_freelist(A_BLOCK_LINK *p_block_to_insert, A_MP_RES* p_mp_res)
{
	A_BLOCK_LINK *p = NULL;
	A_BLOCK_LINK *old_next = NULL;

	if ((NULL == p_block_to_insert) || (NULL == p_mp_res))
	{
		return;
	}

	if (0 != mp_validate_freelist(p_mp_res, "insert_pre"))
	{
		return;
	}

	mp_diag_record("insert_enter", p_mp_res, p_block_to_insert, NULL, NULL, p_block_to_insert->size, 0, MP_OK);

	/* Iterate through the list until a block is found that has a larger addr */
	/* than the block we are inserting. */
	for (p = &p_mp_res->start;; p = p->pnext)
	{
		/* There is nothing to do here - just iterate to the correct position. */
		if (((size_t)p->pnext > (size_t)p_block_to_insert) || (p->pnext == &p_mp_res->end))
		{
			break;
		}
	}
	/* Update the list to include the block being inserted in the correct */
	/* position. */
	old_next = p->pnext;
	p_block_to_insert->pnext = old_next;
	p->pnext = p_block_to_insert;
	if (0 != mp_validate_block_header(p_mp_res, p_block_to_insert, "insert_new"))
	{
		p->pnext = old_next;
		p_block_to_insert->pnext = NULL;
		mp_diag_record("insert_revert", p_mp_res, p_block_to_insert, p, old_next, p_block_to_insert->size, 0, -1);
		mp_diag_dump(p_mp_res, "insert_new_revert", p_block_to_insert);
		return;
	}

	// 插入新的空闲节点后对新节点前后进行内存整理
	los_heap_fragment_clean(p_mp_res, p, p_block_to_insert->pnext->pnext);

	mp_diag_record("insert_exit", p_mp_res, p_block_to_insert, p, p_block_to_insert->pnext, p_block_to_insert->size, 0, MP_OK);
	(void)mp_validate_freelist(p_mp_res, "insert_post");
}

static void los_heap_use_in(A_MP_RES* p_mp_res, void *addr, void* phy, size_t size, uint32_t align, int32_t bref, char* name)
{
	int32_t i = 0;

	LOGD("heap use in addr %p phy %p size %zu align %u bref %d name %s\r\n", addr, phy, size, align, bref, name);

	// 先检查是不是复用内存
	if (bref)
	{
		LOGD("heap use in ref addr %p phy %p size %zu align %u name %s\r\n", addr, phy, size, align, name);
	
		for (i = 0; i < p_mp_res->heap_use_cnt; i++)
		{			
			// 找到可引用的复用内存块
			if (p_mp_res->heap_use[i].used && p_mp_res->heap_use[i].bref
				&& name && !strcmp(name, p_mp_res->heap_use[i].name))
			{
				// 是复用内存
				LOGD("heap use in ref addr find ref block\r\n");
				
				if (size > p_mp_res->heap_use[i].size)
				{
					// 复用内存扩大，更新地址
					p_mp_res->heap_use[i].addr = addr;
					p_mp_res->heap_use[i].phy  = phy;
					p_mp_res->heap_use[i].size = size;
				}
				
				// 补充新引用的长度，计数+1
				if (p_mp_res->heap_use[i].ref_cnt >= MAX_REF_CNT)
				{
					LOGE("heap %s ref cnt overflow, name %s ref_cnt %u max %u\r\n",
						p_mp_res->heap_info.name,
						p_mp_res->heap_use[i].name,
						p_mp_res->heap_use[i].ref_cnt,
						MAX_REF_CNT);
					mp_diag_record("ref_overflow", p_mp_res, NULL, NULL, NULL, size, align, MP_NOHEAPUSE);
					return;
				}

				p_mp_res->heap_use[i].ref_size[p_mp_res->heap_use[i].ref_cnt] = size;
				p_mp_res->heap_use[i].ref_cnt++;

				LOGD("heap use in ref addr %p name %s ref cnt %u size %zu\r\n", addr, name, p_mp_res->heap_use[i].ref_cnt, p_mp_res->heap_use[i].size);

				return;
			}
		}	
	}

	LOGD("heap use in new addr %p phy %p size %zu align %u name %s\r\n", addr, phy, size, align, name);

	// 新内存块
	for (i = 0; i < p_mp_res->heap_use_cnt; i++)
	{
		if (!p_mp_res->heap_use[i].used)
		{
			p_mp_res->heap_use[i].used    = 1;
			p_mp_res->heap_use[i].bref    = (uint8_t)bref;
			p_mp_res->heap_use[i].addr    = addr;
			p_mp_res->heap_use[i].phy     = phy;
			p_mp_res->heap_use[i].size    = size;
			p_mp_res->heap_use[i].align   = align;
			p_mp_res->heap_use[i].ref_cnt = 1;
			p_mp_res->heap_use[i].ref_size[0] = size;
			snprintf(p_mp_res->heap_use[i].name, 32, "%s", name ? name : "undefine");
			p_mp_res->heap_used_cnt++;
			break;
		}
	}

	if (i == p_mp_res->heap_use_cnt)
	{
		LOGE("heap %s not enough heap_use space\r\n", p_mp_res->heap_info.name);
	}
}

static int32_t los_heap_use_out(A_MP_RES* p_mp_res, void *addr, size_t size)
{
	size_t ref_size = 0;
	int32_t i = 0, j = 0;

	LOGD("heap use out addr %p size %zu \r\n", addr, size);
	
	for (i = 0; i < p_mp_res->heap_use_cnt; i++)
	{
		if (p_mp_res->heap_use[i].addr == addr)
		{
			if ((!p_mp_res->heap_use[i].bref && 1 == p_mp_res->heap_use[i].ref_cnt)
				|| (p_mp_res->heap_use[i].bref && 1 == p_mp_res->heap_use[i].ref_cnt && size == p_mp_res->heap_use[i].size))
			{
				if (p_mp_res->heap_use[i].bref)
				{
					LOGI("heap use out ref addr %p size %zu last ref\r\n", addr, size);
				}
				else
				{
					LOGD("heap use out addr %p size %zu\r\n", addr, size);
				}
				
				// 最后一次引用被使用，清空heap信息
				p_mp_res->heap_use[i].used = 0;
				p_mp_res->heap_use[i].size = 0;
				p_mp_res->heap_use[i].ref_cnt = 0;

				for (j = 0;j < MAX_REF_CNT;j++)
				{
					p_mp_res->heap_use[i].ref_size[j] = 0;
				}
				p_mp_res->heap_used_cnt--;
				return MP_OK;
			}
			else
			{
				LOGI("heap use out ref addr %p size %zu ref cnt %d\r\n", addr, size, p_mp_res->heap_use[i].ref_cnt);
				
				// 还需要被引用
				for (j = 0;j < MAX_REF_CNT;j++)
				{
					// 把对应内存使用索引size清空，引用计数-1
					if (p_mp_res->heap_use[i].ref_size[j] == size)
					{
						p_mp_res->heap_use[i].ref_size[j] = 0;
						p_mp_res->heap_use[i].ref_cnt--;
						break;
					}
				}

				if (MAX_REF_CNT == j)
				{
					// size不匹配
					LOGW("heap use out addr %p size %zu failed, size mistach!\r\n", addr, size);
					break;
				}

				// 计算当前可复用内存实际大小，看是否要更新
				for (j = 0;j < MAX_REF_CNT;j++)
				{
					ref_size = MAX(ref_size, p_mp_res->heap_use[i].ref_size[j]);
				}

				if (p_mp_res->heap_use[i].size > ref_size)
				{
					// 需要减少可复用内存
					LOGI("heap use out addr %p size %zu need reduce alloc size %zu reszize %zu\r\n", addr, size, p_mp_res->heap_use[i].size, ref_size);
					mp_reduce_alloc((void*)p_mp_res, 
						p_mp_res->heap_use[i].size, 
						ref_size, 
						p_mp_res->heap_use[i].align, 
						&p_mp_res->heap_use[i].addr, 
						&p_mp_res->heap_use[i].phy, 
						p_mp_res->heap_use[i].name);
					
					p_mp_res->heap_use[i].size = ref_size;
				}
			}
			
			return MP_OK;
		}
	}
	LOGE("heap %s free invalid addr %p\r\n", p_mp_res->heap_info.name, addr);
	return MP_ADDRESS;
}

static uint32_t los_block_align_size(A_BLOCK_LINK *p_block, int32_t align)
{	
	void *pv = NULL;
	uint32_t per_padding_size = 0;
	size_t rem = 0;

	per_padding_size = sizeof(A_BLOCK_LINK) + sizeof(uint8_t *) + sizeof(uint8_t *);
	pv = (void *) (((uint8_t *)p_block) + per_padding_size);		 
	
#if 0
	while ((size_t)pv % align)
	{
		per_padding_size++;
		pv = (void *) (((uint8_t *)p_block) + per_padding_size);
	}

	if (per_padding_size >= (p_block->size + p_block->padding_size))
	{
		// 剩余内存加上填充内存也不够的情况下返回0
		return 0;
	}

	return (p_block->size + p_block->padding_size) - per_padding_size;
#else
	rem = ((size_t)pv % align);
	if (rem)
	{
		per_padding_size += (uint32_t)(align - rem);  // 对齐padding
	}

	if (p_block->size + p_block->padding_size > per_padding_size)
	{
		return p_block->size + p_block->padding_size - per_padding_size;
	}

	return 0;
#endif
}

static int32_t los_heap_fragment_clean(A_MP_RES* p_mp_res, A_BLOCK_LINK *p_start_block, A_BLOCK_LINK *p_end_block)
{
	A_BLOCK_LINK *pb = NULL, *pb_pre = NULL;
	size_t recyle_size = 0;

	p_start_block = (NULL == p_start_block) ? &p_mp_res->start : p_start_block;
	p_end_block = (NULL == p_end_block) ? &p_mp_res->end : p_end_block;

	LOGD("los_heap_fragment_clean start\r\n");
	
	pb_pre = p_start_block;
	pb = p_start_block->pnext;
	if (pb == &p_mp_res->end)
	{
		LOGD("los_heap_fragment_clean done\r\n");
		return 0;
	}

	LOGD("los_heap_fragment_clean continue\r\n");
	
	while (pb != p_end_block)
	{
		if ((size_t)pb == ((size_t)p_start_block + p_start_block->padding_size + p_start_block->size))
		{
			pb_pre->pnext = pb->pnext;
			p_start_block->size += pb->padding_size + pb->size;
			recyle_size += pb->padding_size + pb->size;
			p_mp_res->freebytes_remaining += pb->padding_size;
			pb = pb->pnext;
		}
		else
		{
			break;
		}
	}

	LOGD("heap %s fragmentation recyle memory size %zu\r\n", p_mp_res->heap_info.name, recyle_size);

	p_start_block = p_start_block->pnext;
	if (p_start_block != &p_mp_res->end)
	{
		los_heap_fragment_clean(p_mp_res, p_start_block, p_end_block);
	}

	// 记录最大可用的连续内存块大小
	p_mp_res->max_free_blocksize = los_get_heap_max_freesize(p_mp_res);	

	return 0;
}

static int32_t los_get_heap_fragment_number(A_MP_RES* p_mp_res)
{
	A_BLOCK_LINK *p_block = NULL;
	int32_t block_idx = 0;
	
	p_block = p_mp_res->start.pnext;
	while (p_block != &p_mp_res->end)
	{
		p_block = p_block->pnext;
		block_idx++;
	}

	return block_idx;
}

static size_t los_get_heap_max_freesize(A_MP_RES* p_mp_res)
{
	A_BLOCK_LINK *p_block = NULL;
	size_t max_size = 0;

	p_block = p_mp_res->start.pnext;
	while (p_block != &p_mp_res->end)
	{
		if (p_block->size > max_size)
		{
			max_size = p_block->size;
		}
		p_block = p_block->pnext;
	}

	return max_size;	
}

static A_BLOCK_LINK* los_find_min_block_4_malloc(void* handle, size_t size, uint32_t align, A_BLOCK_LINK** pp_pre_block)
{
	A_BLOCK_LINK *p_block = NULL, *p_min_block = NULL, *p_pre_block = NULL;
	A_MP_RES* p_mp_res = NULL;

	size_t   min_size = 0;
	uint32_t align_size = 0;
	
	if ((NULL == handle) || (NULL == pp_pre_block))
	{
		LOGE("los_find_min_block_4_malloc failed, handle err\r\n");
		return NULL;
	}

	p_mp_res = (A_MP_RES*)handle;
	min_size = (size_t)-1;
	*pp_pre_block = NULL;

	if (0 != mp_validate_freelist(p_mp_res, "find_min_pre"))
	{
		return NULL;
	}

	p_pre_block = &p_mp_res->start;
	p_block = p_mp_res->start.pnext;
	while (p_block != &p_mp_res->end)
	{		
		if (0 != mp_validate_block_header(p_mp_res, p_block, "find_min_walk"))
		{
			return NULL;
		}

		align_size = los_block_align_size(p_block, align);
		if (align_size >= size)
		{
			p_min_block = min_size >= align_size ? p_block : p_min_block;
			*pp_pre_block = min_size >= align_size ? p_pre_block : *pp_pre_block;
			min_size = min_size >= align_size ? align_size : min_size;
		}

		p_pre_block = p_block;
		p_block = p_block->pnext;
		if (NULL == p_block)
		{
			LOGE("heap %s find min block failed, freelist broken with NULL next\r\n", p_mp_res->heap_info.name);
			mp_diag_record("find_null", p_mp_res, p_pre_block, p_pre_block, NULL, size, align, MP_ADDRESS);
			mp_diag_dump(p_mp_res, "find_min_null_next", p_pre_block);
			return NULL;
		}
	}

	return p_min_block;
}

static void mp_snapshot_dump(void* handle)
{
	MP_STATUS sta = MP_OK;
	char dump_path[256] = {0};
	A_MP_RES* p_mp_res = NULL;
	
	void* vir = NULL;
	void* phy = NULL;
	
	if (NULL == handle)
	{
		LOGE("mp_snapshot_dump failed, handle err\r\n");
		return;
	}

	p_mp_res = (A_MP_RES*)handle;

	p_mp_res->snapshot_en = 0;
	
	sta = mp_malloc(handle, 1 * 1024 * 1024, 8, 0, &vir, &phy, NULL);
	if (MP_OK != sta)
	{
		LOGE("mp_snapshot_dump malloc failed, ret %d\r\n", sta);
		p_mp_res->snapshot_en = 1;
		return;
	}

	p_mp_res->snapshot_en = 1;

	memset(vir, 0, 1* 1024 * 1024);
	sta = mp_print(handle, (char*)vir, 1 * 1024 * 1024);
	if (MP_OK != sta)
	{
		LOGE("mp_snapshot_dump print failed, ret %d\r\n", sta);
		goto exit;
	}

	snprintf(dump_path, 256, "%s_%s", MP_DUMP_INFO_PATH, p_mp_res->heap_info.name);
	los_dump_info_2_flash(dump_path, vir, strlen((char*)vir));

exit:
	if (vir)
	{
		sta = mp_free(handle, vir, phy, 1* 1024 * 1024);
		if (MP_OK != sta)
		{
			LOGE("mp_snapshot_dump free failed, ret %d\r\n", sta);
		}
	}
}

MP_STATUS mp_init(MP_INIT_PARAM* init_param, void** handle)
{
	void *vir_base = NULL;
	void *phy_base = NULL;
	A_MP_RES* p_mp_res = NULL;
	void* p_meta_mem = NULL;
	uint8_t *alignedheap = NULL;
	A_BLOCK_LINK *pfreeblock = NULL;

	int32_t ret = 0;
	uint32_t heap_use_cnt = 0;
	size_t heap_use_bytes = 0;
	size_t meta_bytes = 0;
	size_t first_block_bytes = 0;
	int32_t i = 0;
	
	if ((NULL == init_param) || (NULL == handle))
	{
		return MP_NULLPTR;
	}

	if ((NULL == init_param->tab.malloc) || (NULL == init_param->tab.free))
	{
		return MP_NULLPTR;
	}

	if ((NULL == init_param->tab.mtx) || (NULL == init_param->tab.lock) || (NULL == init_param->tab.unlock))
	{
		return MP_NULLPTR;
	}

	if ((0 == init_param->size) || (0 == init_param->align) || (init_param->align % sizeof(void*)))
	{
		LOGE("mp_init invalid size %zu align %u\r\n", init_param->size, init_param->align);
		return MP_PARAM;
	}

	LOGI("mp_init name %s size %zu align %u\r\n", init_param->tab.name, init_param->size, init_param->align);

	// 计算最大记录内存块数
	heap_use_cnt = init_param->size / init_param->align;
	heap_use_cnt = heap_use_cnt > MAX_HEAP_USE_CNT ? MAX_HEAP_USE_CNT : heap_use_cnt;
	heap_use_bytes = sizeof(HEAP_USE) * heap_use_cnt;
	meta_bytes = sizeof(A_MP_RES) + heap_use_bytes;

	p_meta_mem = calloc(1, meta_bytes);
	if (NULL == p_meta_mem)
	{
		LOGE("mp_init name %s calloc meta failed, bytes %zu\r\n", init_param->tab.name, meta_bytes);
		return MP_MEMINIT;
	}

	LOGI("mp_init size %zu meta bytes %zu\r\n", init_param->size, meta_bytes);
	ret = init_param->tab.malloc(init_param->size, init_param->align, &vir_base, &phy_base, init_param->tab.name);
	if (0 != ret)
	{
		LOGE("mp_init name %s malloc failed, ret %d\r\n", init_param->tab.name, ret);
		free(p_meta_mem);
		return ret;
	}

	if (NULL == vir_base)
	{
		LOGE("mp_init name %s malloc NULL memery\r\n", init_param->tab.name);
		free(p_meta_mem);
		return MP_MEMINIT;
	}

	p_mp_res = (A_MP_RES*)p_meta_mem;

	// init heap_info
	snprintf(p_mp_res->heap_info.name, 32, "%s", init_param->tab.name);
	snprintf(p_mp_res->heap_info.space, 32, "%s", (init_param->tab.space) ? "MMZ" : "OS");
	p_mp_res->heap_info.malloc    = init_param->tab.malloc;
	p_mp_res->heap_info.free      = init_param->tab.free;
	p_mp_res->heap_info.mtx       = init_param->tab.mtx;
	p_mp_res->heap_info.lock      = init_param->tab.lock;
	p_mp_res->heap_info.unlock    = init_param->tab.unlock;
	p_mp_res->heap_info.phy_base  = phy_base;
	p_mp_res->heap_info.vir_base  = vir_base;
	p_mp_res->heap_info.start     = (size_t)vir_base;
	p_mp_res->heap_info.end       = (size_t)vir_base + init_param->size;
	p_mp_res->heap_info.align     = init_param->align;

	// init heap_use
	p_mp_res->heap_used_cnt = 0;
	p_mp_res->heap_use_cnt = heap_use_cnt;
	p_mp_res->heap_use = (HEAP_USE*)((size_t)p_meta_mem + sizeof(A_MP_RES));
	for (i = 0;i < p_mp_res->heap_use_cnt;i++)
	{
		p_mp_res->heap_use[i].used = 0;
	}

	// init LINK start
	/* Ensure the heap starts on a correctly aligned boundary. */
	alignedheap = (uint8_t *) ((p_mp_res->heap_info.start + p_mp_res->heap_info.align - 1)
		& ~((size_t)(p_mp_res->heap_info.align - 1)));
	/* Start is used to hold a pointer to the first item in the list of free
	 blocks.  The void cast is used to prevent compiler warnings. */
	p_mp_res->start.pnext = (void *) alignedheap;
	p_mp_res->start.size = (size_t) 0;
	p_mp_res->start.padding_size = (size_t)0;

	// init LINK end
	/* End is used to mark the end of the list of free blocks. */
	p_mp_res->end.padding_size = MP_BLOCK_MIN_PADDING;
	p_mp_res->end.size = 0;
	p_mp_res->end.pnext = NULL;
	
	/* To start with there is a single free block that is sized to take up the
	 entire heap space. */
	pfreeblock = (A_BLOCK_LINK*) alignedheap;
	pfreeblock->padding_size = MP_BLOCK_MIN_PADDING;
	if (p_mp_res->heap_info.end <= ((size_t)alignedheap + pfreeblock->padding_size))
	{
		LOGE("mp_init heap too small after align, start 0x%zx aligned 0x%zx end 0x%zx\r\n",
			p_mp_res->heap_info.start, (size_t)alignedheap, p_mp_res->heap_info.end);
		(void)init_param->tab.free(init_param->align, vir_base, phy_base);
		free(p_meta_mem);
		return MP_MEMINIT;
	}

	first_block_bytes = p_mp_res->heap_info.end - (size_t)alignedheap;
	pfreeblock->size = first_block_bytes - pfreeblock->padding_size;
	pfreeblock->pnext = &p_mp_res->end;
	p_mp_res->freebytes_remaining = pfreeblock->size;
	p_mp_res->max_free_blocksize = los_get_heap_max_freesize(p_mp_res); 
	p_mp_res->min_freebytes = p_mp_res->freebytes_remaining;
	p_mp_res->min_malloc_fail_size = 0;
	p_mp_res->initialised = 1;
	p_mp_res->snapshot_en = 0;
	mp_diag_record("init_ok", p_mp_res, pfreeblock, &p_mp_res->start, pfreeblock->pnext, pfreeblock->size, init_param->align, MP_OK);
	(void)mp_validate_freelist(p_mp_res, "init_post");

	// return handle
	*handle = (void*)p_mp_res;
	
	return MP_OK;
}

MP_STATUS mp_malloc(void* handle, size_t size, uint32_t align, int32_t bref, void** vir_addr, void** phy_addr, char* name)
{
	A_BLOCK_LINK *p_block = NULL, *p_previous_block = NULL, *p_new_block = NULL;
	A_MP_RES* p_mp_res = NULL;
	void *pv = NULL;

	MP_STATUS mp_ret = MP_OK;
	uint32_t per_padding_size = 0, padding_size = 0; //前填充字节总数
	uint32_t max_align = 0;
	int32_t pad_idx = 0;
	size_t max_free_size = 0;
	size_t malloc_size = 0;  // 实际需要:size+border_size

	LOGD("mp_malloc size %zu align %u bref %d name %s start\r\n", size, align, bref, name);

	if (NULL == handle || NULL == vir_addr/* || NULL == phy_addr*/)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}

	mp_diag_record("malloc_enter", p_mp_res, NULL, NULL, NULL, size, align, MP_OK);
	
	ENTER_MTX(p_mp_res);

	if (0 != mp_validate_freelist(p_mp_res, "malloc_pre"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	if (size == 0)
	{
		LOGE("malloc zero size!!!\r\n");
		mp_ret = MP_PARAM;
		goto exit;		
	}

	if (p_mp_res->heap_used_cnt >= p_mp_res->heap_use_cnt)
	{
		LOGE("No heap_use space!!!\r\n");
		mp_ret = MP_NOHEAPUSE;
		goto exit;
	}

	if ((0 == align) || (align % (sizeof(void*))))
	{
		LOGE("malloc align invalied, align %d!!!\r\n", align);
		mp_ret = MP_PARAM;
		goto exit;

	}

	while (size % align)
	{
		size++;
	}

	// add border at malloc memory tail
	malloc_size = size + MEM_BORDER_SIZE;

	max_align = MAX(align, p_mp_res->heap_info.align);
	/* The wanted size is increased so it can contain a BlockLink_t
	 structure and it'addr in addition to the requested amount of bytes. */
	per_padding_size = sizeof(A_BLOCK_LINK) + sizeof(uint8_t *) + sizeof(uint8_t *);
	
	if ((malloc_size > 0) && (malloc_size < p_mp_res->freebytes_remaining))
	{
		if (malloc_size > p_mp_res->max_free_blocksize)
		{
			// 最大的内存块小于需要分配的大小时主动进行碎片整理
			los_heap_fragment_clean(p_mp_res, NULL, NULL);

			if (malloc_size > p_mp_res->max_free_blocksize)
			{
				// 经过碎片整理后仍然没有足够大的一块连续内存则返回失败
				LOGE("mp_malloc size %zu failed, memory max free size %zu is not enough!\r\n", malloc_size, p_mp_res->max_free_blocksize);
				mp_ret = MP_NOTENOUGH;
				goto exit;				
			}
		}
		
		/* Blocks are stored in byte order - traverse the list from the start
		 (smallest) block until one of adequate size is found. */
		p_block = los_find_min_block_4_malloc(handle, malloc_size, max_align, &p_previous_block);
		if (p_block && p_previous_block)
		{
			if (0 != mp_validate_block_header(p_mp_res, p_block, "malloc_pick"))
			{
				mp_ret = MP_ADDRESS;
				goto exit;
			}

			LOGD("mp_malloc find malloc_size %zu align %u p_block %p p_block->pnext %p padding_size %zu\r\n", malloc_size, max_align, p_block, p_block->pnext, p_block->padding_size);
			
			/* Return the memory space - jumping over the BlockLink_t structure
			 at its start. */
			pv = (void *) (((uint8_t *) p_previous_block->pnext) + per_padding_size);		 
			while ((unsigned long)pv % max_align)
			{
				per_padding_size++;
				pv = (void *) (((uint8_t *) p_previous_block->pnext) + per_padding_size);
			}

			/*set padding size*/
			padding_size = ((A_BLOCK_LINK *)p_previous_block->pnext)->padding_size;
			((A_BLOCK_LINK *)p_previous_block->pnext)->padding_size = per_padding_size;
			if (per_padding_size >= padding_size)  // padding_size is change,so need update size param also
			{
				((A_BLOCK_LINK *)p_previous_block->pnext)->size -= (per_padding_size - padding_size);
				p_mp_res->freebytes_remaining -= (per_padding_size - padding_size);
			}
			else
			{
				((A_BLOCK_LINK *)p_previous_block->pnext)->size += (padding_size - per_padding_size);
				p_mp_res->freebytes_remaining += (padding_size - per_padding_size);
			}

			/*pading*/
			for (pad_idx = 0; pad_idx < (int32_t)(per_padding_size - sizeof(uint8_t *) - sizeof(uint8_t *) - sizeof(A_BLOCK_LINK)); pad_idx++)
			{
				*(((uint8_t *)p_previous_block->pnext) + sizeof(A_BLOCK_LINK) + pad_idx) = MEM_HEAP_PADING_BYTE;
			}
			
			/*record start addr*/
			*(size_t*)((size_t)p_previous_block->pnext + per_padding_size - sizeof(uint8_t *)) = (size_t)p_previous_block->pnext;

			/*memory border pading*/
			memset((char*)pv + size, MEM_BORDER_PADING, MEM_BORDER_SIZE);

			/*record memory border addr*/
			*(size_t*)((size_t)p_previous_block->pnext + per_padding_size - sizeof(uint8_t *) - sizeof(uint8_t *)) = (size_t)pv + size;

            p_mp_res->freebytes_remaining -= ((A_BLOCK_LINK *)p_previous_block->pnext)->size;

			/* This block is being returned for use so must be taken out of the
			 list of free blocks. */
			p_previous_block->pnext = p_block->pnext;

			/* If the block is larger than required it can be split into two. */
			if (p_block->size > malloc_size && ((p_block->size - malloc_size) > (per_padding_size * 2)))
			{
				/* This block is to be split into two.	Create a new block
				 following the number of bytes requested. The void cast is
				 used to prevent byte alignment warnings from the compiler. */
				p_new_block = (void *)(((uint8_t *) p_block) + per_padding_size + malloc_size);

				/* Calculate the sizes of two blocks split from the single
				 block. */
				p_new_block->padding_size = per_padding_size;
				p_new_block->size = p_block->size - malloc_size - per_padding_size;
				p_block->size = malloc_size;
				p_mp_res->freebytes_remaining += p_new_block->size;

				/* Insert the new block into the list of free blocks. */
				insert_block_into_freelist(p_new_block, p_mp_res); 			
			}
		}
		else
		{
			*vir_addr = NULL;
			if (phy_addr)
			{
				*phy_addr = NULL;
			}
			
			mp_ret = (0 == mp_validate_freelist(p_mp_res, "malloc_not_enough")) ? MP_NOTENOUGH : MP_ADDRESS;
			goto exit;
		}
	}
	else
	{		
		*vir_addr = NULL;
		if (phy_addr)
		{
			*phy_addr = NULL;
		}
		
		mp_ret = MP_NOTENOUGH;
		goto exit;
	}

	*vir_addr = pv;
	if (phy_addr)
	{
		*phy_addr = (void*)((size_t)p_mp_res->heap_info.phy_base + ((size_t)pv - (size_t)p_mp_res->heap_info.vir_base));  // 根据虚拟基址的偏移计算出物理地址
	}
	
	traceMALLOC(p_mp_res, *vir_addr, size);
	
	// 添加到使用记录
	los_heap_use_in(p_mp_res, *vir_addr, *phy_addr, size, align, bref, name);

	// 记录最大空闲的连续内存块大小
	max_free_size = los_get_heap_max_freesize(p_mp_res);
	p_mp_res->max_free_blocksize = max_free_size;

	// 记录使用过程中最小可用的连续内存块大小
	if (max_free_size < p_mp_res->min_freebytes)
	{
		p_mp_res->min_freebytes = max_free_size;
	}

	if (0 != mp_validate_freelist(p_mp_res, "malloc_post"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	mp_diag_record("malloc_ok", p_mp_res, p_block, p_previous_block, NULL, size, align, MP_OK);

exit:
	if ((MP_OK == mp_ret) && (0 != mp_validate_freelist(p_mp_res, "malloc_exit_check")))
	{
		mp_ret = MP_ADDRESS;
	}
	mp_diag_record("malloc_exit", p_mp_res, p_block, p_previous_block, NULL, size, align, mp_ret);
	LEAVE_MTX(p_mp_res);

	// 	记录分配失败的情况下的最小内存块大小
	if (MP_OK != mp_ret)
	{
		// 内存分配失败时打印内存池分配信息快照记录
		if (p_mp_res->snapshot_en && (MP_ADDRESS != mp_ret))
		{
			mp_snapshot_dump(handle);
		}
	
		if (0 == p_mp_res->min_malloc_fail_size || size < p_mp_res->min_malloc_fail_size)
		{
			p_mp_res->min_malloc_fail_size = size;
		}		
	}
	
	return mp_ret;
}

MP_STATUS mp_realloc(void* handle, size_t size, size_t resize, uint32_t align, void** vir_addr, void** phy_addr, char* name)
{
	A_BLOCK_LINK *p_block = NULL, *p_previous_block = NULL, *p_new_block = NULL;
	A_MP_RES* p_mp_res = NULL;
	void *pv = NULL;
	uint8_t *pvc = NULL;
	A_BLOCK_LINK *block = NULL;

	MP_STATUS mp_ret = MP_OK;
	uint32_t per_padding_size = 0, padding_size = 0; //前填充字节总数
	uint32_t max_align = 0;
	int32_t pad_idx = 0;
	size_t max_free_size = 0;
	size_t malloc_size = 0;  // 实际需要:size+border_size
	int32_t block_idx = 0;

	LOGD("mp_realloc size %zu resize %zu align %u name %s start\r\n", size, resize, align, name);

	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}

	mp_diag_record("realloc_enter", p_mp_res, NULL, NULL, NULL, resize, align, MP_OK);

	ENTER_MTX(p_mp_res);

	if (0 != mp_validate_freelist(p_mp_res, "realloc_pre"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	if ((0 == align) || (align % (sizeof(void*))))
	{
		LOGE("realloc align invalied, align %d!!!\r\n", align);
		mp_ret = MP_PARAM;
		goto exit;
	}

	if (size >= resize)
	{
		// 要新申请的内存小于等于现有内存，无需重新申请，只更新引用计数后返回
		los_heap_use_in(p_mp_res, *vir_addr, *phy_addr, resize, align, 1, name);
		if (0 != mp_validate_freelist(p_mp_res, "realloc_post_small"))
		{
			mp_ret = MP_ADDRESS;
		}
		goto exit;
	}

	//************************** step1.先释放 **************************
	if (NULL == *vir_addr/* || NULL == *phy_addr*/)
	{		
		LOGE("heap %s realloc NULL addr\r\n", p_mp_res->heap_info.name);
		mp_ret =  MP_NULLPTR;
		goto exit;
	}

#if 0
	if (((size_t)vir_addr - (size_t)p_mp_res->heap_info.vir_base) != ((size_t)phy_addr - (size_t)p_mp_res->heap_info.phy_base))
	{
		LOGE("heap %s free addr mismatch, vir_addr %p phy_addr %p\r\n", p_mp_res->heap_info.name, vir_addr, phy_addr);
		mp_ret =  MP_MISMATCH;
		goto exit;		
	}
#endif

	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used && p_mp_res->heap_use[block_idx].addr == *vir_addr)
		{
			break;
		}
	}

	if (block_idx == p_mp_res->heap_use_cnt)
	{
		LOGE("heap %s free vir_addr %p phy_addr %p is invalid on memory space\r\n", 
			p_mp_res->heap_info.name, *vir_addr, *phy_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	pvc = (uint8_t *)*vir_addr;
	if ((p_mp_res->heap_info.end < (size_t)pvc)
		|| ((size_t)pvc < p_mp_res->heap_info.start))
	{
		LOGE("heap %s free vir_addr %p phy_addr %p failed, it's invalid on memory space\r\n", 
			p_mp_res->heap_info.name, *vir_addr, *phy_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	if (0 != mp_validate_allocated_block(p_mp_res, pvc, &block, "realloc_free_block"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	p_mp_res->freebytes_remaining += block->size;

	/* Add this block to the list of free blocks. */
	insert_block_into_freelist(block, p_mp_res);

	traceFREE(p_mp_res, *vir_addr, block->size);

	//************************** step2.再申请 **************************
	if (resize == 0)
	{
		LOGE("realloc zero size!!!\r\n");
		mp_ret = MP_PARAM;
		goto exit;		
	}

	while (resize % align)
	{
		resize++;
	}

	// add border at malloc memory tail
	malloc_size = resize + MEM_BORDER_SIZE;

	max_align = MAX(align, p_mp_res->heap_info.align);
	/* The wanted size is increased so it can contain a BlockLink_t
	 structure and it'addr in addition to the requested amount of bytes. */
	per_padding_size = sizeof(A_BLOCK_LINK) + sizeof(uint8_t *) + sizeof(uint8_t *);
	
	if ((malloc_size > 0) && (malloc_size < p_mp_res->freebytes_remaining))
	{
		if (malloc_size > p_mp_res->max_free_blocksize)
		{
			// 最大的内存块小于需要分配的大小时主动进行碎片整理
			los_heap_fragment_clean(p_mp_res, NULL, NULL);

			if (malloc_size > p_mp_res->max_free_blocksize)
			{
				// 经过碎片整理后仍然没有足够大的一块连续内存则返回失败
				LOGE("mp_realloc size %zu failed, memory max free size %zu is not enough!\r\n", malloc_size, p_mp_res->max_free_blocksize);
				mp_ret = MP_NOTENOUGH;
				goto exit;				
			}
		}
		
		/* Blocks are stored in byte order - traverse the list from the start
		 (smallest) block until one of adequate size is found. */
		p_block = los_find_min_block_4_malloc(handle, malloc_size, max_align, &p_previous_block);
		if (p_block && p_previous_block)
		{
			if (0 != mp_validate_block_header(p_mp_res, p_block, "realloc_pick"))
			{
				mp_ret = MP_ADDRESS;
				goto exit;
			}

			LOGI("mp_realloc find malloc_size %zu align %u p_block %p p_block->pnext %p padding_size %zu\r\n", malloc_size, max_align, p_block, p_block->pnext, p_block->padding_size);
			
			/* Return the memory space - jumping over the BlockLink_t structure
			 at its start. */
			pv = (void *) (((uint8_t *) p_previous_block->pnext) + per_padding_size);		 
			while ((unsigned long)pv % max_align)
			{
				per_padding_size++;
				pv = (void *) (((uint8_t *) p_previous_block->pnext) + per_padding_size);
			}

			/*set padding size*/
			padding_size = ((A_BLOCK_LINK *)p_previous_block->pnext)->padding_size;
			((A_BLOCK_LINK *)p_previous_block->pnext)->padding_size = per_padding_size;
			if (per_padding_size >= padding_size)  // padding_size is change,so need update size param also
			{
				((A_BLOCK_LINK *)p_previous_block->pnext)->size -= (per_padding_size - padding_size);
				p_mp_res->freebytes_remaining -= (per_padding_size - padding_size);
			}
			else
			{
				((A_BLOCK_LINK *)p_previous_block->pnext)->size += (padding_size - per_padding_size);
				p_mp_res->freebytes_remaining += (padding_size - per_padding_size);
			}

			/*pading*/
			for (pad_idx = 0; pad_idx < (int32_t)(per_padding_size - sizeof(uint8_t *) - sizeof(uint8_t *) - sizeof(A_BLOCK_LINK)); pad_idx++)
			{
				*(((uint8_t *)p_previous_block->pnext) + sizeof(A_BLOCK_LINK) + pad_idx) = MEM_HEAP_PADING_BYTE;
			}
			
			/*record start addr*/
			*(size_t*)((size_t)p_previous_block->pnext + per_padding_size - sizeof(uint8_t *)) = (size_t)p_previous_block->pnext;

			/*memory border pading*/
			memset((char*)pv + resize, MEM_BORDER_PADING, MEM_BORDER_SIZE);

			/*record memory border addr*/
			*(size_t*)((size_t)p_previous_block->pnext + per_padding_size - sizeof(uint8_t *) - sizeof(uint8_t *)) = (size_t)pv + resize;
            
			p_mp_res->freebytes_remaining -= ((A_BLOCK_LINK *)p_previous_block->pnext)->size;
            
			/* This block is being returned for use so must be taken out of the
			 list of free blocks. */
			p_previous_block->pnext = p_block->pnext;

			/* If the block is larger than required it can be split into two. */
			if (p_block->size > malloc_size && ((p_block->size - malloc_size) > (per_padding_size * 2)))
			{
				/* This block is to be split into two.	Create a new block
				 following the number of bytes requested. The void cast is
				 used to prevent byte alignment warnings from the compiler. */
				p_new_block = (void *)(((uint8_t *) p_block) + per_padding_size + malloc_size);

				/* Calculate the sizes of two blocks split from the single
				 block. */
				p_new_block->padding_size = per_padding_size;
				p_new_block->size = p_block->size - malloc_size - per_padding_size;
				p_block->size = malloc_size;
				p_mp_res->freebytes_remaining += p_new_block->size;

				/* Insert the new block into the list of free blocks. */
				insert_block_into_freelist(p_new_block, p_mp_res); 			
			}
		}
		else
		{
			mp_ret = (0 == mp_validate_freelist(p_mp_res, "realloc_not_enough")) ? MP_NOTENOUGH : MP_ADDRESS;
			goto exit;
		}
	}
	else
	{		
		*vir_addr = NULL;
		if (phy_addr)
		{
			*phy_addr = NULL;
		}
		
		mp_ret = MP_NOTENOUGH;
		goto exit;
	}

	*vir_addr = pv;
	if (phy_addr)
	{
		*phy_addr = (void*)((size_t)p_mp_res->heap_info.phy_base + ((size_t)pv - (size_t)p_mp_res->heap_info.vir_base));  // 根据虚拟基址的偏移计算出物理地址
	}
	
	if(phy_addr == NULL)
	{
		goto exit;
	}

	traceREALLOC(p_mp_res, *vir_addr, resize);

	// 添加到使用记录
	los_heap_use_in(p_mp_res, *vir_addr, *phy_addr, resize, align, 1, name);

	// 记录最大空闲的连续内存块大小
	max_free_size = los_get_heap_max_freesize(p_mp_res);
	p_mp_res->max_free_blocksize = max_free_size;

	// 记录使用过程中最小可用的连续内存块大小
	if (max_free_size < p_mp_res->min_freebytes)
	{
		p_mp_res->min_freebytes = max_free_size;
	}

	if (0 != mp_validate_freelist(p_mp_res, "realloc_post"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

exit:
	if ((MP_OK == mp_ret) && (0 != mp_validate_freelist(p_mp_res, "realloc_exit_check")))
	{
		mp_ret = MP_ADDRESS;
	}
	mp_diag_record("realloc_exit", p_mp_res, p_block, p_previous_block, NULL, resize, align, mp_ret);
	LEAVE_MTX(p_mp_res);

	// 	记录分配失败的情况下的最小内存块大小
	if (MP_OK != mp_ret)
	{
		// 内存分配失败时打印内存池分配信息快照记录
		if (p_mp_res->snapshot_en && (MP_ADDRESS != mp_ret))
		{
			mp_snapshot_dump(handle);
		}
	
		if (0 == p_mp_res->min_malloc_fail_size || resize < p_mp_res->min_malloc_fail_size)
		{
			p_mp_res->min_malloc_fail_size = resize;
		}		
	}
	
	return mp_ret;
}

static MP_STATUS mp_reduce_alloc(void* handle, size_t size, size_t resize, uint32_t align, void** vir_addr, void** phy_addr, char* name)
{
	A_BLOCK_LINK *p_block = NULL, *p_previous_block = NULL, *p_new_block = NULL;
	A_MP_RES* p_mp_res = NULL;
	void *pv = NULL;
	uint8_t *pvc = NULL;
	A_BLOCK_LINK *block = NULL;

	MP_STATUS mp_ret = MP_OK;
	uint32_t per_padding_size = 0, padding_size = 0; //前填充字节总数
	uint32_t max_align = 0;
	int32_t pad_idx = 0;
	size_t max_free_size = 0;
	size_t malloc_size = 0;  // 实际需要:size+border_size
	int32_t block_idx = 0;

	LOGD("mp_reduce_alloc size %zu resize %zu align %u name %s start\r\n", size, resize, align, name);

	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}

	mp_diag_record("reduce_enter", p_mp_res, NULL, NULL, NULL, resize, align, MP_OK);

	if (0 != mp_validate_freelist(p_mp_res, "reduce_pre"))
	{
		return MP_ADDRESS;
	}
	
	//ENTER_MTX(p_mp_res);

	//************************** 先释放 **************************
	if (NULL == *vir_addr/* || NULL == *phy_addr*/)
	{		
		LOGE("heap %s free NULL addr\r\n", p_mp_res->heap_info.name);
		mp_ret =  MP_NULLPTR;
		goto exit;
	}

#if 0
	if (((size_t)vir_addr - (size_t)p_mp_res->heap_info.vir_base) != ((size_t)phy_addr - (size_t)p_mp_res->heap_info.phy_base))
	{
		LOGE("heap %s free addr mismatch, vir_addr %p phy_addr %p\r\n", p_mp_res->heap_info.name, vir_addr, phy_addr);
		mp_ret =  MP_MISMATCH;
		goto exit;		
	}
#endif

	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used && p_mp_res->heap_use[block_idx].addr == *vir_addr)
		{
			break;
		}
	}

	if (block_idx == p_mp_res->heap_use_cnt)
	{
		LOGE("heap %s free vir_addr %p phy_addr %p is invalid on memory space\r\n", 
			p_mp_res->heap_info.name, *vir_addr, *phy_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	pvc = (uint8_t *)*vir_addr;
	if ((p_mp_res->heap_info.end < (size_t)pvc)
		|| ((size_t)pvc < p_mp_res->heap_info.start))
	{
		LOGE("heap %s free vir_addr %p phy_addr %p failed, it's invalid on memory space\r\n", 
			p_mp_res->heap_info.name, *vir_addr, *phy_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	if (0 != mp_validate_allocated_block(p_mp_res, pvc, &block, "reduce_free_block"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	p_mp_res->freebytes_remaining += block->size;

	/* Add this block to the list of free blocks. */
	insert_block_into_freelist(block, p_mp_res);

	traceFREE(p_mp_res, *vir_addr, block->size);

	//************************** 再申请 **************************
	if (resize == 0)
	{
		LOGE("reduce malloc zero size!!!\r\n");
		mp_ret = MP_PARAM;
		goto exit;		
	}

	if ((0 == align) || (align % (sizeof(void*))))
	{
		LOGE("reduce malloc align invalied, align %d!!!\r\n", align);
		mp_ret = MP_PARAM;
		goto exit;

	}

	while (resize % align)
	{
		resize++;
	}

	// add border at malloc memory tail
	malloc_size = resize + MEM_BORDER_SIZE;

	max_align = MAX(align, p_mp_res->heap_info.align);
	/* The wanted size is increased so it can contain a BlockLink_t
	 structure and it'addr in addition to the requested amount of bytes. */
	per_padding_size = sizeof(A_BLOCK_LINK) + sizeof(uint8_t *) + sizeof(uint8_t *);
	
	if ((malloc_size > 0) && (malloc_size < p_mp_res->freebytes_remaining))
	{
		if (malloc_size > p_mp_res->max_free_blocksize)
		{
			// 最大的内存块小于需要分配的大小时主动进行碎片整理
			los_heap_fragment_clean(p_mp_res, NULL, NULL);

			if (malloc_size > p_mp_res->max_free_blocksize)
			{
				// 经过碎片整理后仍然没有足够大的一块连续内存则返回失败
				LOGE("mp_reduce_malloc size %zu failed, memory max free size %zu is not enough!\r\n", malloc_size, p_mp_res->max_free_blocksize);
				mp_ret = MP_NOTENOUGH;
				goto exit;				
			}
		}
		
		/* Blocks are stored in byte order - traverse the list from the start
		 (smallest) block until one of adequate size is found. */
		p_block = los_find_min_block_4_malloc(handle, malloc_size, max_align, &p_previous_block);
		if (p_block && p_previous_block)
		{
			if (0 != mp_validate_block_header(p_mp_res, p_block, "reduce_pick"))
			{
				mp_ret = MP_ADDRESS;
				goto exit;
			}

			LOGD("mp_reduce_malloc find malloc_size %zu align %u p_block %p p_block->pnext %p padding_size %zu\r\n", malloc_size, max_align, p_block, p_block->pnext, p_block->padding_size);
			
			/* Return the memory space - jumping over the BlockLink_t structure
			 at its start. */
			pv = (void *) (((uint8_t *) p_previous_block->pnext) + per_padding_size);		 
			while ((unsigned long)pv % max_align)
			{
				per_padding_size++;
				pv = (void *) (((uint8_t *) p_previous_block->pnext) + per_padding_size);
			}

			/*set padding size*/
			padding_size = ((A_BLOCK_LINK *)p_previous_block->pnext)->padding_size;
			((A_BLOCK_LINK *)p_previous_block->pnext)->padding_size = per_padding_size;
			if (per_padding_size >= padding_size)  // padding_size is change,so need update size param also
			{
				((A_BLOCK_LINK *)p_previous_block->pnext)->size -= (per_padding_size - padding_size);
				p_mp_res->freebytes_remaining -= (per_padding_size - padding_size);
			}
			else
			{
				((A_BLOCK_LINK *)p_previous_block->pnext)->size += (padding_size - per_padding_size);
				p_mp_res->freebytes_remaining += (padding_size - per_padding_size);
			}

			/*pading*/
			for (pad_idx = 0; pad_idx < (int32_t)(per_padding_size - sizeof(uint8_t *) - sizeof(uint8_t *) - sizeof(A_BLOCK_LINK)); pad_idx++)
			{
				*(((uint8_t *)p_previous_block->pnext) + sizeof(A_BLOCK_LINK) + pad_idx) = MEM_HEAP_PADING_BYTE;
			}
			
			/*record start addr*/
			*(size_t*)((size_t)p_previous_block->pnext + per_padding_size - sizeof(uint8_t *)) = (size_t)p_previous_block->pnext;

			/*memory border pading*/
			memset((char*)pv + resize, MEM_BORDER_PADING, MEM_BORDER_SIZE);

			/*record memory border addr*/
			*(size_t*)((size_t)p_previous_block->pnext + per_padding_size - sizeof(uint8_t *) - sizeof(uint8_t *)) = (size_t)pv + resize;

            p_mp_res->freebytes_remaining -= ((A_BLOCK_LINK *)p_previous_block->pnext)->size;

			/* This block is being returned for use so must be taken out of the
			 list of free blocks. */
			p_previous_block->pnext = p_block->pnext;

			/* If the block is larger than required it can be split into two. */
			if (p_block->size > malloc_size && ((p_block->size - malloc_size) > (per_padding_size * 2)))
			{
				/* This block is to be split into two.	Create a new block
				 following the number of bytes requested. The void cast is
				 used to prevent byte alignment warnings from the compiler. */
				p_new_block = (void *)(((uint8_t *) p_block) + per_padding_size + malloc_size);

				/* Calculate the sizes of two blocks split from the single
				 block. */
				p_new_block->padding_size = per_padding_size;
				p_new_block->size = p_block->size - malloc_size - per_padding_size;
				p_block->size = malloc_size;
				p_mp_res->freebytes_remaining += p_new_block->size;

				/* Insert the new block into the list of free blocks. */
				insert_block_into_freelist(p_new_block, p_mp_res); 			
			}
		}
		else
		{
			mp_ret = (0 == mp_validate_freelist(p_mp_res, "reduce_not_enough")) ? MP_NOTENOUGH : MP_ADDRESS;
			goto exit;
		}
	}
	else
	{		
		*vir_addr = NULL;
		if (phy_addr)
		{
			*phy_addr = NULL;
		}
		
		mp_ret = MP_NOTENOUGH;
		goto exit;
	}

	*vir_addr = pv;
	if (phy_addr)
	{
		*phy_addr = (void*)((size_t)p_mp_res->heap_info.phy_base + ((size_t)pv - (size_t)p_mp_res->heap_info.vir_base));  // 根据虚拟基址的偏移计算出物理地址
	}
	
	traceREALLOC(p_mp_res, *vir_addr, resize);

	// 记录最大空闲的连续内存块大小
	max_free_size = los_get_heap_max_freesize(p_mp_res);
	p_mp_res->max_free_blocksize = max_free_size;

	// 记录使用过程中最小可用的连续内存块大小
	if (max_free_size < p_mp_res->min_freebytes)
	{
		p_mp_res->min_freebytes = max_free_size;
	}

	if (0 != mp_validate_freelist(p_mp_res, "reduce_post"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

exit:
	if ((MP_OK == mp_ret) && (0 != mp_validate_freelist(p_mp_res, "reduce_exit_check")))
	{
		mp_ret = MP_ADDRESS;
	}
	mp_diag_record("reduce_exit", p_mp_res, p_block, p_previous_block, NULL, resize, align, mp_ret);
	//LEAVE_MTX(p_mp_res);

	// 	记录分配失败的情况下的最小内存块大小
	if (MP_OK != mp_ret)
	{
		// 内存分配失败时打印内存池分配信息快照记录
		if (p_mp_res->snapshot_en && (MP_ADDRESS != mp_ret))
		{
			mp_snapshot_dump(handle);
		}
	
		if (0 == p_mp_res->min_malloc_fail_size || resize < p_mp_res->min_malloc_fail_size)
		{
			p_mp_res->min_malloc_fail_size = resize;
		}		
	}
	
	return mp_ret;
}

MP_STATUS mp_free(void* handle, void* vir_addr, void* phy_addr, size_t size)
{
	A_BLOCK_LINK *block = NULL;
	A_MP_RES* p_mp_res = NULL;
	uint8_t *pvc = NULL;

	int32_t block_idx = 0;
	MP_STATUS mp_ret = MP_OK;

	LOGD("mp_free vir_addr %p phy_addr %p size %zu start\r\n", vir_addr, phy_addr, size);
	
	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}

	mp_diag_record("free_enter", p_mp_res, NULL, NULL, NULL, size, 0, MP_OK);

	ENTER_MTX(p_mp_res);

	if (0 != mp_validate_freelist(p_mp_res, "free_pre"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}
	
	if (NULL == vir_addr/* || NULL == phy_addr*/)
	{		
		LOGE("heap %s free NULL addr\r\n", p_mp_res->heap_info.name);
		mp_ret =  MP_NULLPTR;
		goto exit;
	}

#if 0
	if (((size_t)vir_addr - (size_t)p_mp_res->heap_info.vir_base) != ((size_t)phy_addr - (size_t)p_mp_res->heap_info.phy_base))
	{
		LOGE("heap %s free addr mismatch, vir_addr %p phy_addr %p\r\n", p_mp_res->heap_info.name, vir_addr, phy_addr);
		mp_ret =  MP_MISMATCH;
		goto exit;		
	}
#endif

	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used && p_mp_res->heap_use[block_idx].addr == vir_addr)
		{
			break;
		}
	}

	if (block_idx == p_mp_res->heap_use_cnt)
	{
		LOGE("heap %s free vir_addr %p phy_addr %p is invalid on memory space\r\n", 
			p_mp_res->heap_info.name, vir_addr, phy_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	if (p_mp_res->heap_use[block_idx].bref)
	{
		// 可复用内存out处理
		mp_ret = los_heap_use_out(p_mp_res, vir_addr, size);
		if (MP_OK != mp_ret)
		{
			LOGE("heap %s free ref info size %zu failed, ret %d!\r\n", p_mp_res->heap_info.name, size, mp_ret);
			goto exit;			
		}

		// 只要内存还有被引用，则不走free后续流程
		if (0 < p_mp_res->heap_use[block_idx].ref_cnt)
		{
			LOGI("heap %s free ref info size %zu, done!\r\n", p_mp_res->heap_info.name, size);
			goto exit;
		}
	}

	pvc = (uint8_t *)vir_addr;
	if ((p_mp_res->heap_info.end < (size_t)pvc)
		|| ((size_t)pvc < p_mp_res->heap_info.start))
	{
		LOGE("heap %s free vir_addr %p phy_addr %p failed, it's invalid on memory space\r\n", 
			p_mp_res->heap_info.name, vir_addr, phy_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	if (0 != mp_validate_allocated_block(p_mp_res, pvc, &block, "free_block"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	p_mp_res->freebytes_remaining += block->size;

	/* Add this block to the list of free blocks. */
	insert_block_into_freelist(block, p_mp_res);
	
	traceFREE(p_mp_res, vir_addr, block->size);
	
	// 删除使用记录信息
	los_heap_use_out(p_mp_res, vir_addr, block->size);

	// 记录最大可用的连续内存块大小
	p_mp_res->max_free_blocksize = los_get_heap_max_freesize(p_mp_res);

	if (0 != mp_validate_freelist(p_mp_res, "free_post"))
	{
		mp_ret = MP_ADDRESS;
		goto exit;
	}

exit:
	if ((MP_OK == mp_ret) && (0 != mp_validate_freelist(p_mp_res, "free_exit_check")))
	{
		mp_ret = MP_ADDRESS;
	}
	mp_diag_record("free_exit", p_mp_res, block, NULL, NULL, size, 0, mp_ret);
	LEAVE_MTX(p_mp_res);
	return mp_ret;
}

MP_STATUS mp_free_all(void* handle)
{
	int32_t block_idx = 0;
	MP_STATUS mp_ret = MP_OK;
	A_MP_RES* p_mp_res = NULL;

	LOGD("mp_free_all start\r\n");

	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;

	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}

	//ENTER_MTX(p_mp_res);

	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used)
		{
			mp_ret = mp_free(handle, p_mp_res->heap_use[block_idx].addr, NULL, p_mp_res->heap_use[block_idx].size);
			if (MP_OK != mp_ret)
			{
				LOGE("heap %s free addr %p failed\r\n", p_mp_res->heap_info.name, p_mp_res->heap_use[block_idx].addr);
				//break;
			}
		}
	}

	// 主动进行内存整理
	mp_ret = mp_clean(handle);
	if (MP_OK != mp_ret)
	{
		LOGE("heap %s mp_clean failed while free all memory, ret %d\r\n", p_mp_res->heap_info.name, mp_ret);
	}

	//LEAVE_MTX(p_mp_res);

	return mp_ret;
}

MP_STATUS mp_get_phy_addr(void* handle, void* vir_addr, void** phy_addr)
{
	A_MP_RES* p_mp_res = NULL;
	uint8_t *pvc = NULL;

	int32_t block_idx = 0;
	MP_STATUS mp_ret = MP_OK;

	LOGD("mp_get_phy_addr vir_addr %p start\r\n", vir_addr);
	
	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}
	
	if (NULL == vir_addr/* || NULL == phy_addr*/)
	{		
		LOGE("heap %s free NULL addr\r\n", p_mp_res->heap_info.name);
		mp_ret =  MP_NULLPTR;
		goto exit;
	}

	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used && p_mp_res->heap_use[block_idx].addr == vir_addr)
		{
			break;
		}
	}

	if (block_idx == p_mp_res->heap_use_cnt)
	{
		LOGI("heap %s get vir_addr %p phy invalid on memory space\r\n", 
			p_mp_res->heap_info.name, vir_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	pvc = (uint8_t *)vir_addr;
	if ((p_mp_res->heap_info.end < (size_t)pvc)
		|| ((size_t)pvc < p_mp_res->heap_info.start))
	{
		LOGE("heap %s get vir_addr %p phy failed, it's invalid on memory space\r\n", 
			p_mp_res->heap_info.name, vir_addr);
		mp_ret = MP_ADDRESS;
		goto exit;
	}

	
*phy_addr = p_mp_res->heap_use[block_idx].phy;

	LOGD("mp_get_phy_addr vir_addr %p phy_addr %p done\r\n", vir_addr, *phy_addr);

exit:
	return mp_ret;	
}

MP_STATUS mp_get_ref_addr_by_name(void* handle, char* name, void** addr, void** phy, size_t* size)
{
	A_MP_RES* p_mp_res = NULL;
	int32_t block_idx = 0;

	p_mp_res = (A_MP_RES*)handle;

	LOGD("mp_get_ref_addr_by_name name %s start\r\n", name);

	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}
	
	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used
			&& !strcmp(name, p_mp_res->heap_use[block_idx].name)
			&& p_mp_res->heap_use[block_idx].bref)
		{
			*addr = p_mp_res->heap_use[block_idx].addr;
			*phy  = p_mp_res->heap_use[block_idx].phy;
			*size = p_mp_res->heap_use[block_idx].size;
			LOGD("mp_get_ref_addr_by_name name %s addr %p phy %p size %zu done\r\n", name, *addr, *phy, *size);
			return MP_OK;
		}
	}	

	return MP_ADDRESS;
}

MP_STATUS mp_deinit(void* handle)
{
	A_MP_RES* p_mp_res = NULL;
	MP_STATUS free_ret = MP_OK;
	void* vir_base = NULL;
	void* phy_base = NULL;
	uint32_t align = 0;
	MP_FREE free_cb = NULL;

	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}

	mp_diag_record("deinit_enter", p_mp_res, NULL, NULL, NULL, 0, 0, MP_OK);
		
	ENTER_MTX(p_mp_res);
	p_mp_res->initialised = 0;
	align = p_mp_res->heap_info.align;
	vir_base = p_mp_res->heap_info.vir_base;
	phy_base = p_mp_res->heap_info.phy_base;
	free_cb = p_mp_res->heap_info.free;
	LEAVE_MTX(p_mp_res);

	if (free_cb)
	{
		free_ret = free_cb(align, vir_base, phy_base);
	}

	if (MP_OK != free_ret)
	{
		LOGE("heap %s deinit payload free failed, ret %d\r\n", p_mp_res->heap_info.name, free_ret);
	}

	free(p_mp_res);
	return free_ret;
}

MP_STATUS mp_print(void* handle, char* prt_buf, uint32_t buf_len)
{
	A_BLOCK_LINK *p_block = NULL;
	A_MP_RES* p_mp_res = NULL;
	int32_t block_idx = 0;
	size_t free_total_size = 0;

	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;

	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\r\n");
		return MP_NOTINIT;
	}

	ENTER_MTX(p_mp_res);

	snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "-------- %s heap use info --------\r\n", p_mp_res->heap_info.name);

	snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), 
		"heap space %s total %zu free %zu max free %zu usage %.2f%% address range[%zu, %zu] mini free %zu mini fail %zu\r\n", 
		p_mp_res->heap_info.space,
		p_mp_res->heap_info.end - p_mp_res->heap_info.start - p_mp_res->heap_info.align, 
		p_mp_res->freebytes_remaining,
		p_mp_res->max_free_blocksize,
		(float)100.0f - ((float)p_mp_res->freebytes_remaining * 100.0f / (p_mp_res->heap_info.end - p_mp_res->heap_info.start - p_mp_res->heap_info.align)),
		p_mp_res->heap_info.start,
		p_mp_res->heap_info.end,
		p_mp_res->min_freebytes,
		p_mp_res->min_malloc_fail_size);

	// print free info
	p_block = &p_mp_res->start;
	snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "free start block addr %p size %zu padding size %zu\r\n", p_block, p_block->size, p_block->padding_size);

	block_idx = 0;
	p_block = p_block->pnext;
	while ((p_block != &p_mp_res->end) && (NULL != p_block))
	{
		snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "free block index %d addr %p size %zu padding size %zu\r\n", block_idx, p_block, p_block->size, p_block->padding_size);
		free_total_size += p_block->size;
		p_block = p_block->pnext;
		block_idx++;
	}

	if (NULL != p_block)
	{
		snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "free end block addr %p size %zu padding size %zu\r\n", p_block, p_block->size, p_block->padding_size);
	}
	else
	{
		snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "-------- heap free block list is broken!!!\r\n");
	}
	
	snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), 
	    "free block number %d, free total size %zu, max free size %zu\r\n", los_get_heap_fragment_number(p_mp_res), free_total_size, los_get_heap_max_freesize(p_mp_res));

	snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "\r\n");

	// print use info
	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used)
		{
			p_block = (A_BLOCK_LINK*)(*(size_t*)((size_t)p_mp_res->heap_use[block_idx].addr - sizeof(uint8_t *)));
			if (p_mp_res->heap_use[block_idx].bref)
			{
				snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "use block index %04d vir_addr %p phy_addr %p size %09zu ref_cnt %d ref_size[%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu] padding_size %03zu name %s\r\n", 
					block_idx, p_mp_res->heap_use[block_idx].addr, p_mp_res->heap_use[block_idx].phy, p_block->size, p_mp_res->heap_use[block_idx].ref_cnt, p_mp_res->heap_use[block_idx].ref_size[0], p_mp_res->heap_use[block_idx].ref_size[1], 
					p_mp_res->heap_use[block_idx].ref_size[2], p_mp_res->heap_use[block_idx].ref_size[3], p_mp_res->heap_use[block_idx].ref_size[4], p_mp_res->heap_use[block_idx].ref_size[5], p_mp_res->heap_use[block_idx].ref_size[6], 
					p_mp_res->heap_use[block_idx].ref_size[7], p_block->padding_size, p_mp_res->heap_use[block_idx].name);
			}
			else
			{
				snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "use block index %04d vir_addr %p phy_addr %p size %09zu padding_size %03zu name %s\r\n", 
					block_idx, p_mp_res->heap_use[block_idx].addr, p_mp_res->heap_use[block_idx].phy, p_block->size, p_block->padding_size, p_mp_res->heap_use[block_idx].name);				
			}
		}
	}

	snprintf(prt_buf + strlen(prt_buf), buf_len - strlen(prt_buf), "-------- %s info end --------\r\n", p_mp_res->heap_info.name);

	LEAVE_MTX(p_mp_res);

	return MP_OK;
}

MP_STATUS mp_clean(void* handle)
{
	MP_STATUS mp_ret = MP_OK;
	A_MP_RES* p_mp_res = NULL;

	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\n");
		return MP_NOTINIT;
	}

	ENTER_MTX(p_mp_res);
	mp_ret = los_heap_fragment_clean(p_mp_res, NULL, NULL);  // 默认从头开始整理
	LEAVE_MTX(p_mp_res);
	
	return mp_ret;
}

MP_STATUS mp_stat(void* handle, MP_STAT* stat)
{
	int32_t block_idx = 0, border_idx = 0;
	A_MP_RES* p_mp_res = NULL;
	uint8_t* p_border = NULL;
	
	if (NULL == handle || NULL == stat)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	
	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\n");
		return MP_NOTINIT;
	}

	stat->tatol_size = p_mp_res->heap_info.end - p_mp_res->heap_info.start - p_mp_res->heap_info.align;
	stat->free_size = p_mp_res->freebytes_remaining;
	stat->block_num = los_get_heap_fragment_number(p_mp_res);
	stat->max_block_size = los_get_heap_max_freesize(p_mp_res);
	
	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		if (p_mp_res->heap_use[block_idx].used)
		{
			p_border = (uint8_t*)(*(size_t*)((size_t)p_mp_res->heap_use[block_idx].addr - sizeof(uint8_t *) - sizeof(uint8_t *)));
			for (border_idx = 0; border_idx < MEM_BORDER_SIZE; border_idx++)
			{
				if (*p_border != MEM_BORDER_PADING
					&& stat->err_cnt < MP_STAT_ERR_NUM_MAX)
				{
					stat->err_addr[stat->err_cnt] = (size_t)p_mp_res->heap_use[block_idx].addr;
					snprintf(stat->err_name[stat->err_cnt], 32, "%s", p_mp_res->heap_use[block_idx].name);
					stat->err_cnt++;
					break;
				}
				p_border++;
			}
		}
	}

	return MP_OK;
}

MP_STATUS mp_statistics(void* handle, MP_STATISTICS *info)
{
	int block_idx = 0;
	uint32_t heap_use_cnt = 0;
	A_BLOCK_LINK *p_block = NULL;
	A_MP_RES* p_mp_res = NULL;
	
	if (NULL == handle || NULL == info)
	{
		return MP_NULLPTR;
	}
	
	p_mp_res = (A_MP_RES*)handle;

	if (!p_mp_res->initialised)
	{
		LOGE("mp hasn't init yet!\n");
		return MP_NOTINIT;
	}

	info->tatol_size = p_mp_res->heap_info.end - p_mp_res->heap_info.start - p_mp_res->heap_info.align;
	info->use_size   = info->tatol_size - p_mp_res->freebytes_remaining;

	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		info->bucket_use[block_idx].used = p_mp_res->heap_use[block_idx].used;
		if (p_mp_res->heap_use[block_idx].used)
		{
			p_block = (A_BLOCK_LINK*)(*(size_t*)((size_t)p_mp_res->heap_use[block_idx].addr - sizeof(uint8_t *)));
			snprintf(info->bucket_use[block_idx].name, 32, "%s", p_mp_res->heap_use[block_idx].name);
			info->bucket_use[block_idx].size = p_block->size;
			heap_use_cnt++;
		}
	}
	info->bucket_use_cnt = heap_use_cnt;

	return MP_OK;
}

void mp_snapshot(void* handle, int32_t en)
{
	A_MP_RES* p_mp_res = NULL;
	
	if (NULL == handle)
	{
		return;
	}

	p_mp_res = (A_MP_RES*)handle;
	p_mp_res->snapshot_en = en;
}

int32_t mp_get_total_size(void* handle, size_t *size)
{
	A_MP_RES* p_mp_res = NULL;
	
	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	*size = p_mp_res->heap_info.end - p_mp_res->heap_info.start - p_mp_res->heap_info.align;

	return MP_OK;
}

int32_t mp_get_use_size(void* handle, size_t *size)
{
	A_MP_RES* p_mp_res = NULL;
	
	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	*size = p_mp_res->heap_info.end - p_mp_res->heap_info.start - p_mp_res->heap_info.align - p_mp_res->freebytes_remaining;

	return MP_OK;	
}

int32_t mp_get_free_size(void* handle, size_t *size)
{
	A_MP_RES* p_mp_res = NULL;
	
	if (NULL == handle)
	{
		return MP_NULLPTR;
	}

	p_mp_res = (A_MP_RES*)handle;
	*size = p_mp_res->freebytes_remaining;

	return MP_OK;	
}

int32_t mp_addr_check(void* handle, void* vir_addr, size_t size)
{
	A_MP_RES* p_mp_res = NULL;
	HEAP_USE* p_heap = NULL;
	
	int block_idx = 0;
	MP_STATUS mp_ret = MP_OK;
	
	if (NULL == handle || NULL == vir_addr)
	{
		return MP_NULLPTR;
	}
	
	p_mp_res = (A_MP_RES*)handle;

	if (((size_t)vir_addr >= p_mp_res->heap_info.end)
		|| (((size_t)vir_addr + size) <= (size_t)p_mp_res->heap_info.vir_base))
	{
		// 不在内存池地址范围内，返回OK
		return MP_OK;
	}

	for (block_idx = 0; block_idx < p_mp_res->heap_use_cnt; block_idx++)
	{
		p_heap = &p_mp_res->heap_use[block_idx];
		if ((size_t)p_heap->addr <= (size_t)vir_addr
			&& (size_t)p_heap->addr + p_heap->size >= (size_t)vir_addr)
		{
			if ((size_t)p_heap->addr + p_heap->size >= (size_t)vir_addr + size)
			{
				return MP_OK;
			}
			else
			{
				LOGE("ERROR!!! mp addr %p name %s check over range!\r\n", vir_addr, p_heap->name);
				mp_ret = MP_OVERRANGE;
				assert(MP_OK == mp_ret); 
				return mp_ret;
			}
		}
	}	
	return MP_OVERRANGE;	
}

uint32_t mp_get_pool_size()
{
	return sizeof(A_MP_RES) + (sizeof(HEAP_USE) * MAX_HEAP_USE_CNT);
}
