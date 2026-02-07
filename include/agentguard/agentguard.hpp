#pragma once

// AgentGuard: Deadlock Prevention Library for Multi-AI-Agent Systems
//
// Applies the Banker's Algorithm to prevent deadlocks when multiple
// AI agents compete for shared resources (APIs, tools, memory, etc.)

// Core
#include "agentguard/types.hpp"
#include "agentguard/exceptions.hpp"
#include "agentguard/config.hpp"
#include "agentguard/resource.hpp"
#include "agentguard/agent.hpp"
#include "agentguard/safety_checker.hpp"
#include "agentguard/request_queue.hpp"
#include "agentguard/resource_manager.hpp"
#include "agentguard/monitor.hpp"
#include "agentguard/policy.hpp"

// Novel safety subsystems
#include "agentguard/progress_tracker.hpp"
#include "agentguard/delegation_tracker.hpp"
#include "agentguard/demand_estimator.hpp"

// AI-specific resource types
#include "agentguard/ai/token_budget.hpp"
#include "agentguard/ai/rate_limiter.hpp"
#include "agentguard/ai/tool_slot.hpp"
#include "agentguard/ai/memory_pool.hpp"
