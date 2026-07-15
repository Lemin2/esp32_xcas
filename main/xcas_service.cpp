#include "xcas_service.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "esp_log.h"
#include "esp_system.h"

#include "giac.h"

namespace xcas {
namespace {

constexpr char kTag[] = "xcas_service";
constexpr uint32_t kWorkerStackSize = 64 * 1024;

bool isAsciiIdentifierStart(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isAsciiIdentifierPart(char c)
{
    return isAsciiIdentifierStart(c) || (c >= '0' && c <= '9');
}

void replaceAll(std::string &s, const std::string &from, const std::string &to)
{
    if (from.empty()) {
        return;
    }
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool decodeSuperscriptAt(const std::string &s, size_t pos, char &out, size_t &consumed)
{
    struct Item {
        const char *utf8;
        char ascii;
    };

    static const Item kItems[] = {
        {u8"⁰", '0'}, {u8"¹", '1'}, {u8"²", '2'}, {u8"³", '3'}, {u8"⁴", '4'},
        {u8"⁵", '5'}, {u8"⁶", '6'}, {u8"⁷", '7'}, {u8"⁸", '8'}, {u8"⁹", '9'},
        {u8"⁻", '-'},
    };

    for (const auto &item : kItems) {
        const size_t len = std::strlen(item.utf8);
        if (s.compare(pos, len, item.utf8) == 0) {
            out = item.ascii;
            consumed = len;
            return true;
        }
    }
    return false;
}

std::string insertImplicitMultiplication(const std::string &src)
{
    enum class TokType {
        None,
        Number,
        Ident,
        LParen,
        RParen,
        Comma,
        Op,
    };

    auto shouldMul = [](TokType prev, TokType cur, const std::string &prevIdent) {
        const bool leftOk = (prev == TokType::Number) || (prev == TokType::Ident) || (prev == TokType::RParen);
        const bool rightOk = (cur == TokType::Number) || (cur == TokType::Ident) || (cur == TokType::LParen);
        if (!leftOk || !rightOk) {
            return false;
        }
        if (prev == TokType::Ident && cur == TokType::LParen) {
            if (!prevIdent.empty() && prevIdent.back() == '_') {
                return true;
            }
            return false;
        }
        return true;
    };

    std::string out;
    out.reserve(src.size() + 16);
    TokType prev = TokType::None;
    std::string prevIdent;

    size_t i = 0;
    while (i < src.size()) {
        const char c = src[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }

        TokType cur = TokType::Op;
        std::string token;

        if ((c >= '0' && c <= '9') || c == '.') {
            cur = TokType::Number;
            size_t j = i;
            bool seenDot = (c == '.');
            while (j < src.size()) {
                const char cj = src[j];
                if (cj >= '0' && cj <= '9') {
                    ++j;
                    continue;
                }
                if (cj == '.' && !seenDot) {
                    seenDot = true;
                    ++j;
                    continue;
                }
                if ((cj == 'e' || cj == 'E') && (j + 1) < src.size()) {
                    size_t k = j + 1;
                    if (src[k] == '+' || src[k] == '-') {
                        ++k;
                    }
                    bool hasExpDigits = false;
                    while (k < src.size() && src[k] >= '0' && src[k] <= '9') {
                        hasExpDigits = true;
                        ++k;
                    }
                    if (hasExpDigits) {
                        j = k;
                        continue;
                    }
                }
                break;
            }
            token.assign(src, i, j - i);
            i = j;
        } else if (isAsciiIdentifierStart(c)) {
            cur = TokType::Ident;
            size_t j = i + 1;
            while (j < src.size() && isAsciiIdentifierPart(src[j])) {
                ++j;
            }
            token.assign(src, i, j - i);
            i = j;
        } else if (c == '(' || c == '[') {
            cur = TokType::LParen;
            token.push_back(c == '[' ? '(' : c);
            ++i;
        } else if (c == ')' || c == ']') {
            cur = TokType::RParen;
            token.push_back(c == ']' ? ')' : c);
            ++i;
        } else if (c == ',') {
            cur = TokType::Comma;
            token.push_back(c);
            ++i;
        } else {
            cur = TokType::Op;
            token.push_back(c);
            ++i;
        }

        if (shouldMul(prev, cur, prevIdent)) {
            out.push_back('*');
        }
        out += token;

        prev = cur;
        if (cur == TokType::Ident) {
            prevIdent = token;
        } else {
            prevIdent.clear();
        }
    }

    return out;
}

bool evalSampledReal(const std::string &expr, float x0, float dx, int count,
                    giac::context &ctx, std::vector<float> &out)
{
    out.clear();
    if (count <= 0) {
        return false;
    }
    out.reserve(static_cast<size_t>(count));

    try {
        const std::string normalized = XcasService::normalizeNaturalInput(expr);
        giac::gen parsed(normalized, &ctx);
        giac::gen symbolX("x", &ctx);

        for (int i = 0; i < count; ++i) {
            const double xv = static_cast<double>(x0) + static_cast<double>(i) * static_cast<double>(dx);
            giac::gen xval(xv);
            giac::gen substituted = giac::subst(parsed, symbolX, xval, false, &ctx);
            giac::gen evaluated = giac::evalf(substituted, giac::eval_level(&ctx), &ctx);

            float value = std::numeric_limits<float>::quiet_NaN();
            if (!giac::is_undef(evaluated)) {
                if (giac::is_inf(evaluated)) {
                    const double d = evaluated.to_double(&ctx);
                    value = static_cast<float>(d);
                } else if (evaluated.type != giac::_CPLX) {
                    const double d = evaluated.to_double(&ctx);
                    if (std::isfinite(d) || std::isinf(d)) {
                        value = static_cast<float>(d);
                    }
                }
            }
            out.push_back(value);
        }
        return true;
    } catch (...) {
        out.assign(static_cast<size_t>(count), std::numeric_limits<float>::quiet_NaN());
        return false;
    }
}

std::string evalExpression(const char *expr, giac::context &ctx)
{
    if (expr == nullptr || expr[0] == '\0') {
        return "";
    }

    try {
        const std::string normalized = XcasService::normalizeNaturalInput(expr);
        giac::gen parsed(normalized, &ctx);
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
    : job_queue_(nullptr),
      state_lock_(nullptr),
      worker_task_(nullptr),
      busy_(false),
      has_result_(false),
      has_sampled_result_(false)
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
    job.type = JobType::EvalExpr;
    std::strncpy(job.expr, expr, sizeof(job.expr) - 1);
    job.expr[sizeof(job.expr) - 1] = '\0';
    return xQueueSend(job_queue_, &job, 0) == pdPASS;
}

bool XcasService::submitSampledReal(const char *expr, float x0, float dx, int count)
{
    if (job_queue_ == nullptr || expr == nullptr || expr[0] == '\0' || count <= 0) {
        return false;
    }

    EvalJob job = {};
    job.type = JobType::SampledReal;
    std::strncpy(job.expr, expr, sizeof(job.expr) - 1);
    job.expr[sizeof(job.expr) - 1] = '\0';
    job.x0 = x0;
    job.dx = dx;
    job.count = static_cast<uint16_t>(count > 0xFFFF ? 0xFFFF : count);
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

bool XcasService::pollSampledResult(std::vector<float> &out)
{
    if (state_lock_ == nullptr) {
        return false;
    }

    bool got = false;
    if (xSemaphoreTake(state_lock_, 0) == pdTRUE) {
        if (has_sampled_result_) {
            out = last_sampled_result_;
            has_sampled_result_ = false;
            got = true;
        }
        xSemaphoreGive(state_lock_);
    }
    return got;
}

std::string XcasService::normalizeNaturalInput(const std::string &expr)
{
    std::string s = expr;

    replaceAll(s, u8"×", "*");
    replaceAll(s, u8"·", "*");
    replaceAll(s, u8"÷", "/");
    replaceAll(s, u8"−", "-");
    replaceAll(s, u8"π", "pi");
    replaceAll(s, u8"√", "sqrt");
    replaceAll(s, "{", "(");
    replaceAll(s, "}", ")");

    std::string expanded;
    expanded.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size();) {
        char mapped = 0;
        size_t consumed = 0;
        if (decodeSuperscriptAt(s, i, mapped, consumed)) {
            std::string power;
            size_t j = i;
            while (j < s.size()) {
                char ch = 0;
                size_t len = 0;
                if (!decodeSuperscriptAt(s, j, ch, len)) {
                    break;
                }
                power.push_back(ch);
                j += len;
            }
            if (!power.empty() && !expanded.empty()) {
                expanded += "^(";
                expanded += power;
                expanded += ')';
                i = j;
                continue;
            }
            expanded += power;
            i = j;
            continue;
        }

        expanded.push_back(s[i]);
        ++i;
    }

    return insertImplicitMultiplication(expanded);
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

        std::string result;
        std::vector<float> sampled;
        if (job.type == JobType::SampledReal) {
            const bool ok = evalSampledReal(job.expr, job.x0, job.dx, static_cast<int>(job.count), context, sampled);
            if (!ok) {
                result = "xcas error: sampled evaluation failed";
            }
        } else {
            result = evalExpression(job.expr, context);
        }

        if (xSemaphoreTake(state_lock_, portMAX_DELAY) == pdTRUE) {
            busy_ = false;
            if (job.type == JobType::SampledReal) {
                has_sampled_result_ = true;
                last_sampled_result_ = std::move(sampled);
                if (!result.empty()) {
                    has_result_ = true;
                    last_result_ = result;
                }
            } else {
                has_result_ = true;
                last_result_ = result;
            }
            xSemaphoreGive(state_lock_);
        }
    }
}

} // namespace xcas
