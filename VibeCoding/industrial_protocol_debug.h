/** @file
 * @brief Common debug helper for industrial protocol state machine.
 */

#ifndef __INDUSTRIAL_PROTOCOL_DEBUG_H
#define __INDUSTRIAL_PROTOCOL_DEBUG_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IND_PROTO_DEBUG_EVENT_MAX (64)
#define IND_PROTO_DEBUG_DUMP_MAX_DEFAULT (12)
#define IND_PROTO_DEBUG_REASON_LEN (32)

enum
{
	IND_PROTO_DEBUG_LEVEL_OFF = 0,
	IND_PROTO_DEBUG_LEVEL_ERROR = 1,
	IND_PROTO_DEBUG_LEVEL_TRACE = 2,
	IND_PROTO_DEBUG_LEVEL_TRACE_HEARTBEAT = 3,
};

typedef struct
{
	uint8_t bit;
	const char *name;
} ind_proto_debug_bit_t;

typedef struct
{
	uint8_t work_mode;
	uint8_t step;
	uint8_t waiting_result;
	uint16_t control_event;
	uint16_t status_event;
	int32_t elapsed_ms;
	const char *reason;
} ind_proto_debug_state_t;

typedef struct
{
	uint64_t ts_ms;
	uint8_t work_mode;
	uint8_t step;
	uint8_t waiting_result;
	uint16_t control_event;
	uint16_t status_event;
	int32_t elapsed_ms;
	char reason[IND_PROTO_DEBUG_REASON_LEN];
} ind_proto_debug_event_t;

typedef void (*ind_proto_debug_log_cb)(const char *msg, void *user_data);

typedef struct
{
	pthread_mutex_t lock;
	int inited;
	int level;
	char proto_name[16];
	const ind_proto_debug_bit_t *control_bits;
	uint32_t control_bits_num;
	const ind_proto_debug_bit_t *status_bits;
	uint32_t status_bits_num;
	ind_proto_debug_log_cb log_cb;
	void *log_cb_user_data;
	uint32_t heartbeat_ms;
	uint64_t last_heartbeat_ms;
	ind_proto_debug_state_t last_state;
	int has_last_state;
	ind_proto_debug_event_t events[IND_PROTO_DEBUG_EVENT_MAX];
	uint32_t write_idx;
	uint32_t event_cnt;
} ind_proto_debug_ctx_t;

static inline uint64_t ind_proto_debug_now_ms(void)
{
	struct timeval tv = {0};
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static inline int ind_proto_debug_append_text(char *buff, int buff_size, int offset, const char *fmt, ...)
{
	int wrote = 0;
	va_list ap;

	if (buff == NULL || buff_size <= 0 || offset >= buff_size)
	{
		return offset;
	}

	va_start(ap, fmt);
	wrote = vsnprintf(buff + offset, buff_size - offset, fmt, ap);
	va_end(ap);

	if (wrote < 0)
	{
		return offset;
	}
	if (offset + wrote >= buff_size)
	{
		return buff_size - 1;
	}
	return offset + wrote;
}

static inline void ind_proto_debug_bits_to_string(uint16_t value, const ind_proto_debug_bit_t *bits,
	uint32_t bit_num, char *out, int out_len)
{
	uint32_t i = 0;
	int first = 1;
	int offset = 0;

	if (out == NULL || out_len <= 0)
	{
		return;
	}

	out[0] = '\0';
	for (i = 0; i < bit_num; ++i)
	{
		if (value & ((uint16_t)1U << bits[i].bit))
		{
			offset = ind_proto_debug_append_text(out, out_len, offset, "%s%s", first ? "" : "|", bits[i].name);
			first = 0;
		}
	}

	if (first)
	{
		snprintf(out, out_len, "none");
	}
}

static inline int ind_proto_debug_init(ind_proto_debug_ctx_t *ctx,
	const char *proto_name,
	const ind_proto_debug_bit_t *control_bits, uint32_t control_bits_num,
	const ind_proto_debug_bit_t *status_bits, uint32_t status_bits_num,
	uint32_t heartbeat_ms,
	ind_proto_debug_log_cb log_cb, void *user_data)
{
	if (ctx == NULL)
	{
		return -1;
	}

	memset(ctx, 0, sizeof(*ctx));
	if (pthread_mutex_init(&ctx->lock, NULL) != 0)
	{
		return -2;
	}
	ctx->inited = 1;
	ctx->level = IND_PROTO_DEBUG_LEVEL_OFF;
	ctx->control_bits = control_bits;
	ctx->control_bits_num = control_bits_num;
	ctx->status_bits = status_bits;
	ctx->status_bits_num = status_bits_num;
	ctx->heartbeat_ms = (heartbeat_ms > 0) ? heartbeat_ms : 2000U;
	ctx->log_cb = log_cb;
	ctx->log_cb_user_data = user_data;
	if (proto_name != NULL)
	{
		snprintf(ctx->proto_name, sizeof(ctx->proto_name), "%s", proto_name);
	}
	return 0;
}

static inline void ind_proto_debug_deinit(ind_proto_debug_ctx_t *ctx)
{
	if (ctx == NULL || !ctx->inited)
	{
		return;
	}
	pthread_mutex_destroy(&ctx->lock);
	memset(ctx, 0, sizeof(*ctx));
}

static inline int ind_proto_debug_set_level(ind_proto_debug_ctx_t *ctx, int level)
{
	int new_level = level;
	if (ctx == NULL || !ctx->inited)
	{
		return -1;
	}
	if (new_level < IND_PROTO_DEBUG_LEVEL_OFF)
	{
		new_level = IND_PROTO_DEBUG_LEVEL_OFF;
	}
	else if (new_level > IND_PROTO_DEBUG_LEVEL_TRACE_HEARTBEAT)
	{
		new_level = IND_PROTO_DEBUG_LEVEL_TRACE_HEARTBEAT;
	}

	pthread_mutex_lock(&ctx->lock);
	ctx->level = new_level;
	pthread_mutex_unlock(&ctx->lock);
	return new_level;
}

static inline int ind_proto_debug_get_level(ind_proto_debug_ctx_t *ctx)
{
	int level = IND_PROTO_DEBUG_LEVEL_OFF;
	if (ctx == NULL || !ctx->inited)
	{
		return level;
	}
	pthread_mutex_lock(&ctx->lock);
	level = ctx->level;
	pthread_mutex_unlock(&ctx->lock);
	return level;
}

static inline int ind_proto_debug_state_equal(const ind_proto_debug_state_t *lhs, const ind_proto_debug_state_t *rhs)
{
	if (lhs == NULL || rhs == NULL)
	{
		return 0;
	}
	return lhs->work_mode == rhs->work_mode
		&& lhs->step == rhs->step
		&& lhs->waiting_result == rhs->waiting_result
		&& lhs->control_event == rhs->control_event
		&& lhs->status_event == rhs->status_event;
}

static inline void ind_proto_debug_emit(ind_proto_debug_ctx_t *ctx, const ind_proto_debug_event_t *event)
{
	char ctrl_bits[96] = {0};
	char status_bits[128] = {0};
	char line[512] = {0};

	if (ctx == NULL || event == NULL || ctx->log_cb == NULL)
	{
		return;
	}

	ind_proto_debug_bits_to_string(event->control_event, ctx->control_bits,
		ctx->control_bits_num, ctrl_bits, sizeof(ctrl_bits));
	ind_proto_debug_bits_to_string(event->status_event, ctx->status_bits,
		ctx->status_bits_num, status_bits, sizeof(status_bits));

	snprintf(line, sizeof(line),
		"[%s][%s] mode:%u step:%u ctrl:0x%04x(%s) status:0x%04x(%s) wait:%u elapsed:%dms ts:%llu",
		ctx->proto_name,
		event->reason,
		event->work_mode,
		event->step,
		event->control_event,
		ctrl_bits,
		event->status_event,
		status_bits,
		event->waiting_result,
		event->elapsed_ms,
		(unsigned long long)event->ts_ms);
	ctx->log_cb(line, ctx->log_cb_user_data);
}

static inline void ind_proto_debug_record(ind_proto_debug_ctx_t *ctx, const ind_proto_debug_state_t *state, int force_log)
{
	ind_proto_debug_event_t local_event = {0};
	int level = IND_PROTO_DEBUG_LEVEL_OFF;
	int should_log = 0;

	if (ctx == NULL || state == NULL || !ctx->inited)
	{
		return;
	}

	pthread_mutex_lock(&ctx->lock);
	level = ctx->level;
	if (level == IND_PROTO_DEBUG_LEVEL_OFF)
	{
		pthread_mutex_unlock(&ctx->lock);
		return;
	}

	local_event.ts_ms = ind_proto_debug_now_ms();
	local_event.work_mode = state->work_mode;
	local_event.step = state->step;
	local_event.waiting_result = state->waiting_result;
	local_event.control_event = state->control_event;
	local_event.status_event = state->status_event;
	local_event.elapsed_ms = state->elapsed_ms;
	snprintf(local_event.reason, sizeof(local_event.reason), "%s",
		(state->reason != NULL) ? state->reason : "unknown");

	ctx->events[ctx->write_idx] = local_event;
	ctx->write_idx = (ctx->write_idx + 1U) % IND_PROTO_DEBUG_EVENT_MAX;
	if (ctx->event_cnt < IND_PROTO_DEBUG_EVENT_MAX)
	{
		ctx->event_cnt++;
	}
	ctx->last_state = *state;
	ctx->has_last_state = 1;
	should_log = (force_log && level >= IND_PROTO_DEBUG_LEVEL_ERROR)
		|| (level >= IND_PROTO_DEBUG_LEVEL_TRACE);
	pthread_mutex_unlock(&ctx->lock);

	if (should_log)
	{
		ind_proto_debug_emit(ctx, &local_event);
	}
}

static inline void ind_proto_debug_record_if_changed(ind_proto_debug_ctx_t *ctx,
	const ind_proto_debug_state_t *prev_state,
	const ind_proto_debug_state_t *curr_state)
{
	if (ctx == NULL || prev_state == NULL || curr_state == NULL)
	{
		return;
	}

	if (!ind_proto_debug_state_equal(prev_state, curr_state))
	{
		ind_proto_debug_record(ctx, curr_state, 0);
	}
}

static inline void ind_proto_debug_record_heartbeat(ind_proto_debug_ctx_t *ctx,
	const ind_proto_debug_state_t *state)
{
	int level = IND_PROTO_DEBUG_LEVEL_OFF;
	uint64_t now_ms = 0;
	int should_hb = 0;

	if (ctx == NULL || state == NULL || !ctx->inited)
	{
		return;
	}

	now_ms = ind_proto_debug_now_ms();
	pthread_mutex_lock(&ctx->lock);
	level = ctx->level;
	if (level >= IND_PROTO_DEBUG_LEVEL_TRACE_HEARTBEAT)
	{
		if (ctx->last_heartbeat_ms == 0 || now_ms - ctx->last_heartbeat_ms >= ctx->heartbeat_ms)
		{
			ctx->last_heartbeat_ms = now_ms;
			should_hb = 1;
		}
	}
	pthread_mutex_unlock(&ctx->lock);

	if (should_hb)
	{
		ind_proto_debug_record(ctx, state, 0);
	}
}

static inline int ind_proto_debug_dump(ind_proto_debug_ctx_t *ctx, char *buff, int buff_size,
	int *data_len, uint32_t max_dump_cnt)
{
	ind_proto_debug_event_t local_events[IND_PROTO_DEBUG_EVENT_MAX];
	ind_proto_debug_state_t local_state = {0};
	uint32_t event_cnt = 0;
	uint32_t write_idx = 0;
	uint32_t start_idx = 0;
	uint32_t i = 0;
	uint32_t dump_cnt = 0;
	int offset = 0;
	int level = IND_PROTO_DEBUG_LEVEL_OFF;
	int has_last_state = 0;
	char ctrl_bits[96] = {0};
	char status_bits[128] = {0};

	if (ctx == NULL || buff == NULL || buff_size <= 0 || data_len == NULL || !ctx->inited)
	{
		return -1;
	}

	pthread_mutex_lock(&ctx->lock);
	level = ctx->level;
	event_cnt = ctx->event_cnt;
	write_idx = ctx->write_idx;
	has_last_state = ctx->has_last_state;
	if (has_last_state)
	{
		local_state = ctx->last_state;
	}
	if (event_cnt > 0)
	{
		memcpy(local_events, ctx->events, sizeof(local_events));
	}
	pthread_mutex_unlock(&ctx->lock);

	offset = ind_proto_debug_append_text(buff, buff_size, offset, "%s_debug_level=%d(0:off,1:error,2:trace,3:trace_heartbeat)\n",
		ctx->proto_name, level);

	if (has_last_state)
	{
		ind_proto_debug_bits_to_string(local_state.control_event, ctx->control_bits, ctx->control_bits_num, ctrl_bits, sizeof(ctrl_bits));
		ind_proto_debug_bits_to_string(local_state.status_event, ctx->status_bits, ctx->status_bits_num, status_bits, sizeof(status_bits));
		offset = ind_proto_debug_append_text(buff, buff_size, offset,
			"current mode=%u step=%u ctrl=0x%04x(%s) status=0x%04x(%s) wait=%u elapsed=%dms\n",
			local_state.work_mode,
			local_state.step,
			local_state.control_event,
			ctrl_bits,
			local_state.status_event,
			status_bits,
			local_state.waiting_result,
			local_state.elapsed_ms);
	}
	else
	{
		offset = ind_proto_debug_append_text(buff, buff_size, offset, "current state: no event\n");
	}

	if (max_dump_cnt == 0 || max_dump_cnt > IND_PROTO_DEBUG_EVENT_MAX)
	{
		max_dump_cnt = IND_PROTO_DEBUG_DUMP_MAX_DEFAULT;
	}
	dump_cnt = (event_cnt > max_dump_cnt) ? max_dump_cnt : event_cnt;
	offset = ind_proto_debug_append_text(buff, buff_size, offset, "recent_events=%u(total=%u):\n", dump_cnt, event_cnt);

	if (dump_cnt > 0)
	{
		start_idx = (write_idx + IND_PROTO_DEBUG_EVENT_MAX - dump_cnt) % IND_PROTO_DEBUG_EVENT_MAX;
		for (i = 0; i < dump_cnt; ++i)
		{
			uint32_t idx = (start_idx + i) % IND_PROTO_DEBUG_EVENT_MAX;
			offset = ind_proto_debug_append_text(buff, buff_size, offset,
				"[%02u] ts=%llu reason=%s step=%u ctrl=0x%04x status=0x%04x wait=%u elapsed=%dms mode=%u\n",
				i,
				(unsigned long long)local_events[idx].ts_ms,
				local_events[idx].reason,
				local_events[idx].step,
				local_events[idx].control_event,
				local_events[idx].status_event,
				local_events[idx].waiting_result,
				local_events[idx].elapsed_ms,
				local_events[idx].work_mode);
		}
	}

	*data_len = strlen(buff);
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __INDUSTRIAL_PROTOCOL_DEBUG_H */
