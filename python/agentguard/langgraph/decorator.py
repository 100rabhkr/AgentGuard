"""@guarded_tool decorator for automatic resource management."""

from __future__ import annotations

import functools
from typing import Callable, Dict, Optional, Union

from agentguard.langgraph.guard import AgentGuard


def guarded_tool(
    guard: AgentGuard,
    agent_id: int,
    resources: Union[str, Dict[str, int]],
    timeout: Optional[float] = None,
    adaptive: bool = False,
):
    """Decorator that wraps a function with automatic resource acquire/release.

    Usage::

        @guarded_tool(guard, agent_id, "openai_api")
        def call_openai(prompt: str) -> str:
            return openai.chat(prompt)

        @guarded_tool(guard, agent_id, {"openai_api": 2, "browser": 1}, timeout=10.0)
        def research(query: str) -> str:
            ...
    """
    if isinstance(resources, str):
        resources = {resources: 1}

    def decorator(func: Callable) -> Callable:
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            if len(resources) == 1:
                name, qty = next(iter(resources.items()))
                with guard.acquire(
                    agent_id, name, qty, timeout=timeout, adaptive=adaptive
                ):
                    return func(*args, **kwargs)
            else:
                with guard.acquire_batch(agent_id, resources, timeout=timeout):
                    return func(*args, **kwargs)

        return wrapper

    return decorator
