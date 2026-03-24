# FTP 传图触发内存池崩溃复盘报告（可分享版）

  ## 1. 事件概览

  问题现象
  系统在 mp_malloc 前置检查阶段崩溃，free block 的链表头字段被污染，表现为：

  - next = 0x48484800474747（可见 GGG/HHH 字节模式）
  - size = 21475987536169803（明显非正常大小）

  影响范围
  影响 FTP 传图链路（尤其 BMP 编码路径），最终导致内存池 free list 损坏，分配流程异常退出。

  ———

  ## 2. 内存池架构设计（为什么拆分 OS / MMZ）

  我们把内存池设计成两层：

  1. 控制面（OS 内存）
     A_MP_RES、HEAP_USE、锁、统计信息、诊断 ring buffer 等元数据，都放 OS 内存（calloc 分配）。
  2. 数据面（MMZ 或 OS 大块）
     实际图像/业务大块 buffer 放 MMZ（或按配置走 OS），用于硬件/大吞吐数据传输。

  ### 2.1 这样拆分的原因

  1. 职责隔离
     元数据是“控制结构”，业务数据是“高风险写入区”，分开后更容易定位问题。
  2. 降低耦合风险
     即使业务 buffer 发生越界，至少不会直接踩坏 A_MP_RES 本体（控制结构仍在 OS 区）。
  3. 更适配 MMZ 的使用场景
     MMZ 适合 DMA/图像大块数据，不适合承载频繁变动的小控制对象。
  4. 诊断更稳定
     崩溃时仍能依赖 OS 区的诊断结构（事件 ring、统计、dump）输出证据。
  5. 生命周期更清晰
     元数据先建后用，数据区可独立释放，反初始化路径更可控。

  ———

  ## 3. 关键证据与定位线索

  ### 3.1 地址关系证据（最关键）

  从现场日志：

  - ftpmsg.convert_img 地址：0x7f2ad3f560
  - 损坏 block 地址：0x7f2ae9f568
  - 差值：0x160008 = 1441800

  这个差值恰好等于该块 size，说明被污染的是 convert_img 后面的相邻块头，不是随机地址。

  ### 3.2 时序证据

  mp_diag 显示：

  - free_exit 时链表仍正常
  - 到下一次 malloc_pre 时出现 span_bad / next_oob

  说明是“free 后、下次 malloc 前”发生了异步/延迟暴露型内存破坏。

  ### 3.3 字节模式证据

  0x47/0x48/... 与 BMP 处理路径中的字节模式高度相关，结合问题路径收敛到 makeIstreamByFormat -> mono8_2_bmp 一线。

  ———

  ## 4. 排查过程（逐步收敛）

  ## 4.1 第一步：先确认“崩溃点不是根因点”

  虽然崩在 malloc，但我们先假设崩溃点只是“受害者”。
  做法：为 malloc/free/realloc/insert 全链路加事件序列号和上下文记录（seq/op/block/prev/next/size/ret）。

  结论：受害点在 malloc_pre，根因在更早步骤。

  ## 4.2 第二步：建立可复现数据证据链（mp_diag）

  新增 mp_diag ring buffer，记录最近 N 条操作。每次校验失败自动 dump：

  - handle
  - heap range
  - start/end
  - 最近事件窗口

  这样不用依赖一次性日志，能回放崩溃前 200+ 步链路变化。

  ## 4.3 第三步：从内存池回溯到业务路径

  沿着“被污染地址”反推归属 buffer，发现与 ftpmsg.convert_img 相邻。
  继续追业务代码，锁定 FtpClientManager::makeIstreamByFormat。

  ## 4.4 第四步：发现触发条件（重试语义 + 可变字段）

  关键点：

  1. BMP 分支把 pParam->usedLen 改成了 BMP 输出长度。
  2. FTP 上传失败会抛异常，processQueue 在异常路径不会 sfifo_out_skip。
  3. 同一 FIFO 项重试时，usedLen 已被污染，不再是原始图长度。
  4. 下一次进入 BMP/mono 拷贝路径，memcpy(..., usedLen) 可能超过 convert_img 原始容量，导致写穿。

  这一步是根因确认。

  ———

  ## 5. 修复策略与代码实现

  ## 5.1 根因修复（最小必要改动）

  原则：输入参数不可变（特别是队列重试对象）

  在 BMP 分支不再写回 pParam->usedLen，改为局部变量：

  uint32_t bmpLen = strBmpEncParam.output_len;
  auto src = std::make_unique<std::stringstream>();
  src->write((char*)pParam->image.data[0], bmpLen);
  return std::move(src);

  ## 5.2 防御性修复（防止同类回归）

  ### A. FTP/BMP 入口参数防线

  - 校验 pParam、image.data[0]、usedLen
  - 校验输出地址是否在 store_buf 白名单范围
  - 校验 width/height/format 与 usedLen 一致性
  - 对 mono/rgb 分支分别计算 mono_need/color_need

  ### B. mono8_2_bmp 内部防线

  新增：

  - input_data/output_data != NULL
  - image_pixel_bit 只允许合法值
  - input_len >= width * height * pixel_bit
  - 输出缓冲区长度检查保持不变

  ### C. mpool 防线

  1. mp_validate_allocated_block
     在 free/realloc/reduce 回收路径中校验：
      - block 头合法
      - block + padding == user_ptr
      - border 未破坏
  2. free list 插入失败回滚
     避免坏节点先挂链后返回，导致延迟炸链。

  ———

  ## 6. 诊断增强（可分享重点）

  ## 6.1 mp_diag 方法

  核心思路：把“单点崩溃”变成“可回放链路”。

  建议固定字段：

  - seq
  - ts_ms
  - op
  - handle
  - block
  - prev/next
  - size
  - ret

  并在校验失败时自动 dump 最近 ring 内容。

  ## 6.2 错误地址上下 1024 字节 dump（强烈推荐）

  在 suspect 地址处做窗口 dump（建议 [-1024, +1024)，带边界裁剪）：

  - 可直接看到被覆盖模式（如 47/48/49...）
  - 能判断是结构化写入还是随机踩内存
  - 对映射“谁写坏了谁”非常有效

  可复用实现要点：

  1. start = max(heap_start, suspect - 1024)
  2. end = min(heap_end, suspect + 1024)
  3. 每行 16 字节 hex + ASCII 双视图输出
  4. dump 文件名包含 seq/ts/suspect

  ———

  ## 7. 验证结果

  1. 根因路径修复后，不再出现“重试导致 usedLen 扩大再 memcpy 写穿”。
  2. 新增防线可提前拦截参数失真，不再等到 free list 被污染才暴露。
  3. 即使再出问题，mp_diag + window dump 能快速定位到“首次失真点”。

  ———

  ## 8. 经验总结（可直接放分享页）

  1. 崩溃点通常不是根因点，要追“首次失真”。
  2. 重试队列对象必须只读，绝不在处理过程中改输入语义字段。
  3. 参数一致性要双向校验：len、width、height、format 必须互相约束。
  4. 内存池要有自校验能力：回收前校验、链表插入回滚、异常快照。
  5. 诊断能力要前置建设：ring 事件 + 局部 hexdump，比事后猜测高效太多。
  6. 控制面与数据面分离（OS vs MMZ）是长期收益设计，不只是“能跑就行”。

  ———

  ## 9. 踩坑清单

  1. 在业务函数中修改 pParam->usedLen（输入污染）。
  2. 忽略“异常路径不出队”的重试语义。
  3. 仅检查 output buf，不检查 input len。
  4. free list 插入先改链后校验，不回滚。
  5. 只看崩溃栈，不做地址差值分析。
