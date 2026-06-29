#pragma once

#include <string>

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
    bool busy() const;
    bool pollResult(std::string &out);

private:
    struct EvalJob {
        char expr[192];
    };

    static void evalTaskEntry(void *ctx);
    void evalTask();

    QueueHandle_t job_queue_;
    SemaphoreHandle_t state_lock_;
    TaskHandle_t worker_task_;

    bool busy_;
    bool has_result_;
    std::string last_result_;
};

} // namespace xcas
