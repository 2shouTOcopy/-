这个崩溃，大概率不是 los_find_min_block_4_malloc() 自己算错了，而是：

空闲链表里的某个 A_BLOCK_LINK->pnext 已经被别的代码踩坏了。

你现在看到的：

p_block = 0x4f4f4e474c4b4d4c

这种值非常像被业务数据覆盖后的假指针，不像正常地址。

把它按字节看，像是 ASCII 字符串/普通数据，而不是有效虚拟地址。说明更接近下面几类问题：
	1.	用户写越界，把空闲块头 A_BLOCK_LINK 覆盖了
	2.	释放后继续写，把已经回收到 free list 的块头覆盖了
	3.	free list 合并/拆分逻辑有 bug，把 pnext/size/padding_size 写坏
	4.	多线程并发访问内存池，链表修改时没有完全加锁，导致链表断裂或脏写
	5.	传入的 handle/heap 区域本身被踩坏

⸻

一、先明确这段代码最危险的点

你当前遍历代码：

p_pre_block = &p_mp_res->start;
p_block = p_mp_res->start.pnext;
while (p_block->pnext != NULL)
{
    ...
    p_pre_block = p_block;
    p_block = p_block->pnext;
}

这里默认假设：
	•	p_block 一定非空
	•	p_block 一定指向一个合法 A_BLOCK_LINK
	•	p_block->pnext 可读
	•	整个 free list 没被破坏

但实际上只要链表某个节点头部被覆盖，p_block->pnext 一解引用就崩。

所以第一步不是盲查业务，而是先把内存池自身做成“自校验”模式。

⸻

二、最优先的排查方向

1）先怀疑 free block 头被用户数据覆盖

根据你给的布局图，空闲块头里有：
	•	A_BLOCK_LINK
	•	border/padding
	•	start addr
	•	user malloc memory

如果某块已经 free 回链表，那么这块内存前部会被重新解释成 A_BLOCK_LINK。

此时如果还有人拿着旧指针继续写这块 user memory，很容易把链表头覆盖掉。

也就是典型的：
	•	malloc -> 使用 -> free
	•	但是业务层还留着旧指针
	•	后续继续 memcpy/snprintf/fill image
	•	把这块已经回收到 free list 的块头踩坏
	•	下一次 malloc 遍历 free list 时炸掉

这个场景和你现在的现象非常吻合。

⸻

2）再怀疑用户越界写，覆盖到“下一块”的块头

你这个池子布局明显是连续块管理。

如果某个块用户申请了 N 字节，但实际写了 N+K，就会覆盖：
	•	当前块尾部 border
	•	下一个块的 A_BLOCK_LINK
	•	或者 HEAP_USE 记录关联信息

而你图里已经有 border padding（0x5A），说明设计者本来就有意图做越界检测，只是现在可能检测时机不够早，或者只在 free 时检查，没有在 malloc 前检查。

⸻

3）也要查并发

只要存在：
	•	一个线程 mp_malloc
	•	一个线程 mp_free
	•	或一个线程快照扫描，另一个线程分配/释放

且中间有路径没加锁，free list 很容易坏。

要重点确认：
	•	mp_malloc 从进入到摘链、分裂、更新 heap_used_cnt/freebytes_remaining/max_free_blocksize 这整个过程是否全程加锁
	•	mp_free 合并前后是否全程加锁
	•	所有 heap_use[] 修改是否同一把锁保护
	•	是否有“调用者已经加锁，所以内部不加锁”的双路径
	•	是否有中断/回调里也访问这个池

⸻

三、先做最小侵入式加固，不需要单独创建新文件

这类排查不需要单独创建文件。
建议直接写在：
	•	mpool.c：校验函数、调试日志、毒化逻辑
	•	mpool.h：如果要开关宏，再放少量宏声明

⸻

四、第一步：给 free list 遍历加“合法性检查”

在 los_find_min_block_4_malloc() 里，先不要直接 while (p_block->pnext != NULL)，要先验块头合法性。

建议增加块合法性检查函数

static int mp_is_valid_block_addr(A_MP_RES *res, const A_BLOCK_LINK *blk)
{
    size_t addr;
    size_t start;
    size_t end;

    if (res == NULL || blk == NULL)
        return 0;

    addr = (size_t)blk;
    start = res->heap_info.start;
    end   = res->heap_info.end;

    if (addr < start || addr + sizeof(A_BLOCK_LINK) > end)
        return 0;

    /* 块头至少按 sizeof(void*) 对齐 */
    if ((addr & (sizeof(void*) - 1)) != 0)
        return 0;

    return 1;
}

static int mp_is_reasonable_block(A_MP_RES *res, const A_BLOCK_LINK *blk)
{
    if (!mp_is_valid_block_addr(res, blk))
        return 0;

    /* size 不应超过整个池大小 */
    if (blk->size > (res->heap_info.end - res->heap_info.start))
        return 0;

    /* padding_size 也不该离谱 */
    if (blk->padding_size > 4096)   /* 这里阈值可按你们实际对齐策略调整 */
        return 0;

    /* next 指针要么为空，要么也必须落在池内 */
    if (blk->pnext != NULL && !mp_is_valid_block_addr(res, blk->pnext))
        return 0;

    return 1;
}


⸻

改造遍历逻辑

static A_BLOCK_LINK* los_find_min_block_4_malloc(void* handle, size_t size,
                                                 uint32_t align, A_BLOCK_LINK** pp_pre_block)
{
    A_BLOCK_LINK *p_block = NULL, *p_min_block = NULL, *p_pre_block = NULL;
    A_MP_RES* p_mp_res = NULL;
    size_t min_size = 0;
    uint32_t align_size = 0;
    int guard_cnt = 0;

    if (handle == NULL || pp_pre_block == NULL)
    {
        LOGE("los_find_min_block_4_malloc invalid arg\r\n");
        return NULL;
    }

    p_mp_res = (A_MP_RES*)handle;
    min_size = p_mp_res->heap_info.end - p_mp_res->heap_info.start - p_mp_res->heap_info.align;

    p_pre_block = &p_mp_res->start;
    p_block = p_mp_res->start.pnext;

    while (p_block != NULL)
    {
        if (!mp_is_reasonable_block(p_mp_res, p_block))
        {
            LOGE("free list corrupted: block=%p pre=%p size=%zu padding=%zu next=%p\r\n",
                 p_block,
                 p_pre_block,
                 mp_is_valid_block_addr(p_mp_res, p_block) ? p_block->size : 0UL,
                 mp_is_valid_block_addr(p_mp_res, p_block) ? p_block->padding_size : 0UL,
                 mp_is_valid_block_addr(p_mp_res, p_block) ? p_block->pnext : NULL);

            mp_dump_heap_state(p_mp_res); /* 后面会给 */
            return NULL;
        }

        align_size = los_block_align_size(p_block, align);
        if (align_size >= size)
        {
            if (min_size >= align_size)
            {
                p_min_block = p_block;
                *pp_pre_block = p_pre_block;
                min_size = align_size;
            }
        }

        p_pre_block = p_block;
        p_block = p_block->pnext;

        /* 防止环链表死循环 */
        if (++guard_cnt > 100000)
        {
            LOGE("free list loop detected\r\n");
            mp_dump_heap_state(p_mp_res);
            return NULL;
        }
    }

    return p_min_block;
}

这一步的价值非常大：
	•	不再“随机崩”
	•	一旦链表坏了，能打印出第一个坏块
	•	能知道坏的是当前块，还是 next 指针

⸻

五、第二步：加“堆一致性检查”，在 malloc/free 前后都扫

这是最关键的办法之一。

增加一个全堆扫描函数

static void mp_dump_block(const char *tag, A_BLOCK_LINK *blk)
{
    if (blk == NULL)
    {
        LOGE("[%s] blk=NULL\r\n", tag);
        return;
    }

    LOGE("[%s] blk=%p next=%p size=%zu padding=%zu\r\n",
         tag, blk, blk->pnext, blk->size, blk->padding_size);
}

static int mp_check_free_list(A_MP_RES *res)
{
    A_BLOCK_LINK *slow, *fast;
    A_BLOCK_LINK *cur;
    size_t total = 0;
    int cnt = 0;

    if (res == NULL)
        return -1;

    /* 1. Floyd 判环 */
    slow = res->start.pnext;
    fast = res->start.pnext;
    while (fast && fast->pnext)
    {
        if (!mp_is_reasonable_block(res, slow) || !mp_is_reasonable_block(res, fast))
        {
            LOGE("free list invalid during cycle check\r\n");
            return -2;
        }

        slow = slow->pnext;
        fast = fast->pnext->pnext;

        if (slow == fast)
        {
            LOGE("free list has cycle, node=%p\r\n", slow);
            return -3;
        }
    }

    /* 2. 顺序扫描 */
    cur = res->start.pnext;
    while (cur)
    {
        if (!mp_is_reasonable_block(res, cur))
        {
            LOGE("free list corrupted at %p\r\n", cur);
            return -4;
        }

        total += cur->size + cur->padding_size;
        cnt++;

        if (cur->pnext == &res->end)
            break;

        cur = cur->pnext;

        if (cnt > 100000)
        {
            LOGE("free list too long, maybe loop\r\n");
            return -5;
        }
    }

    return 0;
}


⸻

在这些地方调用

建议在调试版本里加：
	•	mp_malloc() 入口
	•	mp_malloc() 摘链前
	•	mp_malloc() 分裂后
	•	mp_free() 入口
	•	mp_free() 插链/合并后

比如：

#ifdef MPOOL_DEBUG
#define MP_CHECK_HEAP(res, where) \
    do { \
        int __ret = mp_check_free_list((res)); \
        if (__ret != 0) { \
            LOGE("MP_CHECK_HEAP failed at %s ret=%d\r\n", (where), __ret); \
            mp_dump_heap_state((res)); \
        } \
    } while (0)
#else
#define MP_CHECK_HEAP(res, where) do {} while (0)
#endif

然后：

MP_CHECK_HEAP(p_mp_res, "mp_malloc enter");
...
MP_CHECK_HEAP(p_mp_res, "mp_malloc before find");
...
MP_CHECK_HEAP(p_mp_res, "mp_malloc after split");

这样你能快速定位：
	•	是在 free 后坏
	•	还是在 malloc 分裂后坏
	•	还是进入 malloc 前就已经坏了

⸻

六、第三步：把“谁最后动过这块内存”记录下来

你现在没有 asan/valgrind，就必须自己造一个轻量版。

方案：给每个块的头尾加调试信息

你图里已经有：
	•	border addr
	•	start addr
	•	border padding(0x5A)

那就继续强化：

建议在用户可见内存前面增加一个 debug header

不一定改单独文件，直接在 mpool.c 里加。

typedef struct _MP_DEBUG_HDR
{
    uint32_t magic1;
    uint32_t req_size;
    uint32_t align;
    uint32_t line;
    const char *file;
    const char *func;
    uint32_t alloc_seq;
    uint32_t freed;
} MP_DEBUG_HDR;

#define MP_DEBUG_MAGIC1 0x4D504844u  /* MPHD */
#define MP_DEBUG_MAGIC2 0x44504D54u  /* DPMT */

尾部再放一个 trailer：

typedef struct _MP_DEBUG_TAIL
{
    uint32_t magic2;
} MP_DEBUG_TAIL;

这样在 free 的时候就能检查：
	•	头 magic 是否还在
	•	尾 magic 是否还在
	•	是否 double free
	•	这块是谁申请的

⸻

包装宏

void* mp_malloc_dbg(void *handle, size_t size, uint32_t align, int bref,
                    void **vir_addr, void **phy_addr, const char *name,
                    const char *file, int line, const char *func);

#define mp_malloc_ex(handle, size, align, bref, vir_addr, phy_addr, name) \
    mp_malloc_dbg(handle, size, align, bref, vir_addr, phy_addr, name, __FILE__, __LINE__, __func__)

这样一旦块头坏了，日志里直接能看到：
	•	哪个文件
	•	哪一行
	•	哪次序号
	•	块大小
	•	是否已经 free

⸻

七、第四步：free 后立刻毒化，抓 use-after-free

这个非常实用。

free 时把用户区全部填固定字节

#define MP_FREE_POISON 0xDD
#define MP_ALLOC_POISON 0xCD

static void mp_poison_memory(void *ptr, size_t size, uint8_t val)
{
    if (ptr && size)
        memset(ptr, val, size);
}

	•	malloc 返回前填 0xCD
	•	free 时填 0xDD

如果业务逻辑里还能看到大片 0xDD 被改掉，说明释放后仍有人写

⸻

更进一步：free 后先不立即回收到 free list

如果内存够，可以做个“小隔离队列”。

思路

释放时先放入 quarantine 队列，过几轮再真正并回 free list。

这样可以更容易发现 use-after-free。

如果一 free 马上回收，旧指针写入就会直接破坏 free list；
如果隔离一段时间，至少你还能在检查时发现“已释放块内容被改”。

这个改动稍大，但效果很好。

⸻

八、第五步：检查 border，不只在 free 时查，在 malloc 搜索前也查

你图里块尾有：

border padding (0x5A)

那就说明可以做块边界哨兵检测。

建议增加函数

static int mp_check_block_border(uint8_t *border, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i)
    {
        if (border[i] != 0x5A)
            return -1;
    }
    return 0;
}

然后在：
	•	mp_free
	•	mp_malloc 前全池扫描
	•	snapshot/dump

都检查一次。

如果发现某个已分配块的尾 border 被破坏，就说明：

越界写已经发生，但还没等到 free。

这比只在 free 时查更早。

⸻

九、第六步：做一个 heap_use[] 和 free list 的双向核对

你的池里已经有：

HEAP_USE* heap_use;
uint32_t heap_use_cnt;
uint32_t heap_used_cnt;

这其实很有价值。

建议做一个调试函数，把：
	•	所有 heap_use[i].used == 1 的块地址、大小
	•	free list 里所有块地址、大小

统一打印并核对：
	1.	是否有区间重叠
	2.	是否有块同时出现在 used 和 free
	3.	是否有块范围跑出池边界
	4.	所有块总和是否约等于池总容量

⸻

示例检查思路

static int mp_check_overlap(size_t s1, size_t e1, size_t s2, size_t e2)
{
    return !(e1 <= s2 || e2 <= s1);
}

把 used 块和 free 块都变成区间，双重循环检查。
虽然 O(n²)，但调试阶段完全值得。

⸻

十、第七步：重点排查 malloc/free/merge/split 逻辑

虽然你现在给的是 find_min_block，但真正把链表写坏的常常在这些地方：

1）分裂块时

比如从一个 free block 切出一部分给用户后：
	•	剩余块地址是否正确
	•	剩余块 size/padding_size/pnext 是否正确
	•	是否覆盖了自己的头

2）回收块时

插回 free list：
	•	插入位置是否按地址/大小策略正确
	•	前后合并时是否把 pnext 串丢
	•	合并后 size 是否加重了或漏加 padding/header

3）释放已释放块

double free 很容易把 free list 搞环或者重复节点。

所以 mp_free 一定要查：
	•	heap_use 状态是否已经是未使用
	•	debug header 里的 freed 标志是否已经置位

⸻

十一、第八步：GDB 的高效抓法

虽然你说 asan/valgrind 不支持，但 GDB 仍然能用。

⸻

1）先在崩溃点确认坏块地址附近的原始内存

在崩溃现场：

x/16gx p_pre_block
x/16gx p_block

如果 p_block 根本不是合法地址，就看 p_pre_block->pnext 是什么时候变成这个值的。

⸻

2）对“上一个合法块的 pnext 字段”下硬件观察点

假设你通过日志定位到某次崩之前：
	•	prev 是合法的
	•	prev->pnext 本应指向某个块
	•	后来变成了 0x4f4f4e474c4b4d4c

那就在下次运行时对它下 watchpoint：

watch *((void**)&prev->pnext)

或者：

watch prev->pnext

谁改它，现场就能停住。

这比等崩溃强得多。

⸻

3）如果坏的是整个块头

对 A_BLOCK_LINK 整个区域下观察点：

watch *(long long*)blk
watch *((long long*)blk + 1)
watch *((long long*)blk + 2)

因为一个块头通常 24 字节左右（64 位下）。

⸻

十二、第九步：从业务层查最像越界的调用

你这次申请的是：

size=1441800
name="6charhorizonal.jpg_img"

这很像图像缓冲区。

图像类内存最容易出的问题：
	1.	宽高行跨度算错，memcpy(dst, src, w*h*ch) 超出
	2.	stride 和 width 混用
	3.	mono/rgb/bgr 格式切换时长度算错
	4.	ROI 拷贝时起始地址对，长度不对
	5.	对齐后缓冲区比“逻辑图像大小”小，但仍按未对齐长度写
	6.	JPEG 解码输出大小估算错误
	7.	文件名、标识字符串写进了块头附近的 metadata

尤其你这个坏指针长得像 ASCII 数据，建议重点 grep：
	•	memcpy
	•	memmove
	•	memset
	•	snprintf
	•	strcpy/strncpy
	•	图像拷贝/旋转/缩放/格式转换函数

重点关注目标地址是否可能落在已释放块或者长度是否覆盖到相邻块头。

⸻

十三、第十步：建议马上加一套“最后 N 次分配/释放日志”

这个特别有用，而且代价很小。

不需要单独文件，直接加在 mpool.c

#define MP_TRACE_CNT 256

typedef struct _MP_TRACE_NODE
{
    uint32_t seq;
    char op;            /* M/F */
    void *user_ptr;
    void *block_ptr;
    size_t req_size;
    size_t real_size;
    const char *file;
    const char *func;
    int line;
    char name[32];
} MP_TRACE_NODE;

static MP_TRACE_NODE g_mp_trace[MP_TRACE_CNT];
static uint32_t g_mp_trace_seq = 0;

记录函数：

static void mp_trace_record(char op, void *user_ptr, void *block_ptr,
                            size_t req_size, size_t real_size,
                            const char *file, int line, const char *func,
                            const char *name)
{
    uint32_t idx = g_mp_trace_seq % MP_TRACE_CNT;
    MP_TRACE_NODE *n = &g_mp_trace[idx];

    memset(n, 0, sizeof(*n));
    n->seq = ++g_mp_trace_seq;
    n->op = op;
    n->user_ptr = user_ptr;
    n->block_ptr = block_ptr;
    n->req_size = req_size;
    n->real_size = real_size;
    n->file = file;
    n->func = func;
    n->line = line;
    if (name)
        snprintf(n->name, sizeof(n->name), "%s", name);
}

崩溃时 dump 最近 256 条，通常就能看到：
	•	某块刚 free
	•	紧接着某业务还在写
	•	或某次 split 后块地址异常

⸻

十四、你现在最该优先落地的 6 个动作

按收益排序，我建议你这样做：

1. 给 los_find_min_block_4_malloc() 加块合法性校验

避免直接野指针解引用。

2. 增加 mp_check_free_list()，在 malloc/free 前后都调用

快速定位“从哪一步开始坏”。

3. free 后毒化用户区

抓 use-after-free。

4. 增加头尾 magic/border 校验

抓越界写。

5. 记录最近 N 次 alloc/free

定位最后操作者。

6. 用 GDB watchpoint 盯住被改坏的 pnext

抓现行。

⸻

十五、给你一个实际判断结论

结合你给的信息，我认为概率最高的是：

第一嫌疑：释放后继续写

因为 free block 的头就是 A_BLOCK_LINK，一旦业务还持有旧地址写图像数据，free list 会最先坏。

第二嫌疑：某次图像缓冲区越界写

因为申请 size 很大，又是 image 名字，图像类最容易长度算错。

第三嫌疑：free/malloc 链表操作缺锁或并发写

如果这个内存池是跨线程公用，一定要查锁。

⸻

十六、最后给你一版非常实用的排查策略

你可以直接按这个顺序执行：
	1.	在 mp_malloc/mp_free 内部入口和出口加 MP_CHECK_HEAP
	2.	在 los_find_min_block_4_malloc 遍历前校验 p_block
	3.	在 mp_free 时检查头尾 magic 和 border
	4.	free 后填 0xDD
	5.	记录最近 256 次分配释放
	6.	一旦日志显示某个块头被破坏，记下它前驱块地址
	7.	下次复现时对 prev->pnext 下 watchpoint
	8.	回头排查最近操作这个块的图像处理路径

⸻

如果你愿意，我下一条可以直接给你一份可落地的 C 版调试补丁，包括：
	•	mp_check_free_list
	•	mp_dump_heap_state
	•	mp_malloc_dbg/mp_free_dbg
	•	border/magic 校验
	•	poison 和 trace ring buffer

我会按你现在这个 mpool.c 风格来写，并标明哪些代码写到 mpool.c，哪些声明写到 mpool.h。