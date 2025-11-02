#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <deque>
#include <functional>
#include <mutex>

// -------------------- 项目通用定义 --------------------
#ifndef SCHEDULED_TRANS_NG_STRING
#define SCHEDULED_TRANS_NG_STRING "NG"
#endif

#ifndef SCHEDULED_SEND_QUEUE_LEN
#define SCHEDULED_SEND_QUEUE_LEN 8
#endif

#ifndef MAX_SCHEDULED_PAYLOAD_LEN
#define MAX_SCHEDULED_PAYLOAD_LEN 1280
#endif

// 统一的待发结构体
typedef struct  {
    uint32_t len;
    uint8_t  buf[MAX_SCHEDULED_PAYLOAD_LEN];
} scheduled_fifo_param;

// 触发参数键（与项目一致）
#ifndef TRIGGER_COUNT
#define TRIGGER_COUNT 1001
#endif
#ifndef TRIGGER_IMG_TIME
#define TRIGGER_IMG_TIME 1002
#endif
#ifndef TRIGGER_MODE_PARAM
#define TRIGGER_MODE_PARAM 1003
#endif

// 输出模式（全局配置 PM_OUTPUT_MODE）
enum class OutputMode : uint8_t {
    kImmediate = 0, // 立即输出（无触发门控）
    kTimed     = 1, // 定时输出（需触发模式）
    kEncoder   = 2  // 编码器输出（需触发模式）
};

// -------------------- 外部依赖接口 --------------------
namespace mvsc_idr_app {
struct ITriggerSource {
    static ITriggerSource* getComponent();
    virtual int GetParam(int key, int& val) = 0;          // TRIGGER_COUNT/TRIGGER_MODE_PARAM
    virtual int GetParam(int key, std::string& val) = 0;  // TRIGGER_IMG_TIME（字符串毫秒）
    virtual ~ITriggerSource() = default;
};
}

// 编码器状态提供者
struct IEncoderStatusProvider {
    virtual int getStatus() = 0; // 返回1表示“该时刻需要发送”
    virtual ~IEncoderStatusProvider() = default;
};

// 时间函数：毫秒/微秒
using NowMsFn = uint64_t(*)();    // 如：get_up_time() 返回毫秒
using SleepUsFn = void(*)(uint32_t); // 如：usleep

// -------------------- 队列适配层 --------------------
// 统一出队接口：支持 simple_fifo 与 std::deque 两种实现
class ISchedQueue {
public:
    virtual ~ISchedQueue() = default;
    // 取一条并消费，成功返回 true
    virtual bool pop_one(scheduled_fifo_param& out) = 0;
    // 清空并逐条发送（用于立即模式批量发送）
    virtual void drain_all(const std::function<void(const scheduled_fifo_param&)>& send) = 0;
};

// simple_fifo 适配（Modbus）
struct simple_fifo;
class SimpleFifoAdapter : public ISchedQueue {
public:
    explicit SimpleFifoAdapter(simple_fifo* q): q_(q) {}
    bool pop_one(scheduled_fifo_param& out) override;   // 内部：sfifo_out + sfifo_drain
    void drain_all(const std::function<void(const scheduled_fifo_param&)>& send) override;
private:
    simple_fifo* q_;
};

// std::deque 适配（TCP/UDP 改造为统一结构体）
class DequeAdapter : public ISchedQueue {
public:
    explicit DequeAdapter(std::deque<scheduled_fifo_param>* dq, std::mutex* mtx)
        : dq_(dq), mtx_(mtx) {}
    bool pop_one(scheduled_fifo_param& out) override;
    void drain_all(const std::function<void(const scheduled_fifo_param&)>& send) override;
    // 供生产者调用：入队，满则丢旧
    void push_drop_old(const scheduled_fifo_param& in);
private:
    std::deque<scheduled_fifo_param>* dq_;
    std::mutex* mtx_;
};

// -------------------- 引擎上下文 --------------------
struct OutputContext {
    ISchedQueue* queue = nullptr;      // 队列适配器
    mvsc_idr_app::ITriggerSource* trig = nullptr; // 触发源
    IEncoderStatusProvider* encoder = nullptr;    // 编码器（编码器模式使用）
    NowMsFn  nowMs = nullptr;          // 毫秒
    SleepUsFn sleepUs = nullptr;       // 微秒
    // 发送函数：按算子注入（modbus_send_result / TCP Convert+Insert）
    std::function<void(const scheduled_fifo_param&)> send;
    // 配置（可动态更新）
    OutputMode mode = OutputMode::kImmediate;
    uint32_t   scheduledIntervalMs = 0; // 定时发送间隔（毫秒）
    const char* ngText = SCHEDULED_TRANS_NG_STRING;
    // 日志
    std::function<void(const char* fmt, ...)> loge; // 可注入 LOGE
};

// -------------------- 引擎 --------------------
class OutputModeEngine {
public:
    explicit OutputModeEngine(const OutputContext& ctx);
    void setConfig(OutputMode mode, uint32_t scheduledIntervalMs); // 运行中可更新
    void tick(); // 在算子线程里轮询调用

private:
    // 内部状态
    OutputContext ctx_;
    bool inTriggerMode_ = false;          // 当前是否处于触发模式
    int  lastTrigCount_ = -1;             // 最近一次已处理的 TRIGGER_COUNT
    uint64_t lastImgMs_ = 0;              // 最近一次出图时间（毫秒）
    // 工具
    bool fetchTriggerMode_();
    bool fetchTrigCount_(int& out);
    bool fetchTrigImgTimeMs_(uint64_t& out);
    void sleepDefault_() { if (ctx_.sleepUs) ctx_.sleepUs(5000); } // 5ms
    void sendNG_();
    // 策略
    void stepImmediate_();
    void stepTimed_();
    void stepEncoder_();
};
