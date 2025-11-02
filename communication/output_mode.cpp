#include "output_mode.h"
#include <cstdio>
#include <cstdarg>

// ---------- 外部C接口（simple_fifo）声明 ----------
extern "C" {
    // 你的 simple_fifo 接口；下方仅示意
    int sfifo_out(struct simple_fifo *q, scheduled_fifo_param* param); // 0 成功；非0 队列空
    void sfifo_drain(struct simple_fifo *q);
}

// ---------- 简单日志兜底 ----------
static void noop_loge(const char* fmt, ...) { (void)fmt; }

// ---------- 工具 ----------
static inline uint64_t parse_u64(const std::string& s, uint64_t defv=0) {
    uint64_t v=0;
    if (sscanf(s.c_str(), "%" SCNu64, &v)==1) return v;
    return defv;
}

// ---------- SimpleFifoAdapter ----------
bool SimpleFifoAdapter::pop_one(scheduled_fifo_param& out) {
    if (!q_) return false;
    if (sfifo_out(q_, &out) != 0) return false; // 空
    sfifo_drain(q_);
    return true;
}

void SimpleFifoAdapter::drain_all(const std::function<void(const scheduled_fifo_param&)>& send) {
    if (!q_) return;
    scheduled_fifo_param p{};
    while (sfifo_out(q_, &p) == 0) {
        sfifo_drain(q_);
        send(p);
    }
}

// ---------- DequeAdapter ----------
bool DequeAdapter::pop_one(scheduled_fifo_param& out) {
    std::lock_guard<std::mutex> lk(*mtx_);
    if (dq_->empty()) return false;
    out = dq_->front();
    dq_->pop_front();
    return true;
}

void DequeAdapter::drain_all(const std::function<void(const scheduled_fifo_param&)>& send) {
    std::deque<scheduled_fifo_param> local;
    {
        std::lock_guard<std::mutex> lk(*mtx_);
        if (!dq_->empty()) dq_->swap(local);
    }
    for (auto& e : local) send(e);
}

void DequeAdapter::push_drop_old(const scheduled_fifo_param& in) {
    std::lock_guard<std::mutex> lk(*mtx_);
    if (dq_->size() >= SCHEDULED_SEND_QUEUE_LEN) {
        dq_->pop_front(); // 丢旧
    }
    dq_->push_back(in);
}

// ---------- 引擎 ----------
OutputModeEngine::OutputModeEngine(const OutputContext& ctx)
: ctx_(ctx) {
    if (!ctx_.loge) ctx_.loge = noop_loge;
}

void OutputModeEngine::setConfig(OutputMode mode, uint32_t scheduledIntervalMs) {
    ctx_.mode = mode;
    ctx_.scheduledIntervalMs = scheduledIntervalMs;
}

bool OutputModeEngine::fetchTriggerMode_() {
    inTriggerMode_ = false;
    if (!ctx_.trig) return false;
    int m = 0;
    if (ctx_.trig->GetParam(TRIGGER_MODE_PARAM, m) == 0) {
        inTriggerMode_ = (m != 0);
        return true;
    }
    return false;
}
bool OutputModeEngine::fetchTrigCount_(int& out) {
    if (!ctx_.trig) return false;
    return ctx_.trig->GetParam(TRIGGER_COUNT, out) == 0;
}
bool OutputModeEngine::fetchTrigImgTimeMs_(uint64_t& out) {
    if (!ctx_.trig) return false;
    std::string s;
    if (ctx_.trig->GetParam(TRIGGER_IMG_TIME, s) != 0) return false;
    out = parse_u64(s, out);
    return true;
}

void OutputModeEngine::sendNG_() {
    scheduled_fifo_param p{};
    const char* txt = ctx_.ngText ? ctx_.ngText : "NG";
    size_t n = std::min(sizeof(p.buf), strlen(txt));
    std::memcpy(p.buf, txt, n);
    p.len = static_cast<uint32_t>(n);
    ctx_.send(p);
}

// 立即模式：批量清空队列；不受触发模式限制
void OutputModeEngine::stepImmediate_() {
    if (!ctx_.queue) return;
    ctx_.queue->drain_all(ctx_.send);
}

// 定时模式：需在触发模式下，侦测“新帧”→等间隔→发送（空则发NG）；每帧只发一次
void OutputModeEngine::stepTimed_() {
    if (!ctx_.queue || !ctx_.nowMs) { sleepDefault_(); return; }
    (void)fetchTriggerMode_(); // 失败不致命
    if (!inTriggerMode_) { sleepDefault_(); return; }

    int trig = 0;
    if (!fetchTrigCount_(trig)) { sleepDefault_(); return; }

    if (lastTrigCount_ == -1) { // 初始化
        lastTrigCount_ = trig;
        (void)fetchTrigImgTimeMs_(lastImgMs_);
        sleepDefault_();
        return;
    }

    if (trig == lastTrigCount_) { // 未出图
        sleepDefault_();
        return;
    }

    // 新帧：拿出图时间
    (void)fetchTrigImgTimeMs_(lastImgMs_);
    // 等待到时（毫秒）
    uint64_t now = ctx_.nowMs();
    if (now - lastImgMs_ < ctx_.scheduledIntervalMs) {
        sleepDefault_();
        return;
    }

    // 到点：尝试取一条发送；若空则发 NG；消费后标记本帧已处理
    scheduled_fifo_param one{};
    if (ctx_.queue->pop_one(one)) {
        ctx_.send(one);
    } else {
        // 与旧实现一致：空则发 NG
        sendNG_();
    }
    lastTrigCount_ = trig;
}

// 编码器模式：需在触发模式下，侦测“新帧”→等待编码器=1→发送（空则仅LOGE）
// 每帧只发一次；如果编码器一直为0，就一直等待，不兜底。
void OutputModeEngine::stepEncoder_() {
    if (!ctx_.queue) { sleepDefault_(); return; }
    (void)fetchTriggerMode_();
    if (!inTriggerMode_) { sleepDefault_(); return; }

    int trig = 0;
    if (!fetchTrigCount_(trig)) { sleepDefault_(); return; }

    if (lastTrigCount_ == -1) { // 初始化
        lastTrigCount_ = trig;
        sleepDefault_();
        return;
    }

    if (trig == lastTrigCount_) { // 未出图
        sleepDefault_();
        return;
    }

    // 已出图：等编码器=1
    if (!ctx_.encoder) { // 缺失编码器接口，无法工作
        ctx_.loge("[encoder] encoder provider null\n");
        sleepDefault_();
        return;
    }
    int st = ctx_.encoder->getStatus();
    if (st != 1) {
        // 仍等待；不消费本帧
        sleepDefault_();
        return;
    }

    // 编码器=1：尝试发送一条；若队列空，仅打印，不发NG；无论是否发送都消费本帧（避免重复）
    scheduled_fifo_param one{};
    if (ctx_.queue->pop_one(one)) {
        ctx_.send(one);
    } else {
        ctx_.loge("[encoder] queue empty on trigger=%d, skip sending\n", trig);
    }
    lastTrigCount_ = trig; // 标记本帧已处理
}

void OutputModeEngine::tick() {
    switch (ctx_.mode) {
    case OutputMode::kImmediate: stepImmediate_(); break;
    case OutputMode::kTimed:     stepTimed_();     break;
    case OutputMode::kEncoder:   stepEncoder_();   break;
    }
}
