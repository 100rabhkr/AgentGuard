# I built a deadlock prevention library for multi-AI-agent systems (open source, Python + C++)

I kept running into the same problem with multi-agent setups: agents sharing API rate limits, tool slots, and token budgets would randomly freeze. No errors, no timeouts -- just stuck. Turns out this is a textbook deadlock, the same class of bug that operating systems solved 60 years ago with the Banker's Algorithm (Dijkstra, 1965).

So I built **AgentGuard** -- a deadlock prevention library for multi-AI-agent systems. C++ core for speed, Python bindings via pybind11, native LangGraph integration.

## What it does

Before granting any resource request, AgentGuard checks: *"If I give this agent these resources, can every agent still finish?"* If no, the request waits. No deadlock, mathematically guaranteed.

```python
from agentguard.langgraph import AgentGuard, guarded_tool

guard = AgentGuard()
guard.add_resource("openai_api", 60)       # 60 req/min shared
guard.add_resource("browser", 1)            # exclusive tool

agent = guard.register_agent("researcher", max_needs={"openai_api": 10, "browser": 1})

@guarded_tool(guard, agent, {"openai_api": 2, "browser": 1})
def research(query: str) -> str:
    # Resources acquired automatically, released on return/exception
    return call_openai(query) + browse(query)
```

## What makes it different from `max_iterations`

Every framework's answer to stuck agents is "kill after N steps." That's a timer, not a safety system. AgentGuard:

- **Prevents** deadlocks instead of recovering from them
- **Detects stuck agents** via progress monitoring (not just iteration counting)
- **Catches authority deadlocks** where agents delegate to each other in a cycle (A->B->C->A) -- no resource is held, but everyone is waiting
- **Works without upfront declarations** -- adaptive mode learns resource patterns at runtime using statistical estimation

## Numbers

- 285 tests (189 C++, 96 Python)
- Safety check runs in microseconds (C++ core)
- Works with LangGraph, LangChain, or any Python framework
- MIT licensed

## Links

- **GitHub**: [github.com/100rabhkr/AgentGuard](https://github.com/100rabhkr/AgentGuard)
- **Install**: `pip install agentguard-ai` (or `pip install "agentguard-ai[langgraph]"` for LangGraph support)

---

Feedback welcome. If you've hit deadlocks in multi-agent setups, I'd love to hear what your workarounds have been. And if you think the approach is wrong, roast my code -- the repo is open.
