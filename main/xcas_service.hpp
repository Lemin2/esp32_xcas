#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace xcas {

class XcasService {
public:
    XcasService();
    ~XcasService();

    bool start();
    bool submit(const char *expr);
    bool submitSampledReal(const char *expr, float x0, float dx, int count);
    bool busy() const;
    bool pollResult(std::string &out);
    bool pollSampledResult(std::vector<float> &out);

    static std::string normalizeNaturalInput(const std::string &expr);

private:
    enum class JobType : uint8_t {
        EvalExpr = 0,
        SampledReal = 1,
    };

    struct EvalJob {
        JobType type = JobType::EvalExpr;
        char expr[192];
        float x0 = 0.0f;
        float dx = 0.0f;
        uint16_t count = 0;
    };

    static void evalTaskEntry(void *ctx);
    void evalTask();

    QueueHandle_t job_queue_;
    SemaphoreHandle_t state_lock_;
    TaskHandle_t worker_task_;

    bool busy_;
    bool has_result_;
    std::string last_result_;
    bool has_sampled_result_;
    std::vector<float> last_sampled_result_;
};

} // namespace xcas
