#include "agentguard/ai/rate_limiter.hpp"

namespace agentguard::ai {

RateLimiter::RateLimiter(ResourceTypeId id,
                         std::string api_name,
                         ResourceQuantity requests_per_window,
                         WindowType window)
    : id_(id)
    , api_name_(std::move(api_name))
    , requests_per_window_(requests_per_window)
    , window_type_(window)
{}

Resource RateLimiter::as_resource() const {
    Resource r(id_, api_name_, ResourceCategory::ApiRateLimit,
               requests_per_window_ + burst_allowance_);
    r.set_replenish_interval(window_to_duration());
    return r;
}

RateLimiter::WindowType RateLimiter::window_type() const noexcept {
    return window_type_;
}

ResourceQuantity RateLimiter::requests_per_window() const noexcept {
    return requests_per_window_;
}

void RateLimiter::set_burst_allowance(ResourceQuantity burst_extra) {
    burst_allowance_ = burst_extra;
}

ResourceQuantity RateLimiter::burst_allowance() const noexcept {
    return burst_allowance_;
}

void RateLimiter::add_endpoint_sublimit(std::string endpoint, ResourceQuantity limit) {
    endpoint_sublimits_[std::move(endpoint)] = limit;
}

const std::unordered_map<std::string, ResourceQuantity>& RateLimiter::endpoint_sublimits() const noexcept {
    return endpoint_sublimits_;
}

ResourceTypeId RateLimiter::id() const noexcept { return id_; }
const std::string& RateLimiter::api_name() const noexcept { return api_name_; }

Duration RateLimiter::window_to_duration() const {
    switch (window_type_) {
        case WindowType::PerSecond: return std::chrono::seconds(1);
        case WindowType::PerMinute: return std::chrono::minutes(1);
        case WindowType::PerHour:   return std::chrono::hours(1);
        case WindowType::PerDay:    return std::chrono::hours(24);
    }
    return std::chrono::minutes(1);
}

} // namespace agentguard::ai
