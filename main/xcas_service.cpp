#include "xcas_service.hpp"

#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_system.h"

#include "giac.h"

namespace xcas {
namespace {

constexpr char kTag[] = "xcas_service";
constexpr uint32_t kWorkerStackSize = 64 * 1024;

std::string evalExpression(const char *expr, giac::context &ctx)
{
    if (expr == nullptr || expr[0] == '\0') {
        return "";
    }

    try {
        giac::gen parsed(std::string(expr), &ctx);
        giac::gen result = giac::eval(parsed, giac::eval_level(&ctx), &ctx);
        const std::string text = result.print(&ctx);
        return text.empty() ? std::string("(ok)") : text;
    } catch (const std::exception &e) {
        return std::string("xcas error: ") + e.what();
    } catch (...) {
        return "xcas error: evaluation failed";
    }
}

} // namespace

XcasService::XcasService()
    : job_queue_(nullptr), state_lock_(nullptr), worker_task_(nullptr), busy_(false), has_result_(false)
{
}

XcasService::~XcasService()
{
    if (worker_task_ != nullptr) {
        vTaskDelete(worker_task_);
        worker_task_ = nullptr;
    }
    if (job_queue_ != nullptr) {
        vQueueDelete(job_queue_);
        job_queue_ = nullptr;
    }
    if (state_lock_ != nullptr) {
        vSemaphoreDelete(state_lock_);
        state_lock_ = nullptr;
    }
}

bool XcasService::start()
{
    if (worker_task_ != nullptr) {
        return true;
    }

    state_lock_ = xSemaphoreCreateMutex();
    job_queue_ = xQueueCreate(4, sizeof(EvalJob));
    if (state_lock_ == nullptr || job_queue_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create xcas service sync primitives");
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        &XcasService::evalTaskEntry,
        "xcas_eval_worker",
        kWorkerStackSize,
        this,
        5,
        &worker_task_,
        0);

    if (ok != pdPASS) {
        ESP_LOGE(kTag, "Failed to create xcas worker task");
        worker_task_ = nullptr;
        return false;
    }

    ESP_LOGI(kTag, "xcas worker started");
    return true;
}

bool XcasService::submit(const char *expr)
{
    if (job_queue_ == nullptr || expr == nullptr || expr[0] == '\0') {
        return false;
    }

    EvalJob job = {};
    std::strncpy(job.expr, expr, sizeof(job.expr) - 1);
    job.expr[sizeof(job.expr) - 1] = '\0';
    return xQueueSend(job_queue_, &job, 0) == pdPASS;
}

bool XcasService::busy() const
{
    if (state_lock_ == nullptr) {
        return false;
    }

    bool result = false;
    if (xSemaphoreTake(state_lock_, 0) == pdTRUE) {
        result = busy_;
        xSemaphoreGive(state_lock_);
    }
    return result;
}

bool XcasService::pollResult(std::string &out)
{
    if (state_lock_ == nullptr) {
        return false;
    }

    bool got = false;
    if (xSemaphoreTake(state_lock_, 0) == pdTRUE) {
        if (has_result_) {
            out = last_result_;
            has_result_ = false;
            got = true;
        }
        xSemaphoreGive(state_lock_);
    }
    return got;
}

void XcasService::evalTaskEntry(void *ctx)
{
    static_cast<XcasService *>(ctx)->evalTask();
}

void XcasService::evalTask()
{
    giac::context context;
    EvalJob job = {};

    for (;;) {
        if (xQueueReceive(job_queue_, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (xSemaphoreTake(state_lock_, portMAX_DELAY) == pdTRUE) {
            busy_ = true;
            xSemaphoreGive(state_lock_);
        }

        const std::string result = evalExpression(job.expr, context);

        if (xSemaphoreTake(state_lock_, portMAX_DELAY) == pdTRUE) {
            busy_ = false;
            has_result_ = true;
            last_result_ = result;
            xSemaphoreGive(state_lock_);
        }
    }
}

} // namespace xcas
