// 02_llm_api_rate_limits.cpp
//
// Three LLM agents share two API rate limits: OpenAI (60 req/min) and
// Anthropic (40 req/min).  Each agent runs in its own thread and makes
// concurrent requests.  A PriorityPolicy ensures higher-priority agents
// get rate-limit slots first.
//
// This example shows how AgentGuard prevents agents from collectively
// exceeding API rate limits, avoiding HTTP 429 errors in production.

#include <agentguard/agentguard.hpp>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

using namespace agentguard;
using namespace agentguard::ai;
using namespace std::chrono_literals;

// Simulates an agent making a series of API calls.
void agent_workload(ResourceManager& manager,
                    AgentId agent_id,
                    const std::string& agent_name,
                    ResourceTypeId openai_id,
                    ResourceTypeId anthropic_id,
                    int num_iterations) {
    for (int i = 0; i < num_iterations; ++i) {
        // Each iteration: request 5 OpenAI slots and 3 Anthropic slots,
        // simulating a chain-of-thought workflow that calls both APIs.

        std::cout << "[" << agent_name << "] iteration " << (i + 1)
                  << ": requesting 5 OpenAI slots...\n";
        auto s1 = manager.request_resources(agent_id, openai_id, 5, 10s);
        std::cout << "[" << agent_name << "] OpenAI request: " << to_string(s1) << "\n";

        if (s1 != RequestStatus::Granted) {
            std::cout << "[" << agent_name << "] could not get OpenAI slots, skipping.\n";
            continue;
        }

        std::cout << "[" << agent_name << "] requesting 3 Anthropic slots...\n";
        auto s2 = manager.request_resources(agent_id, anthropic_id, 3, 10s);
        std::cout << "[" << agent_name << "] Anthropic request: " << to_string(s2) << "\n";

        if (s2 != RequestStatus::Granted) {
            // Release what we got so far -- avoid holding resources we cannot use.
            manager.release_resources(agent_id, openai_id, 5);
            std::cout << "[" << agent_name << "] released OpenAI slots (Anthropic denied).\n";
            continue;
        }

        // Simulate API call latency.
        std::this_thread::sleep_for(50ms);

        // Release both after use.
        manager.release_resources(agent_id, openai_id, 5);
        manager.release_resources(agent_id, anthropic_id, 3);
        std::cout << "[" << agent_name << "] released all slots after iteration "
                  << (i + 1) << ".\n";
    }
}

int main() {
    std::cout << "=== AgentGuard: LLM API Rate Limits Example ===\n\n";

    // ----------------------------------------------------------------
    // 1. Configuration
    // ----------------------------------------------------------------
    Config config;
    config.default_request_timeout = 15s;
    config.processor_poll_interval = 5ms;
    ResourceManager manager(config);

    // Use a CompositeMonitor: console output + metrics collection.
    auto console = std::make_shared<ConsoleMonitor>(ConsoleMonitor::Verbosity::Normal);
    auto metrics = std::make_shared<MetricsMonitor>();
    auto composite = std::make_shared<CompositeMonitor>();
    composite->add_monitor(console);
    composite->add_monitor(metrics);
    manager.set_monitor(composite);

    // Use priority-based scheduling so high-priority agents go first.
    manager.set_scheduling_policy(std::make_unique<PriorityPolicy>());

    // ----------------------------------------------------------------
    // 2. Register API rate-limit resources using the RateLimiter helper.
    // ----------------------------------------------------------------
    constexpr ResourceTypeId OPENAI_ID   = 100;
    constexpr ResourceTypeId ANTHROPIC_ID = 200;

    // OpenAI: 60 requests per minute.
    RateLimiter openai_limiter(OPENAI_ID, "OpenAI-GPT4", 60,
                               RateLimiter::WindowType::PerMinute);
    openai_limiter.set_burst_allowance(10);
    openai_limiter.add_endpoint_sublimit("/v1/chat/completions", 50);
    openai_limiter.add_endpoint_sublimit("/v1/embeddings", 10);
    manager.register_resource(openai_limiter.as_resource());

    // Anthropic: 40 requests per minute.
    RateLimiter anthropic_limiter(ANTHROPIC_ID, "Anthropic-Claude", 40,
                                  RateLimiter::WindowType::PerMinute);
    anthropic_limiter.set_burst_allowance(5);
    manager.register_resource(anthropic_limiter.as_resource());

    std::cout << "Registered OpenAI (60 req/min) and Anthropic (40 req/min) rate limits.\n\n";

    // ----------------------------------------------------------------
    // 3. Register three agents with different priorities.
    // ----------------------------------------------------------------

    // Research agent -- critical priority, needs lots of API calls.
    Agent researcher(0, "Researcher", PRIORITY_CRITICAL);
    researcher.set_model_identifier("gpt-4");
    researcher.set_task_description("Deep research requiring many API calls");
    researcher.declare_max_need(OPENAI_ID, 30);
    researcher.declare_max_need(ANTHROPIC_ID, 20);
    AgentId researcher_id = manager.register_agent(researcher);

    // Summarizer -- normal priority.
    Agent summarizer(0, "Summarizer", PRIORITY_NORMAL);
    summarizer.set_model_identifier("claude-3");
    summarizer.set_task_description("Summarize research results");
    summarizer.declare_max_need(OPENAI_ID, 15);
    summarizer.declare_max_need(ANTHROPIC_ID, 10);
    AgentId summarizer_id = manager.register_agent(summarizer);

    // Background indexer -- low priority.
    Agent indexer(0, "Indexer", PRIORITY_LOW);
    indexer.set_model_identifier("gpt-4-mini");
    indexer.set_task_description("Background indexing of documents");
    indexer.declare_max_need(OPENAI_ID, 15);
    indexer.declare_max_need(ANTHROPIC_ID, 10);
    AgentId indexer_id = manager.register_agent(indexer);

    std::cout << "Registered 3 agents: Researcher (CRITICAL), "
              << "Summarizer (NORMAL), Indexer (LOW).\n\n";

    // ----------------------------------------------------------------
    // 4. Start the manager and spawn agent threads.
    // ----------------------------------------------------------------
    manager.start();

    std::vector<std::thread> threads;

    threads.emplace_back(agent_workload, std::ref(manager),
                         researcher_id, "Researcher",
                         OPENAI_ID, ANTHROPIC_ID, 4);

    threads.emplace_back(agent_workload, std::ref(manager),
                         summarizer_id, "Summarizer",
                         OPENAI_ID, ANTHROPIC_ID, 3);

    threads.emplace_back(agent_workload, std::ref(manager),
                         indexer_id, "Indexer",
                         OPENAI_ID, ANTHROPIC_ID, 3);

    // Wait for all agents to finish.
    for (auto& t : threads) {
        t.join();
    }

    // ----------------------------------------------------------------
    // 5. Print collected metrics.
    // ----------------------------------------------------------------
    auto m = metrics->get_metrics();
    std::cout << "\n=== Metrics Summary ===\n";
    std::cout << "Total requests:         " << m.total_requests << "\n";
    std::cout << "Granted requests:       " << m.granted_requests << "\n";
    std::cout << "Denied requests:        " << m.denied_requests << "\n";
    std::cout << "Timed-out requests:     " << m.timed_out_requests << "\n";
    std::cout << "Avg wait time (ms):     " << m.average_wait_time_ms << "\n";
    std::cout << "Unsafe state detections:" << m.unsafe_state_detections << "\n";

    // ----------------------------------------------------------------
    // 6. Clean up.
    // ----------------------------------------------------------------
    manager.release_all_resources(researcher_id);
    manager.release_all_resources(summarizer_id);
    manager.release_all_resources(indexer_id);
    manager.stop();

    std::cout << "\n=== Done ===\n";
    return 0;
}
