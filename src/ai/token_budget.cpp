#include "agentguard/ai/token_budget.hpp"

namespace agentguard::ai {

TokenBudget::TokenBudget(ResourceTypeId id,
                         std::string name,
                         ResourceQuantity total_tokens_per_window,
                         Duration window_duration)
    : id_(id)
    , name_(std::move(name))
    , total_tokens_(total_tokens_per_window)
    , window_duration_(window_duration)
{}

Resource TokenBudget::as_resource() const {
    Resource r(id_, name_, ResourceCategory::TokenBudget, total_tokens_);
    r.set_replenish_interval(window_duration_);
    return r;
}

ResourceQuantity TokenBudget::total_tokens_per_window() const noexcept {
    return total_tokens_;
}

Duration TokenBudget::window_duration() const noexcept {
    return window_duration_;
}

double TokenBudget::tokens_per_second_rate() const {
    auto secs = std::chrono::duration_cast<std::chrono::duration<double>>(window_duration_).count();
    return (secs > 0.0) ? static_cast<double>(total_tokens_) / secs : 0.0;
}

void TokenBudget::set_input_output_ratio(double input_fraction) {
    input_fraction_ = input_fraction;
}

double TokenBudget::input_output_ratio() const noexcept {
    return input_fraction_;
}

ResourceTypeId TokenBudget::id() const noexcept { return id_; }
const std::string& TokenBudget::name() const noexcept { return name_; }

} // namespace agentguard::ai
