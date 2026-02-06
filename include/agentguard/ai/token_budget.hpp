#pragma once

#include "agentguard/resource.hpp"

namespace agentguard::ai {

// Represents a shared pool of LLM tokens with time-window replenishment
class TokenBudget {
public:
    TokenBudget(ResourceTypeId id,
                std::string name,
                ResourceQuantity total_tokens_per_window,
                Duration window_duration);

    Resource as_resource() const;

    ResourceQuantity total_tokens_per_window() const noexcept;
    Duration window_duration() const noexcept;
    double tokens_per_second_rate() const;

    // Separate input/output tracking ratio (e.g., 0.7 = 70% input tokens)
    void set_input_output_ratio(double input_fraction);
    double input_output_ratio() const noexcept;

    ResourceTypeId id() const noexcept;
    const std::string& name() const noexcept;

private:
    ResourceTypeId id_;
    std::string name_;
    ResourceQuantity total_tokens_;
    Duration window_duration_;
    double input_fraction_{0.5};
};

} // namespace agentguard::ai
