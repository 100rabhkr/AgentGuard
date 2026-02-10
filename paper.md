---
title: 'AgentGuard: Deadlock Prevention for Multi-AI-Agent Systems via Extended Banker''s Algorithm'
tags:
  - C++
  - Python
  - deadlock prevention
  - multi-agent systems
  - concurrency
  - Banker's Algorithm
  - large language models
  - resource management
authors:
  - name: Saurabh Kumar
    orcid: 0009-0007-6123-7238
    corresponding: true
    affiliation: 1
affiliations:
  - name: Independent Researcher, India
    index: 1
date: 10 February 2026
bibliography: paper.bib
---

# Summary

AgentGuard is a C++17 library with Python bindings that provides mathematically guaranteed deadlock prevention for multi-AI-agent systems. It extends Dijkstra's Banker's Algorithm [@Dijkstra:1965] with three subsystems designed for the unique challenges of LLM-based agents: a Progress Monitor for detecting stalled agents, a Delegation Tracker for preventing circular authority chains, and an Adaptive Demand Estimator that learns resource needs at runtime. The library integrates natively with LangGraph and is available on PyPI as `agentguard-ai`.

# Statement of Need

Multi-agent systems powered by large language models (LLMs) increasingly share limited resources such as API rate limits, tool instances, token budgets, and database connections [@Xi:2023; @Wang:2024]. When multiple agents compete for these shared resources, the classical conditions for deadlock arise [@Coffman:1971]: mutual exclusion (a tool serves one agent at a time), hold-and-wait (an agent holds API slots while requesting a browser), no preemption (resources cannot be forcibly reclaimed from an active LLM call), and circular wait (agents form a cycle of resource dependencies).

Current frameworks---LangGraph [@LangGraph:2024], AutoGen [@Wu:2023], CrewAI [@CrewAI:2024]---handle this with `max_iterations`, a counter that terminates agents after N steps. This is a timeout, not a prevention mechanism: it wastes tokens and compute while the deadlock persists undetected.

AgentGuard targets developers building multi-agent AI systems who need formal resource management guarantees. It addresses three gaps that arise when applying the classical Banker's Algorithm [@Silberschatz:2018] to LLM agents:

1. **Silent stalls**: An LLM agent may enter an infinite reasoning loop, holding resources indefinitely. The classical algorithm assumes agents eventually complete.
2. **Authority deadlocks**: Agent A delegates to B, B to C, C back to A---no resource is held, yet the system is frozen. This is invisible to resource-based analysis.
3. **Unknown maximum demands**: The Banker's Algorithm requires agents to declare maximum resource needs upfront. LLM agents cannot predict how many API calls a task will require.

# State of the Field

Deadlock prevention is well-studied in operating systems [@Silberschatz:2018; @Coffman:1971], but no existing library applies these techniques to AI agent orchestration. At the infrastructure level, systems like vLLM [@Kwon:2023] manage GPU memory for inference serving---a complementary concern at a different layer. Multi-agent frameworks [@LangGraph:2024; @Wu:2023; @CrewAI:2024] provide orchestration but rely on timeouts for deadlock handling. Classical multi-agent systems research [@Dorri:2018] addresses coordination protocols but does not target the specific resource contention patterns of LLM agents (API rate limits, tool slots, token budgets).

AgentGuard was built as a new library rather than contributing to an existing framework because it operates at a layer below any specific framework: it manages resource allocation independently and can wrap any agent system. The LangGraph integration demonstrates this pattern---AgentGuard provides the resource safety layer while LangGraph provides the workflow orchestration.

# Software Design

AgentGuard's architecture consists of a central `ResourceManager` that coordinates four subsystems:

- **SafetyChecker**: A pure, stateless function implementing the Banker's Algorithm in $O(n^2 m)$ time, where $n$ is the number of agents and $m$ is the number of resource types. Being stateless, it requires no internal locking and supports safe parallel invocation.
- **ProgressTracker**: Maintains per-agent progress records with configurable stall thresholds. A background thread periodically checks for stalled agents and can automatically reclaim their resources, transforming potential livelocks into recoverable situations.
- **DelegationTracker**: Maintains a directed graph of active agent delegations. Incremental BFS cycle detection runs on each new delegation, and a configurable policy (reject, notify, or cancel) determines the response when cycles are found [@Cormen:2022].
- **DemandEstimator**: Tracks rolling statistics (mean, variance) of resource usage per agent and estimates maximum needs using the inverse normal CDF via the Beasley-Springer-Moro approximation [@Beasley:1977]. This enables a probabilistic safety check for agents that cannot declare resource needs upfront.

The C++ core uses `std::shared_mutex` for concurrent read access with exclusive writes [@Williams:2019]. Python bindings via pybind11 [@pybind11:2017] release the GIL during blocking calls and re-acquire it for callbacks. A high-level Python layer provides a `@guarded_tool` decorator and a `GuardedToolNode` for drop-in LangGraph integration.

Key design trade-offs:

- **Centralized coordinator**: All requests go through one `ResourceManager`, creating a throughput ceiling at high concurrency but simplifying safety guarantees.
- **Quadratic safety check**: The $O(n^2 m)$ algorithm adds overhead per request, but benchmarks show this is under 200 $\mu$s for typical deployments ($n \leq 20$, $m \leq 10$)---negligible relative to LLM inference latency.
- **Statistical vs. declared demands**: The Adaptive mode trades mathematical certainty for flexibility. Hybrid mode (capping estimates with declared bounds) preserves the classical safety guarantee.

# Research Impact Statement

AgentGuard addresses an emerging problem in AI systems engineering. As multi-agent LLM systems move from prototypes to production---with agents sharing API rate limits, tool instances, and compute budgets---formal resource management becomes critical. The library provides:

- A reference implementation of deadlock prevention for AI agents, validated by 290 tests (189 C++, 101 Python) including classical deadlock scenarios (Dining Philosophers, circular wait)
- Benchmarks demonstrating practical overhead: 14 $\mu$s safety check latency at 5 agents, 17,850 ops/s single-threaded throughput, 163 bytes/agent memory overhead
- Native integration with LangGraph, the most widely-used stateful agent framework
- Open-source availability on PyPI (`pip install agentguard-ai`) for immediate adoption

The library is designed to enable reproducible research on resource management in multi-agent AI systems.

# AI Usage Disclosure

Generative AI tools (Claude) were used to assist with code generation, documentation writing, and test development during the creation of this software. All AI-generated code was reviewed, tested, and validated by the human author. The core algorithmic design decisions, architecture, and system design were made by the human author.

# Acknowledgements

AgentGuard builds on Dijkstra's Banker's Algorithm (1965) and the Beasley-Springer-Moro inverse normal CDF approximation. The library uses GoogleTest for C++ testing and pybind11 for Python bindings.

# References
