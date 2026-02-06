#pragma once

#include "agentguard/types.hpp"
#include "agentguard/resource.hpp"
#include "agentguard/agent.hpp"
#include "agentguard/safety_checker.hpp"
#include "agentguard/request_queue.hpp"
#include "agentguard/monitor.hpp"
#include "agentguard/policy.hpp"
#include "agentguard/config.hpp"

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace agentguard {

class ResourceManager {
public:
    explicit ResourceManager(Config config = Config{});
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // ==================== Resource Registration ====================

    void register_resource(Resource resource);
    bool unregister_resource(ResourceTypeId id);
    bool adjust_resource_capacity(ResourceTypeId id, ResourceQuantity new_capacity);
    std::optional<Resource> get_resource(ResourceTypeId id) const;
    std::vector<Resource> get_all_resources() const;

    // ==================== Agent Lifecycle ====================

    AgentId register_agent(Agent agent);
    bool deregister_agent(AgentId id);
    bool update_agent_max_claim(AgentId id, ResourceTypeId resource_type,
                                 ResourceQuantity new_max);
    std::optional<Agent> get_agent(AgentId id) const;
    std::vector<Agent> get_all_agents() const;
    std::size_t agent_count() const;

    // ==================== Synchronous Resource Requests ====================

    RequestStatus request_resources(
        AgentId agent_id,
        ResourceTypeId resource_type,
        ResourceQuantity quantity,
        std::optional<Duration> timeout = std::nullopt);

    RequestStatus request_resources_batch(
        AgentId agent_id,
        const std::unordered_map<ResourceTypeId, ResourceQuantity>& requests,
        std::optional<Duration> timeout = std::nullopt);

    // ==================== Asynchronous Resource Requests ====================

    std::future<RequestStatus> request_resources_async(
        AgentId agent_id,
        ResourceTypeId resource_type,
        ResourceQuantity quantity,
        std::optional<Duration> timeout = std::nullopt);

    RequestId request_resources_callback(
        AgentId agent_id,
        ResourceTypeId resource_type,
        ResourceQuantity quantity,
        RequestCallback callback,
        std::optional<Duration> timeout = std::nullopt);

    // ==================== Resource Release ====================

    void release_resources(AgentId agent_id, ResourceTypeId resource_type,
                          ResourceQuantity quantity);
    void release_all_resources(AgentId agent_id, ResourceTypeId resource_type);
    void release_all_resources(AgentId agent_id);

    // ==================== Queries ====================

    bool is_safe() const;
    SystemSnapshot get_snapshot() const;
    std::size_t pending_request_count() const;

    // ==================== Configuration ====================

    void set_scheduling_policy(std::unique_ptr<SchedulingPolicy> policy);
    void set_monitor(std::shared_ptr<Monitor> monitor);

    void start();
    void stop();
    bool is_running() const noexcept;

private:
    Config config_;

    // Core state (Banker's Algorithm matrices)
    mutable std::shared_mutex state_mutex_;
    std::unordered_map<ResourceTypeId, Resource> resources_;
    std::unordered_map<AgentId, Agent> agents_;

    // Sub-components
    SafetyChecker safety_checker_;
    RequestQueue request_queue_;
    std::unique_ptr<SchedulingPolicy> scheduling_policy_;
    std::shared_ptr<Monitor> monitor_;

    // Background processor
    std::thread processor_thread_;
    std::atomic<bool> running_{false};
    std::condition_variable_any release_cv_;

    // ID generators
    AgentId next_agent_id_{1};

    // Internal helpers
    SafetyCheckInput build_safety_input() const;
    void process_queue_loop();
    void try_grant_pending_requests();
    bool try_grant_single(AgentId agent_id, ResourceTypeId resource_type,
                          ResourceQuantity quantity);
    void emit_event(EventType type, const std::string& message,
                    std::optional<AgentId> agent_id = std::nullopt,
                    std::optional<ResourceTypeId> resource_type = std::nullopt,
                    std::optional<RequestId> request_id = std::nullopt,
                    std::optional<ResourceQuantity> quantity = std::nullopt,
                    std::optional<bool> safety_result = std::nullopt);
};

} // namespace agentguard
