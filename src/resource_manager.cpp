#include "agentguard/resource_manager.hpp"
#include "agentguard/exceptions.hpp"

#include <algorithm>

namespace agentguard {

ResourceManager::ResourceManager(Config config)
    : config_(std::move(config))
    , request_queue_(config_.max_queue_size)
    , scheduling_policy_(std::make_unique<FifoPolicy>())
    , demand_estimator_(config_.adaptive)
{
    if (config_.progress.enabled) {
        progress_tracker_ = std::make_unique<ProgressTracker>(config_.progress);
    }
    if (config_.delegation.enabled) {
        delegation_tracker_ = std::make_unique<DelegationTracker>(config_.delegation);
    }
}

ResourceManager::~ResourceManager() {
    stop();
}

// ==================== Resource Registration ====================

void ResourceManager::register_resource(Resource resource) {
    std::unique_lock lock(state_mutex_);
    auto id = resource.id();
    resources_.emplace(id, std::move(resource));
    lock.unlock();
    emit_event(EventType::ResourceRegistered, "Resource registered",
               std::nullopt, id);
}

bool ResourceManager::unregister_resource(ResourceTypeId id) {
    std::unique_lock lock(state_mutex_);
    auto it = resources_.find(id);
    if (it == resources_.end()) return false;
    if (it->second.allocated() > 0) return false;
    resources_.erase(it);
    return true;
}

bool ResourceManager::adjust_resource_capacity(ResourceTypeId id, ResourceQuantity new_capacity) {
    std::unique_lock lock(state_mutex_);
    auto it = resources_.find(id);
    if (it == resources_.end()) return false;
    bool ok = it->second.set_total_capacity(new_capacity);
    if (ok) {
        lock.unlock();
        emit_event(EventType::ResourceCapacityChanged, "Capacity adjusted",
                   std::nullopt, id, std::nullopt, new_capacity);
    }
    return ok;
}

std::optional<Resource> ResourceManager::get_resource(ResourceTypeId id) const {
    std::shared_lock lock(state_mutex_);
    auto it = resources_.find(id);
    if (it == resources_.end()) return std::nullopt;
    return it->second;
}

std::vector<Resource> ResourceManager::get_all_resources() const {
    std::shared_lock lock(state_mutex_);
    std::vector<Resource> result;
    result.reserve(resources_.size());
    for (auto& [_, r] : resources_) {
        result.push_back(r);
    }
    return result;
}

// ==================== Agent Lifecycle ====================

AgentId ResourceManager::register_agent(Agent agent) {
    std::unique_lock lock(state_mutex_);
    AgentId id = next_agent_id_++;
    // Update the agent's ID to the assigned one
    Agent registered(id, agent.name(), agent.priority());
    for (auto& [rt, qty] : agent.max_needs()) {
        registered.declare_max_need(rt, qty);
    }
    if (!agent.model_identifier().empty()) {
        registered.set_model_identifier(agent.model_identifier());
    }
    if (!agent.task_description().empty()) {
        registered.set_task_description(agent.task_description());
    }
    agents_.emplace(id, std::move(registered));
    lock.unlock();

    if (progress_tracker_) progress_tracker_->register_agent(id);
    if (delegation_tracker_) delegation_tracker_->register_agent(id);

    emit_event(EventType::AgentRegistered, "Agent registered: " + agent.name(),
               id);
    return id;
}

bool ResourceManager::deregister_agent(AgentId id) {
    std::unique_lock lock(state_mutex_);
    auto it = agents_.find(id);
    if (it == agents_.end()) return false;

    // Release all resources held by this agent
    for (auto& [rt, qty] : it->second.current_allocation()) {
        auto res_it = resources_.find(rt);
        if (res_it != resources_.end()) {
            res_it->second.deallocate(qty);
        }
    }

    std::string name = it->second.name();
    agents_.erase(it);
    lock.unlock();

    if (progress_tracker_) progress_tracker_->deregister_agent(id);
    if (delegation_tracker_) delegation_tracker_->deregister_agent(id);
    demand_estimator_.clear_agent(id);

    // Cancel all pending requests for this agent
    request_queue_.cancel_all_for_agent(id);

    emit_event(EventType::AgentDeregistered, "Agent deregistered: " + name, id);

    // Notify the processor to re-evaluate pending requests
    release_cv_.notify_all();
    return true;
}

bool ResourceManager::update_agent_max_claim(AgentId id, ResourceTypeId resource_type,
                                              ResourceQuantity new_max) {
    std::unique_lock lock(state_mutex_);
    auto it = agents_.find(id);
    if (it == agents_.end()) return false;

    auto& alloc = it->second.current_allocation();
    auto alloc_it = alloc.find(resource_type);
    ResourceQuantity current = (alloc_it != alloc.end()) ? alloc_it->second : 0;

    if (new_max < current) return false;  // Can't reduce below current allocation

    it->second.declare_max_need(resource_type, new_max);
    return true;
}

std::optional<Agent> ResourceManager::get_agent(AgentId id) const {
    std::shared_lock lock(state_mutex_);
    auto it = agents_.find(id);
    if (it == agents_.end()) return std::nullopt;
    return it->second;
}

std::vector<Agent> ResourceManager::get_all_agents() const {
    std::shared_lock lock(state_mutex_);
    std::vector<Agent> result;
    result.reserve(agents_.size());
    for (auto& [_, a] : agents_) {
        result.push_back(a);
    }
    return result;
}

std::size_t ResourceManager::agent_count() const {
    std::shared_lock lock(state_mutex_);
    return agents_.size();
}

// ==================== Synchronous Resource Requests ====================

RequestStatus ResourceManager::request_resources(
    AgentId agent_id,
    ResourceTypeId resource_type,
    ResourceQuantity quantity,
    std::optional<Duration> timeout)
{
    // Validate the request
    {
        std::shared_lock lock(state_mutex_);
        auto agent_it = agents_.find(agent_id);
        if (agent_it == agents_.end()) {
            throw AgentNotFoundException(agent_id);
        }
        auto res_it = resources_.find(resource_type);
        if (res_it == resources_.end()) {
            throw ResourceNotFoundException(resource_type);
        }

        // Check if request exceeds max claim
        auto max_it = agent_it->second.max_needs().find(resource_type);
        if (max_it != agent_it->second.max_needs().end()) {
            auto alloc = agent_it->second.current_allocation();
            auto alloc_it = alloc.find(resource_type);
            ResourceQuantity current = (alloc_it != alloc.end()) ? alloc_it->second : 0;
            if (current + quantity > max_it->second) {
                throw MaxClaimExceededException(agent_id, resource_type,
                                                 quantity, max_it->second);
            }
        }

        // Check if request exceeds total capacity
        if (quantity > res_it->second.total_capacity()) {
            throw ResourceCapacityExceededException(resource_type, quantity,
                                                     res_it->second.total_capacity());
        }
    }

    emit_event(EventType::RequestSubmitted, "Request submitted",
               agent_id, resource_type, std::nullopt, quantity);

    demand_estimator_.record_request(agent_id, resource_type, quantity);

    // Try to grant immediately
    {
        std::unique_lock lock(state_mutex_);
        auto& res = resources_.at(resource_type);

        if (res.available() >= quantity) {
            // Check if granting would keep us in a safe state
            auto input = build_safety_input();
            auto t0 = std::chrono::steady_clock::now();
            auto result = safety_checker_.check_hypothetical(
                input, agent_id, resource_type, quantity);
            auto t1 = std::chrono::steady_clock::now();
            double dur_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

            emit_event(EventType::SafetyCheckPerformed, result.reason,
                       agent_id, resource_type, std::nullopt, quantity,
                       result.is_safe, dur_us);

            if (result.is_safe) {
                // Grant!
                res.allocate(quantity);
                agents_.at(agent_id).allocate(resource_type, quantity);
                auto alloc = agents_.at(agent_id).current_allocation();
                auto alloc_it = alloc.find(resource_type);
                ResourceQuantity level = (alloc_it != alloc.end()) ? alloc_it->second : 0;
                lock.unlock();
                demand_estimator_.record_allocation_level(agent_id, resource_type, level);
                emit_event(EventType::RequestGranted, "Granted immediately",
                           agent_id, resource_type, std::nullopt, quantity);
                return RequestStatus::Granted;
            } else {
                emit_event(EventType::UnsafeStateDetected,
                          "Would create unsafe state", agent_id, resource_type);
            }
        }
    }

    // Can't grant immediately - wait with timeout
    Duration wait_timeout = timeout.value_or(config_.default_request_timeout);
    auto deadline = Clock::now() + wait_timeout;

    while (Clock::now() < deadline) {
        {
            std::unique_lock lock(state_mutex_);

            auto res_it = resources_.find(resource_type);
            if (res_it == resources_.end()) return RequestStatus::Denied;
            auto agent_it = agents_.find(agent_id);
            if (agent_it == agents_.end()) return RequestStatus::Denied;

            if (res_it->second.available() >= quantity) {
                auto input = build_safety_input();
                auto t0 = std::chrono::steady_clock::now();
                auto result = safety_checker_.check_hypothetical(
                    input, agent_id, resource_type, quantity);
                auto t1 = std::chrono::steady_clock::now();
                double dur_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

                emit_event(EventType::SafetyCheckPerformed, result.reason,
                           agent_id, resource_type, std::nullopt, quantity,
                           result.is_safe, dur_us);

                if (result.is_safe) {
                    res_it->second.allocate(quantity);
                    agent_it->second.allocate(resource_type, quantity);
                    auto alloc = agent_it->second.current_allocation();
                    auto a_it = alloc.find(resource_type);
                    ResourceQuantity level = (a_it != alloc.end()) ? a_it->second : 0;
                    lock.unlock();
                    demand_estimator_.record_allocation_level(agent_id, resource_type, level);
                    emit_event(EventType::RequestGranted, "Granted after waiting",
                               agent_id, resource_type, std::nullopt, quantity);
                    return RequestStatus::Granted;
                }
                // Resources available but unsafe - if no background processor
                // running, state won't change, so deny immediately
                if (!running_.load()) {
                    lock.unlock();
                    emit_event(EventType::RequestDenied,
                              "Unsafe state and no processor running",
                              agent_id, resource_type, std::nullopt, quantity);
                    return RequestStatus::Denied;
                }
            }

            // Wait for a release event or timeout
            auto remaining = deadline - Clock::now();
            if (remaining <= Duration::zero()) break;

            release_cv_.wait_for(lock, std::min(remaining, config_.processor_poll_interval));
        }
    }

    emit_event(EventType::RequestTimedOut, "Request timed out",
               agent_id, resource_type, std::nullopt, quantity);
    return RequestStatus::TimedOut;
}

RequestStatus ResourceManager::request_resources_batch(
    AgentId agent_id,
    const std::unordered_map<ResourceTypeId, ResourceQuantity>& requests,
    std::optional<Duration> timeout)
{
    // Validate all requests
    {
        std::shared_lock lock(state_mutex_);
        auto agent_it = agents_.find(agent_id);
        if (agent_it == agents_.end()) {
            throw AgentNotFoundException(agent_id);
        }
        for (auto& [rt, qty] : requests) {
            auto res_it = resources_.find(rt);
            if (res_it == resources_.end()) {
                throw ResourceNotFoundException(rt);
            }
        }
    }

    Duration wait_timeout = timeout.value_or(config_.default_request_timeout);
    auto deadline = Clock::now() + wait_timeout;

    while (Clock::now() < deadline) {
        std::unique_lock lock(state_mutex_);

        // Check if all resources are available
        bool all_available = true;
        for (auto& [rt, qty] : requests) {
            auto res_it = resources_.find(rt);
            if (res_it == resources_.end() || res_it->second.available() < qty) {
                all_available = false;
                break;
            }
        }

        if (all_available) {
            // Build hypothetical batch
            auto input = build_safety_input();
            std::vector<ResourceRequest> batch;
            for (auto& [rt, qty] : requests) {
                ResourceRequest req;
                req.agent_id = agent_id;
                req.resource_type = rt;
                req.quantity = qty;
                batch.push_back(req);
            }

            auto t0 = std::chrono::steady_clock::now();
            auto result = safety_checker_.check_hypothetical_batch(input, batch);
            auto t1 = std::chrono::steady_clock::now();
            double dur_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

            emit_event(EventType::SafetyCheckPerformed, result.reason,
                       agent_id, std::nullopt, std::nullopt, std::nullopt,
                       result.is_safe, dur_us);

            if (result.is_safe) {
                // Grant all atomically
                for (auto& [rt, qty] : requests) {
                    resources_.at(rt).allocate(qty);
                    agents_.at(agent_id).allocate(rt, qty);
                }
                lock.unlock();
                emit_event(EventType::RequestGranted, "Batch granted",
                           agent_id);
                return RequestStatus::Granted;
            }

            // Resources available but unsafe - if no background processor
            // running, state won't change, so deny immediately
            if (!running_.load()) {
                lock.unlock();
                emit_event(EventType::RequestDenied,
                          "Batch unsafe and no processor running", agent_id);
                return RequestStatus::Denied;
            }
        }

        auto remaining = deadline - Clock::now();
        if (remaining <= Duration::zero()) break;

        release_cv_.wait_for(lock, std::min(remaining, config_.processor_poll_interval));
    }

    emit_event(EventType::RequestTimedOut, "Batch request timed out", agent_id);
    return RequestStatus::TimedOut;
}

// ==================== Asynchronous Resource Requests ====================

std::future<RequestStatus> ResourceManager::request_resources_async(
    AgentId agent_id,
    ResourceTypeId resource_type,
    ResourceQuantity quantity,
    std::optional<Duration> timeout)
{
    return std::async(std::launch::async, [this, agent_id, resource_type, quantity, timeout] {
        return request_resources(agent_id, resource_type, quantity, timeout);
    });
}

RequestId ResourceManager::request_resources_callback(
    AgentId agent_id,
    ResourceTypeId resource_type,
    ResourceQuantity quantity,
    RequestCallback callback,
    std::optional<Duration> timeout)
{
    ResourceRequest req;
    req.agent_id = agent_id;
    req.resource_type = resource_type;
    req.quantity = quantity;
    req.priority = PRIORITY_NORMAL;
    req.timeout = timeout;
    req.callback = std::move(callback);

    // Get the agent's priority
    {
        std::shared_lock lock(state_mutex_);
        auto it = agents_.find(agent_id);
        if (it != agents_.end()) {
            req.priority = it->second.priority();
        }
    }

    return request_queue_.enqueue(std::move(req));
}

// ==================== Resource Release ====================

void ResourceManager::release_resources(AgentId agent_id, ResourceTypeId resource_type,
                                         ResourceQuantity quantity) {
    std::unique_lock lock(state_mutex_);
    auto agent_it = agents_.find(agent_id);
    if (agent_it == agents_.end()) {
        throw AgentNotFoundException(agent_id);
    }
    auto res_it = resources_.find(resource_type);
    if (res_it == resources_.end()) {
        throw ResourceNotFoundException(resource_type);
    }

    agent_it->second.deallocate(resource_type, quantity);
    res_it->second.deallocate(quantity);
    auto alloc = agent_it->second.current_allocation();
    auto a_it = alloc.find(resource_type);
    ResourceQuantity level = (a_it != alloc.end()) ? a_it->second : 0;
    lock.unlock();

    demand_estimator_.record_allocation_level(agent_id, resource_type, level);

    emit_event(EventType::ResourcesReleased, "Resources released",
               agent_id, resource_type, std::nullopt, quantity);

    release_cv_.notify_all();
}

void ResourceManager::release_all_resources(AgentId agent_id, ResourceTypeId resource_type) {
    std::unique_lock lock(state_mutex_);
    auto agent_it = agents_.find(agent_id);
    if (agent_it == agents_.end()) return;

    auto& alloc = agent_it->second.current_allocation();
    auto alloc_it = alloc.find(resource_type);
    if (alloc_it == alloc.end()) return;

    ResourceQuantity qty = alloc_it->second;
    agent_it->second.deallocate(resource_type, qty);

    auto res_it = resources_.find(resource_type);
    if (res_it != resources_.end()) {
        res_it->second.deallocate(qty);
    }
    lock.unlock();

    emit_event(EventType::ResourcesReleased, "All resources released for type",
               agent_id, resource_type, std::nullopt, qty);
    release_cv_.notify_all();
}

void ResourceManager::release_all_resources(AgentId agent_id) {
    std::unique_lock lock(state_mutex_);
    auto agent_it = agents_.find(agent_id);
    if (agent_it == agents_.end()) return;

    // Copy allocation to avoid modifying while iterating
    auto alloc_copy = agent_it->second.current_allocation();
    for (auto& [rt, qty] : alloc_copy) {
        agent_it->second.deallocate(rt, qty);
        auto res_it = resources_.find(rt);
        if (res_it != resources_.end()) {
            res_it->second.deallocate(qty);
        }
    }
    lock.unlock();

    emit_event(EventType::ResourcesReleased, "All resources released",
               agent_id);
    release_cv_.notify_all();
}

// ==================== Queries ====================

bool ResourceManager::is_safe() const {
    std::shared_lock lock(state_mutex_);
    auto input = build_safety_input();
    auto result = safety_checker_.check_safety(input);
    return result.is_safe;
}

SystemSnapshot ResourceManager::get_snapshot() const {
    std::shared_lock lock(state_mutex_);
    SystemSnapshot snap;
    snap.timestamp = Clock::now();

    for (auto& [id, res] : resources_) {
        snap.total_resources[id] = res.total_capacity();
        snap.available_resources[id] = res.available();
    }

    for (auto& [id, agent] : agents_) {
        AgentAllocationSnapshot as;
        as.agent_id = id;
        as.name = agent.name();
        as.priority = agent.priority();
        as.state = agent.state();
        as.allocation = agent.current_allocation();
        as.max_claim = agent.max_needs();
        snap.agents.push_back(std::move(as));
    }

    snap.pending_requests = request_queue_.size();

    auto input = build_safety_input();
    snap.is_safe = safety_checker_.check_safety(input).is_safe;

    return snap;
}

std::size_t ResourceManager::pending_request_count() const {
    return request_queue_.size();
}

// ==================== Configuration ====================

void ResourceManager::set_scheduling_policy(std::unique_ptr<SchedulingPolicy> policy) {
    scheduling_policy_ = std::move(policy);
}

void ResourceManager::set_monitor(std::shared_ptr<Monitor> monitor) {
    monitor_ = monitor;
    if (delegation_tracker_) delegation_tracker_->set_monitor(monitor);
}

void ResourceManager::start() {
    if (running_.exchange(true)) return;  // Already running

    if (progress_tracker_) {
        // Stall action: auto-release all resources for the stalled agent
        ProgressTracker::StallActionCallback stall_cb = nullptr;
        if (config_.progress.auto_release_on_stall) {
            stall_cb = [this](AgentId id) {
                release_all_resources(id);
                emit_event(EventType::AgentResourcesAutoReleased,
                           "Stalled agent resources auto-released", id);
            };
        }
        progress_tracker_->start(monitor_, std::move(stall_cb));
    }

    processor_thread_ = std::thread([this] { process_queue_loop(); });
}

void ResourceManager::stop() {
    if (!running_.exchange(false)) return;  // Already stopped

    if (progress_tracker_) progress_tracker_->stop();

    release_cv_.notify_all();
    request_queue_.notify();

    if (processor_thread_.joinable()) {
        processor_thread_.join();
    }
}

bool ResourceManager::is_running() const noexcept {
    return running_.load();
}

// ==================== Internal Helpers ====================

SafetyCheckInput ResourceManager::build_safety_input() const {
    // Caller must hold state_mutex_ (shared or exclusive)
    SafetyCheckInput input;

    for (auto& [id, res] : resources_) {
        input.total[id] = res.total_capacity();
        input.available[id] = res.available();
    }

    for (auto& [id, agent] : agents_) {
        input.allocation[id] = agent.current_allocation();
        input.max_need[id] = agent.max_needs();
    }

    return input;
}

void ResourceManager::process_queue_loop() {
    while (running_.load()) {
        // Process callback-based requests from the queue
        try_grant_pending_requests();

        // Expire timed-out requests
        if (config_.enable_timeout_expiration) {
            auto expired = request_queue_.expire_timed_out();
            for (auto req_id : expired) {
                emit_event(EventType::RequestTimedOut, "Queue request timed out",
                           std::nullopt, std::nullopt, req_id);
            }
        }

        // Sleep briefly
        std::unique_lock lock(state_mutex_);
        release_cv_.wait_for(lock, config_.processor_poll_interval);
    }
}

void ResourceManager::try_grant_pending_requests() {
    auto pending = request_queue_.get_all_pending();
    if (pending.empty()) return;

    // Apply scheduling policy
    auto snapshot = get_snapshot();
    auto ordered = scheduling_policy_->prioritize(pending, snapshot);

    for (auto& req : ordered) {
        std::unique_lock lock(state_mutex_);

        auto res_it = resources_.find(req.resource_type);
        auto agent_it = agents_.find(req.agent_id);
        if (res_it == resources_.end() || agent_it == agents_.end()) {
            request_queue_.cancel(req.id);
            continue;
        }

        if (res_it->second.available() >= req.quantity) {
            auto input = build_safety_input();
            auto t0 = std::chrono::steady_clock::now();
            auto result = safety_checker_.check_hypothetical(
                input, req.agent_id, req.resource_type, req.quantity);
            auto t1 = std::chrono::steady_clock::now();
            double dur_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

            emit_event(EventType::SafetyCheckPerformed, result.reason,
                       req.agent_id, req.resource_type, req.id, req.quantity,
                       result.is_safe, dur_us);

            if (result.is_safe) {
                res_it->second.allocate(req.quantity);
                agent_it->second.allocate(req.resource_type, req.quantity);
                lock.unlock();

                // Remove from queue and notify callback
                request_queue_.cancel(req.id);  // Remove from queue
                if (req.callback) {
                    req.callback(req.id, RequestStatus::Granted);
                }
                emit_event(EventType::RequestGranted, "Queue request granted",
                           req.agent_id, req.resource_type, req.id, req.quantity);
            }
        }
    }
}

bool ResourceManager::try_grant_single(AgentId agent_id, ResourceTypeId resource_type,
                                         ResourceQuantity quantity) {
    // Caller must hold exclusive state_mutex_
    auto res_it = resources_.find(resource_type);
    auto agent_it = agents_.find(agent_id);
    if (res_it == resources_.end() || agent_it == agents_.end()) return false;

    if (res_it->second.available() < quantity) return false;

    auto input = build_safety_input();
    auto t0 = std::chrono::steady_clock::now();
    auto result = safety_checker_.check_hypothetical(input, agent_id, resource_type, quantity);
    auto t1 = std::chrono::steady_clock::now();
    double dur_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

    emit_event(EventType::SafetyCheckPerformed, result.reason,
               agent_id, resource_type, std::nullopt, quantity,
               result.is_safe, dur_us);

    if (result.is_safe) {
        res_it->second.allocate(quantity);
        agent_it->second.allocate(resource_type, quantity);
        return true;
    }
    return false;
}

// ==================== Progress Monitoring ====================

void ResourceManager::report_progress(AgentId id, const std::string& metric, double value) {
    if (progress_tracker_) progress_tracker_->report_progress(id, metric, value);
}

void ResourceManager::set_agent_stall_threshold(AgentId id, Duration threshold) {
    if (progress_tracker_) progress_tracker_->set_agent_stall_threshold(id, threshold);
}

bool ResourceManager::is_agent_stalled(AgentId id) const {
    if (progress_tracker_) return progress_tracker_->is_stalled(id);
    return false;
}

std::vector<AgentId> ResourceManager::get_stalled_agents() const {
    if (progress_tracker_) return progress_tracker_->get_stalled_agents();
    return {};
}

// ==================== Delegation Tracking ====================

DelegationResult ResourceManager::report_delegation(AgentId from, AgentId to,
                                                     const std::string& task_desc) {
    if (delegation_tracker_) return delegation_tracker_->report_delegation(from, to, task_desc);
    return DelegationResult{true, false, {}};
}

void ResourceManager::complete_delegation(AgentId from, AgentId to) {
    if (delegation_tracker_) delegation_tracker_->complete_delegation(from, to);
}

void ResourceManager::cancel_delegation(AgentId from, AgentId to) {
    if (delegation_tracker_) delegation_tracker_->cancel_delegation(from, to);
}

std::vector<DelegationInfo> ResourceManager::get_all_delegations() const {
    if (delegation_tracker_) return delegation_tracker_->get_all_delegations();
    return {};
}

std::optional<std::vector<AgentId>> ResourceManager::find_delegation_cycle() const {
    if (delegation_tracker_) return delegation_tracker_->find_cycle();
    return std::nullopt;
}

// ==================== Adaptive Demands ====================

void ResourceManager::set_agent_demand_mode(AgentId id, DemandMode mode) {
    demand_estimator_.set_agent_demand_mode(id, mode);
    emit_event(EventType::AdaptiveDemandModeChanged,
               std::string("Demand mode changed to ") + to_string(mode), id);
}

ProbabilisticSafetyResult ResourceManager::check_safety_probabilistic(double confidence) const {
    std::shared_lock lock(state_mutex_);
    auto input = build_adaptive_safety_input(confidence);
    return safety_checker_.check_safety_probabilistic(input, confidence);
}

ProbabilisticSafetyResult ResourceManager::check_safety_probabilistic() const {
    return check_safety_probabilistic(config_.adaptive.default_confidence_level);
}

RequestStatus ResourceManager::request_resources_adaptive(
    AgentId agent_id,
    ResourceTypeId resource_type,
    ResourceQuantity quantity,
    std::optional<Duration> timeout)
{
    // Validate: agent must exist, resource must exist
    {
        std::shared_lock lock(state_mutex_);
        auto agent_it = agents_.find(agent_id);
        if (agent_it == agents_.end()) {
            throw AgentNotFoundException(agent_id);
        }
        auto res_it = resources_.find(resource_type);
        if (res_it == resources_.end()) {
            throw ResourceNotFoundException(resource_type);
        }
        if (quantity > res_it->second.total_capacity()) {
            throw ResourceCapacityExceededException(resource_type, quantity,
                                                     res_it->second.total_capacity());
        }

        // For adaptive/hybrid mode agents, skip max-claim check.
        // For static mode, enforce the normal check.
        DemandMode mode = demand_estimator_.get_agent_demand_mode(agent_id);
        if (mode == DemandMode::Static) {
            auto max_it = agent_it->second.max_needs().find(resource_type);
            if (max_it != agent_it->second.max_needs().end()) {
                auto alloc = agent_it->second.current_allocation();
                auto alloc_it = alloc.find(resource_type);
                ResourceQuantity current = (alloc_it != alloc.end()) ? alloc_it->second : 0;
                if (current + quantity > max_it->second) {
                    throw MaxClaimExceededException(agent_id, resource_type,
                                                     quantity, max_it->second);
                }
            }
        }
    }

    emit_event(EventType::RequestSubmitted, "Adaptive request submitted",
               agent_id, resource_type, std::nullopt, quantity);

    demand_estimator_.record_request(agent_id, resource_type, quantity);

    // Try to grant immediately using adaptive safety check
    {
        std::unique_lock lock(state_mutex_);
        auto& res = resources_.at(resource_type);

        if (res.available() >= quantity) {
            auto input = build_adaptive_safety_input(config_.adaptive.default_confidence_level);
            auto t0 = std::chrono::steady_clock::now();
            auto result = safety_checker_.check_hypothetical_probabilistic(
                input, agent_id, resource_type, quantity,
                config_.adaptive.default_confidence_level);
            auto t1 = std::chrono::steady_clock::now();
            double dur_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

            emit_event(EventType::ProbabilisticSafetyCheck, result.reason,
                       agent_id, resource_type, std::nullopt, quantity,
                       result.is_safe, dur_us);

            if (result.is_safe) {
                res.allocate(quantity);
                agents_.at(agent_id).allocate(resource_type, quantity);
                auto alloc = agents_.at(agent_id).current_allocation();
                auto a_it = alloc.find(resource_type);
                ResourceQuantity level = (a_it != alloc.end()) ? a_it->second : 0;
                lock.unlock();
                demand_estimator_.record_allocation_level(agent_id, resource_type, level);
                emit_event(EventType::RequestGranted, "Adaptive request granted immediately",
                           agent_id, resource_type, std::nullopt, quantity);
                return RequestStatus::Granted;
            } else {
                emit_event(EventType::UnsafeStateDetected,
                          "Adaptive: would create unsafe state", agent_id, resource_type);
            }
        }
    }

    // Wait with timeout
    Duration wait_timeout = timeout.value_or(config_.default_request_timeout);
    auto deadline = Clock::now() + wait_timeout;

    while (Clock::now() < deadline) {
        {
            std::unique_lock lock(state_mutex_);

            auto res_it = resources_.find(resource_type);
            if (res_it == resources_.end()) return RequestStatus::Denied;
            auto agent_it = agents_.find(agent_id);
            if (agent_it == agents_.end()) return RequestStatus::Denied;

            if (res_it->second.available() >= quantity) {
                auto input = build_adaptive_safety_input(config_.adaptive.default_confidence_level);
                auto t0 = std::chrono::steady_clock::now();
                auto result = safety_checker_.check_hypothetical_probabilistic(
                    input, agent_id, resource_type, quantity,
                    config_.adaptive.default_confidence_level);
                auto t1 = std::chrono::steady_clock::now();
                double dur_us = std::chrono::duration<double, std::micro>(t1 - t0).count();

                emit_event(EventType::ProbabilisticSafetyCheck, result.reason,
                           agent_id, resource_type, std::nullopt, quantity,
                           result.is_safe, dur_us);

                if (result.is_safe) {
                    res_it->second.allocate(quantity);
                    agent_it->second.allocate(resource_type, quantity);
                    auto alloc = agent_it->second.current_allocation();
                    auto a_it = alloc.find(resource_type);
                    ResourceQuantity level = (a_it != alloc.end()) ? a_it->second : 0;
                    lock.unlock();
                    demand_estimator_.record_allocation_level(agent_id, resource_type, level);
                    emit_event(EventType::RequestGranted, "Adaptive request granted after waiting",
                               agent_id, resource_type, std::nullopt, quantity);
                    return RequestStatus::Granted;
                }

                if (!running_.load()) {
                    lock.unlock();
                    emit_event(EventType::RequestDenied,
                              "Adaptive: unsafe state and no processor running",
                              agent_id, resource_type, std::nullopt, quantity);
                    return RequestStatus::Denied;
                }
            }

            auto remaining = deadline - Clock::now();
            if (remaining <= Duration::zero()) break;
            release_cv_.wait_for(lock, std::min(remaining, config_.processor_poll_interval));
        }
    }

    emit_event(EventType::RequestTimedOut, "Adaptive request timed out",
               agent_id, resource_type, std::nullopt, quantity);
    return RequestStatus::TimedOut;
}

// ==================== Internal Helpers (new) ====================

SafetyCheckInput ResourceManager::build_adaptive_safety_input(double confidence_level) const {
    // Caller must hold state_mutex_ (shared or exclusive)
    SafetyCheckInput input;

    for (auto& [id, res] : resources_) {
        input.total[id] = res.total_capacity();
        input.available[id] = res.available();
    }

    // Get estimated max needs from DemandEstimator
    auto estimated = demand_estimator_.estimate_all_max_needs(confidence_level);

    for (auto& [id, agent] : agents_) {
        input.allocation[id] = agent.current_allocation();

        DemandMode mode = demand_estimator_.get_agent_demand_mode(id);

        if (mode == DemandMode::Static) {
            // Use declared max needs
            input.max_need[id] = agent.max_needs();
        } else if (mode == DemandMode::Adaptive) {
            // Use purely estimated max needs
            auto est_it = estimated.find(id);
            if (est_it != estimated.end()) {
                input.max_need[id] = est_it->second;
            }
            // Ensure max_need >= current allocation for each resource
            for (auto& [rt, alloc_qty] : input.allocation[id]) {
                if (input.max_need[id][rt] < alloc_qty) {
                    input.max_need[id][rt] = alloc_qty;
                }
            }
        } else {
            // Hybrid: min(estimated, declared) where declared exists, else estimated
            auto declared = agent.max_needs();
            auto est_it = estimated.find(id);
            auto& max = input.max_need[id];

            // Start with declared
            max = declared;

            // Override with estimated where available, capped by declared
            if (est_it != estimated.end()) {
                for (auto& [rt, est_qty] : est_it->second) {
                    auto decl_it = declared.find(rt);
                    if (decl_it != declared.end()) {
                        max[rt] = std::min(est_qty, decl_it->second);
                    } else {
                        max[rt] = est_qty;
                    }
                }
            }

            // Ensure max_need >= current allocation
            for (auto& [rt, alloc_qty] : input.allocation[id]) {
                if (max[rt] < alloc_qty) {
                    max[rt] = alloc_qty;
                }
            }
        }
    }

    return input;
}

// ==================== Event Emission ====================

void ResourceManager::emit_event(EventType type, const std::string& message,
                                   std::optional<AgentId> agent_id,
                                   std::optional<ResourceTypeId> resource_type,
                                   std::optional<RequestId> request_id,
                                   std::optional<ResourceQuantity> quantity,
                                   std::optional<bool> safety_result,
                                   std::optional<double> duration_us) {
    if (!monitor_) return;

    MonitorEvent event;
    event.type = type;
    event.timestamp = Clock::now();
    event.message = message;
    event.agent_id = agent_id;
    event.resource_type = resource_type;
    event.request_id = request_id;
    event.quantity = quantity;
    event.safety_result = safety_result;
    event.duration_us = duration_us;

    monitor_->on_event(event);
}

} // namespace agentguard
