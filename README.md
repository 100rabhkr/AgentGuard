# AgentGuard

**Deadlock prevention for multi-AI-agent systems.**

AgentGuard is a C++17 library that applies the [Banker's Algorithm](https://en.wikipedia.org/wiki/Banker%27s_algorithm) to prevent deadlocks when multiple AI agents compete for shared resources such as API rate limits, tool access slots, token budgets, database connections, and GPU compute.

Before granting any resource request, AgentGuard checks whether doing so would leave the system in a *safe state* -- one where every agent can still finish. If granting would risk deadlock, the request is queued or denied. This guarantee holds even with agents joining and leaving at runtime, concurrent multi-threaded access, and multiple resource types requested atomically.

## Table of Contents

- [The Problem](#the-problem)
- [How It Works](#how-it-works)
- [Quick Start](#quick-start)
- [Building](#building)
- [API Reference](#api-reference)
  - [ResourceManager](#resourcemanager)
  - [Agent](#agent)
  - [Resource](#resource)
  - [SafetyChecker](#safetychecker)
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

1. **Each agent declares its maximum resource needs** when it registers.
2. **Before granting any request**, the `SafetyChecker` simulates: "If I grant this, can all agents still complete?" It does this by iteratively finding agents whose remaining needs can be met with available resources, simulating their completion, and reclaiming their resources.
3. **If the resulting state is safe** (a valid completion sequence exists), the request is granted immediately.
4. **If the resulting state is unsafe**, the request is queued and re-evaluated whenever resources are released.
5. **Agents join and leave dynamically** -- unlike classical Banker's, which assumes a fixed process set.

The safety check runs in O(n^2 * m) time where n = number of agents and m = number of resource types. For typical multi-agent systems (n < 100, m < 50), this completes in microseconds.

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

All 109 tests should pass.

### Run examples

```bash
cd build
./examples/example_basic
./examples/example_llm_rate_limits
./examples/example_tool_sharing
./examples/example_priority_agents
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
AgentRegistered, AgentDeregistered, ResourceRegistered, ResourceCapacityChanged,
RequestSubmitted, RequestGranted, RequestDenied, RequestTimedOut, RequestCancelled,
ResourcesReleased, SafetyCheckPerformed, UnsafeStateDetected, QueueSizeChanged
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

109 tests across unit, integration, and concurrent categories:

| Category | Tests | Coverage |
|---|---|---|
| **Unit: Resource** | 12 | Construction, capacity, metadata |
| **Unit: Agent** | 17 | Construction, max needs, allocation, metadata |
| **Unit: SafetyChecker** | 21 | Safe/unsafe states, hypothetical checks, batch, bottlenecks, edge cases |
| **Unit: ResourceManager** | 23 | Registration, requests, releases, batch, snapshots, exceptions |
| **Unit: RequestQueue** | 17 | Priority ordering, cancellation, timeouts, capacity |
| **Unit: Policy** | 10 | FIFO, Priority, Fairness, Deadline, ShortestNeed |
| **Integration: Deadlock Prevention** | 4 | Dining philosophers, circular wait, incremental requests |
| **Integration: Concurrent** | 5 | 10-agent stress test, registration races, batch concurrency, async, high contention |

```bash
cd build && ctest --output-on-failure
# 109/109 tests pass in ~0.5 seconds
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
|   |-- config.hpp                      # Config struct
|   |-- resource.hpp                    # Resource class
|   |-- agent.hpp                       # Agent class
|   |-- safety_checker.hpp              # Core Banker's Algorithm
|   |-- request_queue.hpp               # Priority queue for pending requests
|   |-- resource_manager.hpp            # Central coordinator
|   |-- monitor.hpp                     # Monitor interface + ConsoleMonitor + MetricsMonitor
|   |-- policy.hpp                      # Scheduling policies
|   |-- ai/
|       |-- token_budget.hpp            # LLM token pool resource
|       |-- rate_limiter.hpp            # API rate limit resource
|       |-- tool_slot.hpp               # Tool access resource
|       |-- memory_pool.hpp             # Shared memory resource
|-- src/
|   |-- CMakeLists.txt                  # Library target
|   |-- resource.cpp, agent.cpp, safety_checker.cpp, resource_manager.cpp,
|   |-- request_queue.cpp, monitor.cpp, policy.cpp, config.cpp
|   |-- ai/
|       |-- token_budget.cpp, rate_limiter.cpp, tool_slot.cpp, memory_pool.cpp
|-- tests/
|   |-- CMakeLists.txt                  # GoogleTest via FetchContent
|   |-- unit/                           # Per-class unit tests (6 files)
|   |-- integration/                    # Concurrent and deadlock prevention tests (2 files)
|-- examples/
    |-- CMakeLists.txt
    |-- 01_basic_usage.cpp              # Minimal example
    |-- 02_llm_api_rate_limits.cpp      # Multi-threaded API rate sharing
    |-- 03_tool_sharing.cpp             # Deadlock-free tool sharing
    |-- 04_priority_agents.cpp          # Priority scheduling with metrics
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
