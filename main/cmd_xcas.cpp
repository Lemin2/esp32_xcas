#include <stdio.h>
#include <string.h>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_console.h"
#include "esp_err.h"
#include "linenoise/linenoise.h"

#include "giac.h"

/*
 * giac's evaluator is heavily recursive and needs a large stack. The default
 * main/console task stack (~7 KB) overflows on the very first evaluation, which
 * corrupts memory and shows up as a crash deep inside malloc/the C++ exception
 * runtime. We therefore run every evaluation in a dedicated FreeRTOS task with
 * a large stack (the unit of xTaskCreate's stack argument is bytes in ESP-IDF).
 */
#ifndef XCAS_EVAL_STACK_SIZE
#define XCAS_EVAL_STACK_SIZE (48 * 1024)
#endif

static giac::context g_xcas_context;

static int xcas_eval_line(const char *line)
{
    if (line == nullptr || line[0] == '\0') {
        return ESP_OK;
    }

    try {
        giac::gen expr(std::string(line), &g_xcas_context);
        giac::gen result = giac::eval(expr, giac::eval_level(&g_xcas_context), &g_xcas_context);
        const std::string printed = result.print(&g_xcas_context);
        if (!printed.empty()) {
            printf("%s\n", printed.c_str());
        }
    } catch (const std::exception &e) {
        printf("xcas error: %s\n", e.what());
        return ESP_FAIL;
    } catch (...) {
        printf("xcas error: evaluation failed\n");
        return ESP_FAIL;
    }

    return ESP_OK;
}

namespace {

struct xcas_eval_job {
    const char *line;
    int result;
    TaskHandle_t caller;
};

void xcas_eval_task(void *param)
{
    auto *job = static_cast<xcas_eval_job *>(param);
    job->result = xcas_eval_line(job->line);
    xTaskNotifyGive(job->caller);
    vTaskDelete(nullptr);
}

// Evaluate one line on a dedicated large-stack task and wait for it to finish.
int xcas_eval_on_big_stack(const char *line)
{
    xcas_eval_job job{ line, ESP_OK, xTaskGetCurrentTaskHandle() };
    TaskHandle_t handle = nullptr;
    BaseType_t ok = xTaskCreate(xcas_eval_task, "xcas_eval",
                                XCAS_EVAL_STACK_SIZE, &job, 5, &handle);
    if (ok != pdPASS) {
        printf("xcas: failed to allocate evaluation task (out of memory)\n");
        return ESP_ERR_NO_MEM;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return job.result;
}

} // namespace

static int cmd_xcas(int argc, char **argv)
{
    if (argc > 1) {
        std::string expr;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) {
                expr.push_back(' ');
            }
            expr += argv[i];
        }
        return xcas_eval_on_big_stack(expr.c_str());
    }

    printf("Entering xcas shell. Type 'exit' to return.\n");
    while (true) {
        char *line = linenoise("xcas> ");
        if (line == nullptr) {
            break;
        }

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            linenoiseFree(line);
            break;
        }

        if (line[0] != '\0') {
            linenoiseHistoryAdd(line);
        }

        xcas_eval_on_big_stack(line);
        linenoiseFree(line);
    }

    return ESP_OK;
}

extern "C" void register_xcas(void)
{
    esp_console_cmd_t command = {};
    command.command = "xcas";
    command.help = "Enter xcas REPL, or evaluate xcas <expr>";
    command.hint = "[expr]";
    command.func = &cmd_xcas;
    command.func_w_context = NULL;
    command.argtable = NULL;
    command.context = NULL;
    ESP_ERROR_CHECK(esp_console_cmd_register(&command));
}