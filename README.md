# AgentGuard

**Deadlock prevention for multi-AI-agent systems -- beyond the textbook.**

AgentGuard is a C++17 library that started as a clean implementation of the [Banker's Algorithm](https://en.wikipedia.org/wiki/Banker%27s_algorithm) (Dijkstra, 1965) for preventing deadlocks when multiple AI agents compete for shared resources. But classical Banker's has real gaps when applied to AI agents. We identified three, and built solutions for each:

1. **Agents get stuck with no one noticing.** The #1 complaint across LangGraph, CrewAI, and AutoGen is infinite loops. Current solutions are dumb counters (kill after N steps). AgentGuard's **Progress Monitor** detects stuck agents using progress invariants and auto-releases their resources.

2. **Authority deadlocks are invisible.** Agent A delegates to B, B delegates to C, C delegates back to A -- everyone is politely waiting, no resource is held, and no existing tool detects it. AgentGuard's **Delegation Tracker** maintains a directed graph of active delegations and detects cycles in real time.

3. **AI agents don't know their max resource needs.** Banker's requires every process to declare its maximum needs upfront. That's fine for OS processes, but an LLM agent has no idea how many API calls it will make. AgentGuard's **Demand Estimator** learns resource patterns at runtime and runs a probabilistic Banker's Algorithm -- no upfront declarations needed.

The core guarantee still holds: before granting any resource request, AgentGuard checks whether doing so would leave the system in a *safe state*. If granting would risk deadlock, the request is queued or denied. This works with agents joining and leaving at runtime, concurrent multi-threaded access, and multiple resource types requested atomically.

## Table of Contents

- [The Problem](#the-problem)
- [How It Works](#how-it-works)
- [What Makes AgentGuard Different](#what-makes-agentguard-different)
- [Quick Start](#quick-start)
- [Building](#building)
- [API Reference](#api-reference)
  - [ResourceManager](#resourcemanager)
  - [Agent](#agent)
  - [Resource](#resource)
  - [SafetyChecker](#safetychecker)
  - [Progress Monitoring](#progress-monitoring)
  - [Delegation Tracking](#delegation-tracking)
  - [Adaptive Demands](#adaptive-demands)
  - [Scheduling Policies](#scheduling-policies)
  - [Monitoring](#monitoring)
  - [AI-Specific Resource Types](#ai-specific-resource-types)
  - [Configuration](#configuration)
  - [Exceptions](#exceptions)
- [Examples](#examples)
- [Architecture](#architecture)
- [Testing](#testing)
- [Project Structure](#project-structure)
- [Requirements](#requirements)
- [Integration](#integration)
- [License](#license)

## The Problem

When multiple AI agents operate concurrently, they share limited resources:

| Resource | Example |
|---|---|
| API rate limits | 60 OpenAI requests/minute shared across 5 agents |
| Tool access | A code interpreter that only 1 agent can use at a time |
| Token budgets | 100K tokens/minute pooled across agents |
| Database connections | 10 connections shared by 20 agents |
| GPU compute | 4 GPU slots shared by 8 training jobs |

Without coordination, agents deadlock. Agent A holds the code interpreter and waits for the browser. Agent B holds the browser and waits for the code interpreter. Neither can proceed.

AgentGuard eliminates this class of failure.

## How It Works

AgentGuard adapts the Banker's Algorithm from operating systems theory:

1. **Each agent declares its maximum resource needs** when it registers -- or, in adaptive mode, the system learns them automatically from usage patterns.
2. **Before granting any request**, the `SafetyChecker` simulates: "If I grant this, can all agents still complete?" It does this by iteratively finding agents whose remaining needs can be met with available resources, simulating their completion, and reclaiming their resources.
3. **If the resulting state is safe** (a valid completion sequence exists), the request is granted immediately.
4. **If the resulting state is unsafe**, the request is queued and re-evaluated whenever resources are released.
5. **Agents join and leave dynamically** -- unlike classical Banker's, which assumes a fixed process set.
6. **Stuck agents are detected** via progress monitoring, and their resources are auto-released to unblock others.
7. **Authority deadlock cycles** (A delegates to B delegates to C delegates to A) are detected in real time and can be automatically broken.

The safety check runs in O(n^2 * m) time where n = number of agents and m = number of resource types. For typical multi-agent systems (n < 100, m < 50), this completes in microseconds.

## What Makes AgentGuard Different

We started with a textbook Banker's Algorithm. It worked, but it wasn't enough. Real AI agent systems fail in ways that Dijkstra never anticipated.

### The journey

**V1: Classical Banker's.** We implemented the full algorithm with dynamic agent registration, multi-resource batch requests, pluggable scheduling policies, and comprehensive monitoring. 109 tests, clean architecture. But when we looked at how multi-agent systems actually fail in production, three gaps became obvious.

**Gap 1: Agents hang and nobody notices.** An LLM agent enters an infinite reasoning loop, or a tool call blocks forever. The agent holds resources, other agents wait, and the whole system grinds to a halt. Every framework has this problem -- LangGraph, CrewAI, AutoGen. Their solution? Kill after N iterations. That's a timer, not a safety system.

**Gap 2: Authority deadlocks.** Agent A says "I need B to handle this." B says "C is better suited." C says "Let me check with A." Nobody holds a resource. Nobody is blocked in the traditional sense. But the system is completely stuck. No existing tool detects this.

**Gap 3: Banker's requires crystal balls.** The algorithm needs every process to declare its maximum resource needs upfront. An OS process can do this (it knows it needs at most N file descriptors). An LLM agent cannot -- it has no idea how many API calls a complex research task will require. This makes classical Banker's impractical for AI.

**V2: What we built.**

| Feature | Solves | How |
|---------|--------|-----|
| **Progress Monitor** | Gap 1: Stuck agents | Tracks named progress metrics per agent. A background thread detects stalls (no progress within a configurable threshold). On stall, resources are auto-released and events are emitted. |
| **Delegation Tracker** | Gap 2: Authority deadlocks | Maintains a directed graph of active delegations. On each new delegation, runs BFS cycle detection from the target back to the source. Configurable actions: notify, reject the delegation, or cancel the latest edge. |
| **Demand Estimator** | Gap 3: Unknown max needs | Records every resource request per agent. Computes a statistical estimate of max needs: `mean + k * stddev` where k comes from the desired confidence level (inverse normal CDF). Runs Banker's with these estimates instead of declared maximums. Cold-start handling included. |

All three features are opt-in (disabled by default), backward-compatible, and independently toggleable. The original 109 tests pass unchanged.

## Quick Start

```cpp
#include <agentguard/agentguard.hpp>
#include <iostream>

using namespace agentguard;
using namespace std::chrono_literals;

int main() {
    // 1. Create the resource manager
    ResourceManager manager;

    // 2. Register shared resources
    manager.register_resource(Resource(1, "API-Slots", ResourceCategory::ApiRateLimit, 10));
    manager.register_resource(Resource(2, "Tool-Access", ResourceCategory::ToolSlot, 2));

    // 3. Register agents with their maximum resource needs
    Agent agent_a(0, "ResearchAgent", PRIORITY_HIGH);
    agent_a.declare_max_need(1, 5);   // needs at most 5 API slots
    agent_a.declare_max_need(2, 1);   // needs at most 1 tool slot
    AgentId id_a = manager.register_agent(agent_a);

    Agent agent_b(0, "SummaryAgent", PRIORITY_NORMAL);
    agent_b.declare_max_need(1, 4);   // needs at most 4 API slots
    agent_b.declare_max_need(2, 1);   // needs at most 1 tool slot
    AgentId id_b = manager.register_agent(agent_b);

    // 4. Start the background processor
    manager.start();

    // 5. Request resources -- the Banker's Algorithm ensures safety
    auto status = manager.request_resources(id_a, 1, 3, 5s);  // 3 API slots
    if (status == RequestStatus::Granted) {
        // ... do work ...
        manager.release_resources(id_a, 1, 3);
    }

    // 6. Atomic multi-resource request (all-or-nothing)
    auto batch_status = manager.request_resources_batch(id_b,
        {{1, 2}, {2, 1}},   // 2 API slots AND 1 tool slot
        5s);

    if (batch_status == RequestStatus::Granted) {
        // ... do work with both resources ...
        manager.release_all_resources(id_b);
    }

    manager.stop();
    return 0;
}
```

## Building

### Prerequisites

- C++17 compiler (GCC 7+, Clang 5+, AppleClang 10+, MSVC 19.14+)
- CMake 3.16+
- (Tests only) Internet connection for GoogleTest download via FetchContent

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

### Build Options

| Option | Default | Description |
|---|---|---|
| `AGENTGUARD_BUILD_TESTS` | `ON` | Build unit and integration tests |
| `AGENTGUARD_BUILD_EXAMPLES` | `ON` | Build example programs |
| `AGENTGUARD_BUILD_BENCHMARKS` | `OFF` | Build benchmark programs |
| `AGENTGUARD_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `AGENTGUARD_ENABLE_TSAN` | `OFF` | Enable ThreadSanitizer |
| `AGENTGUARD_ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |
| `AGENTGUARD_INSTALL` | `ON` | Generate install targets |

### Build with sanitizers (recommended for development)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DAGENTGUARD_ENABLE_TSAN=ON
cmake --build . --parallel
ctest --output-on-failure
```

### Run tests

```bash
cd build
ctest --output-on-failure
```

All 189 tests should pass.

### Run examples

```bash
cd build
./examples/example_basic
./examples/example_llm_rate_limits
./examples/example_tool_sharing
./examples/example_priority_agents
./examples/example_adaptive_agents
```

### Install

```bash
cmake --install . --prefix /usr/local
```

## API Reference

### ResourceManager

The central coordinator. Manages agents, resources, and the Banker's Algorithm.

```cpp
#include <agentguard/resource_manager.hpp>

// Construction
ResourceManager manager;                    // default config
ResourceManager manager(Config{});          // custom config

// Resource registration
manager.register_resource(Resource(1, "MyResource", ResourceCategory::Custom, 10));
manager.unregister_resource(1);                        // fails if any agent holds it
manager.adjust_resource_capacity(1, 20);               // dynamic scaling
std::optional<Resource> r = manager.get_resource(1);   // query
std::vector<Resource> all = manager.get_all_resources();

// Agent lifecycle
Agent a(0, "MyAgent", PRIORITY_HIGH);
a.declare_max_need(1, 5);
AgentId id = manager.register_agent(a);       // returns assigned ID
manager.deregister_agent(id);                 // releases all held resources
manager.update_agent_max_claim(id, 1, 3);     // reduce max claim (must be >= current alloc)
std::optional<Agent> agent = manager.get_agent(id);
std::vector<Agent> agents = manager.get_all_agents();
std::size_t count = manager.agent_count();

// Synchronous requests (blocking)
RequestStatus s = manager.request_resources(id, resource_type, quantity, timeout);
RequestStatus s = manager.request_resources_batch(id, {{rt1, qty1}, {rt2, qty2}}, timeout);

// Asynchronous requests
std::future<RequestStatus> f = manager.request_resources_async(id, rt, qty, timeout);
RequestId rid = manager.request_resources_callback(id, rt, qty, callback, timeout);

// Release
manager.release_resources(id, resource_type, quantity);
manager.release_all_resources(id, resource_type);  // release all of one type
manager.release_all_resources(id);                 // release everything

// Queries
bool safe = manager.is_safe();
SystemSnapshot snap = manager.get_snapshot();
std::size_t pending = manager.pending_request_count();

// Configuration
manager.set_scheduling_policy(std::make_unique<PriorityPolicy>());
manager.set_monitor(std::make_shared<ConsoleMonitor>());
manager.start();   // launch background queue processor thread
manager.stop();    // drain queue and stop
bool running = manager.is_running();
```

#### Request behavior

| Scenario | Behavior |
|---|---|
| Resources available, state safe | `Granted` immediately |
| Resources available, state unsafe, processor running | Queued, retried on each release until timeout |
| Resources available, state unsafe, processor **not** running | `Denied` immediately |
| Resources unavailable, processor running | Queued until available and safe, or timeout |
| Request exceeds agent's declared max claim | Throws `MaxClaimExceededException` |
| Request exceeds resource total capacity | Throws `ResourceCapacityExceededException` |
| Agent not found | Throws `AgentNotFoundException` |
| Resource type not found | Throws `ResourceNotFoundException` |

### Agent

Represents an AI agent in the system.

```cpp
#include <agentguard/agent.hpp>

Agent a(0, "MyAgent", PRIORITY_HIGH);       // id=0 means "assign me one"

// Declare maximum resource needs (required before requesting)
a.declare_max_need(1, 5);                   // resource type 1, up to 5 units
a.declare_max_need(2, 3);                   // resource type 2, up to 3 units

// Priority levels
a.set_priority(PRIORITY_CRITICAL);          // PRIORITY_LOW=0, NORMAL=50, HIGH=100, CRITICAL=200

// AI-specific metadata
a.set_model_identifier("claude-opus-4-6");
a.set_task_description("Research recent papers on transformers");

// Queries
AgentId id = a.id();
const std::string& name = a.name();
Priority p = a.priority();
AgentState state = a.state();               // Registered, Active, Waiting, Releasing, Deregistered
ResourceQuantity need = a.remaining_need(1);
const auto& alloc = a.current_allocation(); // map of resource_type -> quantity held
const auto& maxn = a.max_needs();           // map of resource_type -> max declared need
```

### Resource

Represents a shared resource type.

```cpp
#include <agentguard/resource.hpp>

Resource r(1, "OpenAI-API", ResourceCategory::ApiRateLimit, 60);

// Queries
ResourceTypeId id = r.id();                 // 1
const std::string& name = r.name();         // "OpenAI-API"
ResourceCategory cat = r.category();        // ResourceCategory::ApiRateLimit
ResourceQuantity total = r.total_capacity(); // 60
ResourceQuantity alloc = r.allocated();     // 0 (initially)
ResourceQuantity avail = r.available();     // 60 (initially)

// Dynamic capacity adjustment
bool ok = r.set_total_capacity(100);        // false if new_capacity < allocated

// AI-specific metadata
r.set_replenish_interval(std::chrono::minutes(1));   // for rate-limited resources
r.set_cost_per_unit(0.003);                          // for usage accounting
```

#### Resource categories

```cpp
enum class ResourceCategory {
    ApiRateLimit,    // API calls per time window
    TokenBudget,     // LLM token allocation
    ToolSlot,        // Tool access (code interpreter, browser, etc.)
    MemoryPool,      // Shared memory / context window
    DatabaseConn,    // Database connection pool
    GpuCompute,      // GPU compute units
    FileHandle,      // File system handles
    NetworkSocket,   // Network connections
    Custom           // User-defined
};
```

### SafetyChecker

The core Banker's Algorithm implementation. Stateless and thread-safe (no internal locking needed).

```cpp
#include <agentguard/safety_checker.hpp>

SafetyChecker checker;

// Build a state snapshot
SafetyCheckInput input;
input.total[1] = 10;
input.available[1] = 3;
input.allocation[agent_1][1] = 4;
input.max_need[agent_1][1] = 7;
input.allocation[agent_2][1] = 3;
input.max_need[agent_2][1] = 9;

// Core safety check
SafetyCheckResult result = checker.check_safety(input);
// result.is_safe       -- true if a safe sequence exists
// result.safe_sequence -- one valid completion order [agent_1, agent_2]
// result.reason        -- human-readable explanation

// Hypothetical: "If I grant 2 units of resource 1 to agent_1, is it still safe?"
SafetyCheckResult h = checker.check_hypothetical(input, agent_1, 1, 2);

// Batch hypothetical: "If I grant all of these simultaneously?"
std::vector<ResourceRequest> batch = { ... };
SafetyCheckResult hb = checker.check_hypothetical_batch(input, batch);

// Which of these pending requests can be safely granted?
std::vector<RequestId> grantable = checker.find_grantable_requests(input, candidates);

// Which agents are resource bottlenecks? (sorted by impact, descending)
std::vector<AgentId> bottlenecks = checker.identify_bottleneck_agents(input);
```

### Progress Monitoring

Detect stuck agents and auto-release their resources.

```cpp
// Enable in config
Config cfg;
cfg.progress.enabled = true;
cfg.progress.default_stall_threshold = std::chrono::seconds(120);
cfg.progress.check_interval = std::chrono::seconds(5);
cfg.progress.auto_release_on_stall = true;   // free resources from stuck agents

ResourceManager manager(cfg);
manager.start();

// Agents report progress as they work
manager.report_progress(agent_id, "steps_completed", 5);
manager.report_progress(agent_id, "tokens_generated", 1200);

// Per-agent stall threshold override
manager.set_agent_stall_threshold(agent_id, std::chrono::seconds(30));

// Query stall state
bool stuck = manager.is_agent_stalled(agent_id);
std::vector<AgentId> stalled = manager.get_stalled_agents();
```

If an agent stops reporting progress for longer than its stall threshold, AgentGuard emits `AgentStalled` and (if configured) releases all resources held by that agent. When the agent resumes progress, `AgentStallResolved` is emitted.

### Delegation Tracking

Detect authority deadlock cycles where agents delegate to each other in a loop.

```cpp
// Enable in config
Config cfg;
cfg.delegation.enabled = true;
cfg.delegation.cycle_action = DelegationCycleAction::RejectDelegation;
// Options: NotifyOnly, RejectDelegation, CancelLatest

ResourceManager manager(cfg);

// Report delegations as they happen
DelegationResult r = manager.report_delegation(agent_a, agent_b, "Summarize document");
// r.accepted     -- true if the delegation was added
// r.cycle_detected -- true if this delegation would create a cycle
// r.cycle_path   -- the cycle (e.g., [A, B, C, A])

// Complete or cancel delegations
manager.complete_delegation(agent_a, agent_b);
manager.cancel_delegation(agent_b, agent_c);

// Query delegation state
std::vector<DelegationInfo> all = manager.get_all_delegations();
std::optional<std::vector<AgentId>> cycle = manager.find_delegation_cycle();
```

| Cycle Action | Behavior |
|---|---|
| `NotifyOnly` | Accept the delegation, emit `DelegationCycleDetected` event |
| `RejectDelegation` | Refuse to add the edge, return `accepted=false` |
| `CancelLatest` | Add then immediately remove the edge, emit `DelegationCancelled` |

### Adaptive Demands

Run Banker's Algorithm without requiring agents to declare max resource needs upfront.

```cpp
// Enable in config
Config cfg;
cfg.adaptive.enabled = true;
cfg.adaptive.default_confidence_level = 0.95;   // 95th percentile estimate
cfg.adaptive.cold_start_default_demand = 3;      // assume 3 until we have data
cfg.adaptive.cold_start_headroom_factor = 1.5;   // multiply first observation by 1.5x
cfg.adaptive.adaptive_headroom_factor = 1.2;     // cap at 1.2x observed max cumulative

ResourceManager manager(cfg);

// Set agents to adaptive mode (no declare_max_need needed)
manager.set_agent_demand_mode(agent_id, DemandMode::Adaptive);

// Use adaptive resource requests
auto status = manager.request_resources_adaptive(agent_id, resource_type, 3, 5s);

// Check system safety probabilistically
ProbabilisticSafetyResult result = manager.check_safety_probabilistic(0.95);
// result.is_safe            -- safe at this confidence level?
// result.confidence_level   -- the confidence used
// result.estimated_max_needs -- what the estimator computed per agent
// result.safe_sequence      -- completion order (if safe)

// No-argument version uses config default confidence
auto result2 = manager.check_safety_probabilistic();
```

Three demand modes are available per agent:

| Mode | Behavior |
|---|---|
| `Static` | Classical Banker's: uses `declare_max_need()` only (default, backward-compatible) |
| `Adaptive` | No upfront declaration needed. Max needs estimated from usage history. |
| `Hybrid` | Uses the minimum of the statistical estimate and the declared max need. |

### Scheduling Policies

Control the order in which queued requests are processed. All implement the `SchedulingPolicy` interface.

```cpp
#include <agentguard/policy.hpp>

// Set on the ResourceManager
manager.set_scheduling_policy(std::make_unique<PriorityPolicy>());
```

| Policy | Behavior |
|---|---|
| `FifoPolicy` | First-come, first-served (default) |
| `PriorityPolicy` | Higher-priority agents served first, FIFO within same priority |
| `ShortestNeedPolicy` | Agents closest to finishing go first (maximizes throughput) |
| `DeadlinePolicy` | Requests with nearest timeout deadline go first |
| `FairnessPolicy` | Longest-waiting requests go first (prevents starvation) |

#### Custom policies

```cpp
class MyPolicy : public SchedulingPolicy {
public:
    std::vector<ResourceRequest> prioritize(
        const std::vector<ResourceRequest>& pending,
        const SystemSnapshot& state) const override
    {
        auto result = pending;
        // ... your ordering logic ...
        return result;
    }

    std::string name() const override { return "MyPolicy"; }
};

manager.set_scheduling_policy(std::make_unique<MyPolicy>());
```

### Monitoring

Observe every significant event in the system.

```cpp
#include <agentguard/monitor.hpp>

// Console logger with verbosity levels
manager.set_monitor(
    std::make_shared<ConsoleMonitor>(ConsoleMonitor::Verbosity::Verbose));
    // Verbosity: Quiet, Normal, Verbose, Debug

// Metrics collector
auto metrics_mon = std::make_shared<MetricsMonitor>();
manager.set_monitor(metrics_mon);

// Later, query collected metrics
MetricsMonitor::Metrics m = metrics_mon->get_metrics();
// m.total_requests, m.granted_requests, m.denied_requests,
// m.timed_out_requests, m.unsafe_state_detections,
// m.resource_utilization_percent

// Threshold alerts
metrics_mon->set_utilization_alert_threshold(0.9, [](const std::string& msg) {
    std::cerr << "ALERT: " << msg << "\n";
});
metrics_mon->set_queue_size_alert_threshold(100, [](const std::string& msg) {
    std::cerr << "ALERT: " << msg << "\n";
});

// Combine multiple monitors
auto composite = std::make_shared<CompositeMonitor>();
composite->add_monitor(std::make_shared<ConsoleMonitor>());
composite->add_monitor(metrics_mon);
manager.set_monitor(composite);
```

#### Event types

```
// Core events
AgentRegistered, AgentDeregistered, ResourceRegistered, ResourceCapacityChanged,
RequestSubmitted, RequestGranted, RequestDenied, RequestTimedOut, RequestCancelled,
ResourcesReleased, SafetyCheckPerformed, UnsafeStateDetected, QueueSizeChanged,

// Progress monitoring
AgentProgressReported, AgentStalled, AgentStallResolved, AgentResourcesAutoReleased,

// Delegation tracking
DelegationReported, DelegationCompleted, DelegationCancelled, DelegationCycleDetected,

// Adaptive demands
DemandEstimateUpdated, ProbabilisticSafetyCheck, AdaptiveDemandModeChanged
```

#### Custom monitors

```cpp
class SlackMonitor : public Monitor {
public:
    void on_event(const MonitorEvent& event) override {
        if (event.type == EventType::UnsafeStateDetected) {
            // send Slack alert
        }
    }
    void on_snapshot(const SystemSnapshot& snapshot) override {
        // post dashboard update
    }
};
```

### AI-Specific Resource Types

Higher-level resource types with AI-relevant metadata. Each produces a `Resource` via `.as_resource()`.

#### TokenBudget

```cpp
#include <agentguard/ai/token_budget.hpp>

using namespace agentguard::ai;

TokenBudget budget(1, "GPT4-Tokens", 100000, std::chrono::minutes(1));
budget.set_input_output_ratio(0.7);   // 70% input, 30% output

manager.register_resource(budget.as_resource());

// Queries
double rate = budget.tokens_per_second_rate();   // ~1666.67
```

#### RateLimiter

```cpp
#include <agentguard/ai/rate_limiter.hpp>

RateLimiter limiter(2, "OpenAI-API", 60, RateLimiter::WindowType::PerMinute);
limiter.set_burst_allowance(10);                          // allow bursts up to 70
limiter.add_endpoint_sublimit("/v1/chat/completions", 40); // endpoint sub-limit

manager.register_resource(limiter.as_resource());
// Resource capacity = requests_per_window + burst_allowance = 70
```

#### ToolSlot

```cpp
#include <agentguard/ai/tool_slot.hpp>

// Exclusive tool: only 1 agent at a time
ToolSlot interpreter(3, "CodeInterpreter", ToolSlot::AccessMode::Exclusive);

// Concurrent tool: up to 3 agents
ToolSlot browser(4, "WebBrowser", ToolSlot::AccessMode::Concurrent, 3);

browser.set_estimated_usage_duration(std::chrono::seconds(30));
browser.set_fallback_tool(5);   // try tool ID 5 if browser is full

manager.register_resource(interpreter.as_resource());
manager.register_resource(browser.as_resource());
```

#### MemoryPool

```cpp
#include <agentguard/ai/memory_pool.hpp>

MemoryPool pool(5, "SharedContext", 1024, MemoryPool::MemoryUnit::Megabytes);
pool.set_eviction_policy("LRU");
pool.set_fragmentation_threshold(0.3);

manager.register_resource(pool.as_resource());
// MemoryUnit: Bytes, Kilobytes, Megabytes, Tokens, Entries
```

### Configuration

```cpp
#include <agentguard/config.hpp>

Config cfg;
cfg.max_agents = 1024;                                   // max concurrent agents
cfg.max_resource_types = 256;                             // max resource types
cfg.max_queue_size = 10000;                               // request queue capacity
cfg.default_request_timeout = std::chrono::seconds(30);   // default blocking timeout
cfg.processor_poll_interval = std::chrono::milliseconds(10); // queue check interval
cfg.snapshot_interval = std::chrono::seconds(5);          // monitor snapshot interval
cfg.enable_timeout_expiration = true;                     // expire queued requests
cfg.starvation_threshold = std::chrono::seconds(60);      // starvation warning
cfg.thread_safe = true;                                   // set false for single-threaded use

ResourceManager manager(cfg);
```

### Exceptions

All exceptions inherit from `AgentGuardException` (which inherits from `std::runtime_error`).

| Exception | Thrown when |
|---|---|
| `AgentNotFoundException` | Operating on an unregistered agent ID |
| `ResourceNotFoundException` | Operating on an unregistered resource type ID |
| `MaxClaimExceededException` | Requesting more than the agent's declared max need |
| `ResourceCapacityExceededException` | Requesting more than the resource's total capacity |
| `QueueFullException` | Enqueueing to a full request queue |
| `AgentAlreadyRegisteredException` | Registering an agent with a duplicate ID |

```cpp
try {
    manager.request_resources(agent_id, resource_type, quantity);
} catch (const MaxClaimExceededException& e) {
    std::cerr << e.what() << "\n";
    // "Agent 3 requested 10 of resource 1 but max claim is 5"
} catch (const AgentNotFoundException& e) {
    std::cerr << "Unknown agent: " << e.agent_id() << "\n";
}
```

## Examples

### 01 -- Basic Usage

Minimal example: 2 resources, 3 agents, sequential requests with a `ConsoleMonitor` showing the Banker's Algorithm decisions in real time.

```bash
./examples/example_basic
```

### 02 -- LLM API Rate Limits

3 LLM agents (Researcher, Summarizer, Indexer) at different priority levels sharing OpenAI and Anthropic API rate limits. Uses `RateLimiter`, `PriorityPolicy`, and `MetricsMonitor` to track throughput.

```bash
./examples/example_llm_rate_limits
```

### 03 -- Tool Sharing

4 agents sharing a code interpreter (exclusive, 1 slot), web browser (concurrent, 2 slots), and filesystem (concurrent, 3 slots). Demonstrates that the Banker's Algorithm prevents the classic tool-sharing deadlock where agents hold some tools and wait for others.

```bash
./examples/example_tool_sharing
```

### 04 -- Priority Agents

4 agents at CRITICAL, HIGH, NORMAL, and LOW priority competing for a scarce token budget. Shows that `PriorityPolicy` serves high-priority agents first, with `MetricsMonitor` alerts on utilization thresholds.

```bash
./examples/example_priority_agents
```

### 05 -- Adaptive Agents (all three novel features)

3 agents in adaptive demand mode (no `declare_max_need` calls) sharing API tokens and tool slots. Demonstrates all three novel features working together:

- **Adaptive demands**: agents request resources without upfront max declarations; a probabilistic safety check passes at 90% confidence.
- **Delegation cycle detection**: A delegates to B, B delegates to C, C tries to delegate back to A -- cycle detected and rejected.
- **Progress monitoring**: Agent B stops reporting progress, is detected as stalled within 200ms, and its resources are auto-released.

```bash
./examples/example_adaptive_agents
```

## Architecture

### Request processing flow

```
Agent Thread                 ResourceManager                SafetyChecker
    |                              |                              |
    |-- request_resources() ------>|                              |
    |                              |-- acquire shared_mutex ----  |
    |                              |-- build SafetyCheckInput     |
    |                              |-- check_hypothetical() ----->|
    |                              |                              |-- Banker's Algorithm
    |                              |<-- SafetyCheckResult --------|   O(n^2 * m)
    |                              |                              |
    |                         [if safe]                           |
    |                              |-- upgrade to write lock      |
    |                              |-- update allocation matrices |
    |<-- Granted ------------------|                              |
    |                              |                              |
    |                         [if unsafe]                         |
    |                              |-- queue request              |
    |                              |-- wait on condition_variable |
    |                              |   (re-checked on release)    |
```

### Concurrency design

- **`std::shared_mutex`** protects the Banker's matrices. Reads (safety checks, snapshots) take shared locks. Writes (allocations, registrations) take exclusive locks. This is optimal for read-heavy workloads.
- **`std::condition_variable_any`** wakes blocked request threads when resources are released.
- **Background processor thread** (`start()`/`stop()`) handles callback-based async requests and timeout expiration from the `RequestQueue`.
- **`SafetyChecker` is stateless** -- no internal locks, can be called concurrently.
- Each `Monitor` implementation handles its own thread safety.

### Key design decisions

| Decision | Rationale |
|---|---|
| `shared_mutex` | Read-heavy workload (safety checks >> writes) |
| Stateless `SafetyChecker` | Testable in isolation, no coupling to `ResourceManager` locking |
| `unordered_map` for matrices | Dynamic agent count (agents join/leave at runtime) |
| Pluggable `SchedulingPolicy` | Different deployments need different strategies |
| AI types in `ai/` subnamespace | Core stays generic; AI resources are opt-in |
| Header + source (not header-only) | Non-trivial implementation; faster downstream compiles |
| C++17 | Broad compiler support across GCC, Clang, MSVC |

## Testing

189 tests across unit, integration, and concurrent categories:

| Category | Tests | Coverage |
|---|---|---|
| **Unit: Resource** | 12 | Construction, capacity, metadata |
| **Unit: Agent** | 17 | Construction, max needs, allocation, metadata |
| **Unit: SafetyChecker** | 21 | Safe/unsafe states, hypothetical checks, batch, bottlenecks, edge cases |
| **Unit: ResourceManager** | 23 | Registration, requests, releases, batch, snapshots, exceptions |
| **Unit: RequestQueue** | 17 | Priority ordering, cancellation, timeouts, capacity |
| **Unit: Policy** | 10 | FIFO, Priority, Fairness, Deadline, ShortestNeed |
| **Unit: ProgressTracker** | 10 | Registration, stall detection/resolution, per-agent thresholds, monitor events |
| **Unit: DelegationTracker** | 18 | Cycles (2-node, 3-node, self), notify/reject/cancel actions, deregister cleanup |
| **Unit: DemandEstimator** | 22 | Statistics (mean/variance/stddev), cold start, confidence levels, rolling window, modes |
| **Unit: Probabilistic Safety** | 10 | Probabilistic wrappers, confidence recording, hypothetical checks, multi-resource |
| **Integration: Deadlock Prevention** | 4 | Dining philosophers, circular wait, incremental requests |
| **Integration: Concurrent** | 5 | 10-agent stress test, registration races, batch concurrency, async, high contention |
| **Integration: Delegation Cycles** | 6 | Cycle detection through ResourceManager, reject/cancel config, disabled no-ops |
| **Integration: Adaptive Demands** | 8 | Adaptive/hybrid/static modes, probabilistic safety, backward compatibility |
| **Integration: Progress Monitor** | 6 | Stall detection, auto-release, monitor events, multi-agent stall states |

```bash
cd build && ctest --output-on-failure
# 189/189 tests pass in ~2.7 seconds
```

### Deadlock prevention proof tests

The integration tests construct scenarios that **would** deadlock without the Banker's Algorithm and verify that AgentGuard prevents them:

- **Dining Philosophers**: 5 agents, 5 resources (capacity 1 each), each agent needs 2 adjacent resources. All 5 complete.
- **Circular Wait**: 3 agents forming a circular resource dependency chain. All 3 complete.
- **Incremental Requests**: 3 agents incrementally requesting from a shared pool. All complete via serialization.

## Project Structure

```
agentguard/
|-- CMakeLists.txt                      # Root build configuration
|-- cmake/
|   |-- CompilerWarnings.cmake          # -Wall -Wextra -Wpedantic etc.
|   |-- Sanitizers.cmake                # ASan / TSan / UBSan support
|-- include/agentguard/
|   |-- agentguard.hpp                  # Umbrella header (includes everything)
|   |-- types.hpp                       # AgentId, ResourceTypeId, enums, structs
|   |-- exceptions.hpp                  # Exception hierarchy
|   |-- config.hpp                      # Config struct (+ ProgressConfig, DelegationConfig, AdaptiveConfig)
|   |-- resource.hpp                    # Resource class
|   |-- agent.hpp                       # Agent class
|   |-- safety_checker.hpp              # Core Banker's Algorithm + probabilistic extensions
|   |-- request_queue.hpp               # Priority queue for pending requests
|   |-- resource_manager.hpp            # Central coordinator
|   |-- monitor.hpp                     # Monitor interface + ConsoleMonitor + MetricsMonitor
|   |-- policy.hpp                      # Scheduling policies
|   |-- progress_tracker.hpp            # Stuck agent detection via progress invariants
|   |-- delegation_tracker.hpp          # Authority deadlock cycle detection
|   |-- demand_estimator.hpp            # Statistical max-need estimation
|   |-- ai/
|       |-- token_budget.hpp            # LLM token pool resource
|       |-- rate_limiter.hpp            # API rate limit resource
|       |-- tool_slot.hpp               # Tool access resource
|       |-- memory_pool.hpp             # Shared memory resource
|-- src/
|   |-- CMakeLists.txt                  # Library target
|   |-- resource.cpp, agent.cpp, safety_checker.cpp, resource_manager.cpp,
|   |-- request_queue.cpp, monitor.cpp, policy.cpp, config.cpp
|   |-- progress_tracker.cpp, delegation_tracker.cpp, demand_estimator.cpp
|   |-- ai/
|       |-- token_budget.cpp, rate_limiter.cpp, tool_slot.cpp, memory_pool.cpp
|-- tests/
|   |-- CMakeLists.txt                  # GoogleTest via FetchContent
|   |-- unit/                           # Per-class unit tests (10 files)
|   |-- integration/                    # Concurrent, deadlock, and feature integration tests (5 files)
|-- examples/
    |-- CMakeLists.txt
    |-- 01_basic_usage.cpp              # Minimal example
    |-- 02_llm_api_rate_limits.cpp      # Multi-threaded API rate sharing
    |-- 03_tool_sharing.cpp             # Deadlock-free tool sharing
    |-- 04_priority_agents.cpp          # Priority scheduling with metrics
    |-- 05_adaptive_agents.cpp          # All three novel features in action
```

## Requirements

- **C++ Standard**: C++17
- **Build System**: CMake 3.16+
- **Compiler**: Any C++17-capable compiler
  - GCC 7+
  - Clang 5+
  - AppleClang 10+ (Xcode 10+)
  - MSVC 19.14+ (Visual Studio 2017 15.7+)
- **Dependencies**: None (GoogleTest fetched automatically for tests)
- **Threading**: POSIX threads (pthreads) or Windows threads

## Integration

### CMake (after install)

```cmake
find_package(AgentGuard REQUIRED)
target_link_libraries(my_target PRIVATE AgentGuard::agentguard)
```

### CMake (as subdirectory)

```cmake
add_subdirectory(agentguard)
target_link_libraries(my_target PRIVATE AgentGuard::agentguard)
```

### Single include

```cpp
#include <agentguard/agentguard.hpp>   // includes everything
```

Or include only what you need:

```cpp
#include <agentguard/resource_manager.hpp>
#include <agentguard/ai/rate_limiter.hpp>
```

## License

MIT
