#include <cstring>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <cmath>

#include "RstDispatcher.h"
#include "ModuleLoader.h"
#include "PluginService.h"
#include "../oconfig/configfile.h"

struct RstDispatcher::Impl
{
    std::deque<std::shared_ptr<value_list_t>> queue;
    std::mutex              mtx;
    std::condition_variable cv;
    std::atomic<bool>       exit{false};
    std::thread             th;

    Impl()
    {
		th = std::thread([this]{
            /* 若有 thread_set_name，可以在此调用 */
            while (!exit.load(std::memory_order_acquire))
            {
                std::shared_ptr<value_list_t> vl;
                {
                    std::unique_lock<std::mutex> lk(mtx);
                    cv.wait(lk, [this]{ return exit || !queue.empty(); });
                    if (exit && queue.empty()) break;
                    vl = std::move(queue.front());
                    queue.pop_front();
                }

                /* todo: 过滤链钩子占位 */

                /* 分发给所有 writer 插件 */
				const data_set_t* ds = ConfigManager::Instance().GetDataSetByName(vl.get()->type);
				PluginService::Instance().write(ds, vl.get());
            }
        });
    }

    ~Impl()
    {
        exit.store(true);
        cv.notify_all();
        if (th.joinable()) th.join();
    }
};

RstDispatcher& RstDispatcher::Instance()
{
    static RstDispatcher inst;
    return inst;
}

RstDispatcher::RstDispatcher()  : pImpl_(new Impl) {}
RstDispatcher::~RstDispatcher() = default;

std::shared_ptr<value_list_t> RstDispatcher::vl_clone(const value_list_t *src)
{
	if (!src) return nullptr;

	auto dst = std::make_shared<value_list_t>();

	dst->values_len = src->values_len;
	
	if (src->time == 0)
	{
		dst->time = cdtime();
	}
	else
	{
		dst->time = src->time;
	}
	dst->interval = src->interval;
	memcpy(dst->plugin, src->plugin, sizeof(dst->plugin));
	memcpy(dst->plugin_instance, src->plugin_instance, sizeof(dst->plugin_instance));
	memcpy(dst->type, src->type, sizeof(dst->type));
	memcpy(dst->type_instance, src->type_instance, sizeof(dst->type_instance));

	// 深拷贝动态数组
	if (src->values && src->values_len > 0)
	{
		dst->values = new value_t[src->values_len];
		memcpy(dst->values, src->values, src->values_len * sizeof(value_t));
	}
	else
	{
		dst->values = nullptr;
	}

	return dst;
}

int RstDispatcher::enqueue(const value_list_t *vl)
{
	if (!vl) return EINVAL;

	auto clone_vl = vl_clone(vl);
	if (!clone_vl) return ENOMEM;

	{
		std::lock_guard<std::mutex> lk(pImpl_->mtx);
		pImpl_->queue.emplace_back(clone_vl);
	}

	pImpl_->cv.notify_one();
	return 0;
}

int RstDispatcher::enqueueMultivalues(const value_list_t *vl_template,
                                      bool store_percentage_if_gauge,
                                      int common_store_type,
                                      const std::vector<MetricDataPoint> &data_points)
{
	if (!vl_template || data_points.empty())
	{
		return EINVAL;
	}

	gauge_t sum = 0.0;
	if (common_store_type == DS_TYPE_GAUGE && store_percentage_if_gauge)
	{
		for (const auto &dp : data_points)
		{
			if (!std::isnan(dp.value))
			{
				sum += dp.value;
			}
		}
	}

	int failed_submissions = 0;

	value_list_t working_vl;
	value_t tmp_vl;

	working_vl.time = vl_template->time;
	working_vl.interval = vl_template->interval;
	snprintf(working_vl.plugin, DATA_MAX_NAME_LEN, "%s", vl_template->plugin);
	snprintf(working_vl.plugin_instance, DATA_MAX_NAME_LEN, "%s", vl_template->plugin_instance);

	working_vl.values = &tmp_vl;
	working_vl.values_len = 1;

	if (store_percentage_if_gauge && common_store_type == DS_TYPE_GAUGE)
	{
		snprintf(working_vl.type, DATA_MAX_NAME_LEN, "percent");
	}
	else
	{
		snprintf(working_vl.type, DATA_MAX_NAME_LEN, "%s", vl_template->type);
	}

	for (const auto &dp : data_points)
	{
		snprintf(working_vl.type_instance, DATA_MAX_NAME_LEN, "%s", dp.type_instance_name);

		switch (common_store_type)
		{
		case DS_TYPE_GAUGE:
			working_vl.values[0].gauge = dp.value;
			if (store_percentage_if_gauge)
			{
				if (sum != 0.0 && !std::isnan(dp.value))
				{
					working_vl.values[0].gauge = (dp.value / sum) * 100.0;
				}
				else
				{
					working_vl.values[0].gauge = NAN;
				}
			}
			break;
		case DS_TYPE_ABSOLUTE:
			working_vl.values[0].absolute = static_cast<absolute_t>(dp.value);
			break;
		case DS_TYPE_COUNTER:
			working_vl.values[0].counter = static_cast<counter_t>(dp.value);
			break;
		case DS_TYPE_DERIVE:
			working_vl.values[0].derive = static_cast<derive_t>(dp.value);
			break;
		default:
			ERROR("enqueue multi valus: given store_type is incorrect.");
			failed_submissions++;
			continue;
		}

		if (this->enqueue(&working_vl) != 0)
		{
			failed_submissions++;
		}
	}

	return failed_submissions;
}

int RstDispatcher::flushAll(cdtime_t /*timeout*/, const char* /*ident*/)
{
	/* 目前无实际操作，仅占位 */
	return 0;
}

void RstDispatcher::stop()
{
	pImpl_.reset();
}

