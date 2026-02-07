"""High-level AgentGuard wrapper with string-based resource names."""

from __future__ import annotations

import datetime
import threading
from contextlib import contextmanager
from typing import Dict, Optional

import agentguard as ag


class AgentGuard:
    """Pythonic deadlock-prevention guard for multi-agent systems.

    Resources and agents are identified by string names instead of integer IDs.
    Provides context managers for automatic resource acquisition/release.

    Usage::

        guard = AgentGuard()
        guard.add_resource("openai_api", capacity=60, category="api_rate_limit")
        agent_id = guard.register_agent("researcher", max_needs={"openai_api": 10})

        with guard.acquire(agent_id, "openai_api", 3, timeout=5.0):
            # ... use the resource, auto-released on exit ...
            pass
    """

    _CATEGORY_MAP = {
        "api_rate_limit": ag.ResourceCategory.ApiRateLimit,
        "token_budget": ag.ResourceCategory.TokenBudget,
        "tool_slot": ag.ResourceCategory.ToolSlot,
        "memory_pool": ag.ResourceCategory.MemoryPool,
        "database_conn": ag.ResourceCategory.DatabaseConn,
        "gpu_compute": ag.ResourceCategory.GpuCompute,
        "file_handle": ag.ResourceCategory.FileHandle,
        "network_socket": ag.ResourceCategory.NetworkSocket,
        "custom": ag.ResourceCategory.Custom,
    }

    def __init__(
        self,
        config: Optional[ag.Config] = None,
        auto_start: bool = True,
    ):
        self._config = config or ag.Config()
        self._manager = ag.ResourceManager(self._config)
        self._resource_names: Dict[str, int] = {}
        self._next_resource_id = 1
        self._lock = threading.Lock()
        self._started = False
        if auto_start:
            self._manager.start()
            self._started = True

    def add_resource(
        self,
        name: str,
        capacity: int,
        category: str = "custom",
    ) -> int:
        """Register a named resource. Returns the resource type ID."""
        with self._lock:
            if name in self._resource_names:
                return self._resource_names[name]
            rid = self._next_resource_id
            self._next_resource_id += 1
            cat = self._CATEGORY_MAP.get(category, ag.ResourceCategory.Custom)
            self._manager.register_resource(ag.Resource(rid, name, cat, capacity))
            self._resource_names[name] = rid
            return rid

    def _resolve_resource(self, name_or_id) -> int:
        """Resolve a string resource name to its integer ID."""
        if isinstance(name_or_id, int):
            return name_or_id
        try:
            return self._resource_names[name_or_id]
        except KeyError:
            raise KeyError(f"Unknown resource: '{name_or_id}'") from None

    def register_agent(
        self,
        name: str,
        priority: int = ag.PRIORITY_NORMAL,
        max_needs: Optional[Dict[str, int]] = None,
        demand_mode: Optional[ag.DemandMode] = None,
    ) -> int:
        """Register a named agent. Returns the assigned AgentId."""
        agent = ag.Agent(0, name, priority)
        if max_needs:
            for res_name, qty in max_needs.items():
                rid = self._resolve_resource(res_name)
                agent.declare_max_need(rid, qty)
        aid = self._manager.register_agent(agent)
        if demand_mode is not None:
            self._manager.set_agent_demand_mode(aid, demand_mode)
        return aid

    def deregister_agent(self, agent_id: int) -> bool:
        """Remove an agent, releasing all its resources."""
        return self._manager.deregister_agent(agent_id)

    @contextmanager
    def acquire(
        self,
        agent_id: int,
        resource: str,
        quantity: int = 1,
        timeout: Optional[float] = None,
        adaptive: bool = False,
    ):
        """Context manager: acquire resource, yield, auto-release on exit.

        Raises AgentGuardError if the request is denied or times out.
        """
        rid = self._resolve_resource(resource)
        dur = datetime.timedelta(seconds=timeout) if timeout is not None else None

        if adaptive:
            status = self._manager.request_resources_adaptive(
                agent_id, rid, quantity, dur
            )
        else:
            status = self._manager.request_resources(agent_id, rid, quantity, dur)

        if status != ag.RequestStatus.Granted:
            raise ag.AgentGuardError(
                f"Resource request failed: {status.name} "
                f"(agent={agent_id}, resource='{resource}', qty={quantity})"
            )
        try:
            yield status
        finally:
            self._manager.release_resources(agent_id, rid, quantity)

    @contextmanager
    def acquire_batch(
        self,
        agent_id: int,
        resources: Dict[str, int],
        timeout: Optional[float] = None,
    ):
        """Context manager: atomically acquire multiple resources."""
        resolved = {
            self._resolve_resource(name): qty for name, qty in resources.items()
        }
        dur = datetime.timedelta(seconds=timeout) if timeout is not None else None

        status = self._manager.request_resources_batch(agent_id, resolved, dur)
        if status != ag.RequestStatus.Granted:
            raise ag.AgentGuardError(f"Batch request failed: {status.name}")
        try:
            yield status
        finally:
            for rid, qty in resolved.items():
                self._manager.release_resources(agent_id, rid, qty)

    # --- Delegation ---

    def delegate(
        self, from_agent: int, to_agent: int, task: str = ""
    ) -> ag.DelegationResult:
        """Report a delegation from one agent to another."""
        return self._manager.report_delegation(from_agent, to_agent, task)

    def complete_delegation(self, from_agent: int, to_agent: int):
        """Mark a delegation as completed."""
        self._manager.complete_delegation(from_agent, to_agent)

    def cancel_delegation(self, from_agent: int, to_agent: int):
        """Cancel an active delegation."""
        self._manager.cancel_delegation(from_agent, to_agent)

    # --- Progress ---

    def report_progress(self, agent_id: int, metric: str, value: float):
        """Report agent progress (resets stall timer)."""
        self._manager.report_progress(agent_id, metric, value)

    def is_stalled(self, agent_id: int) -> bool:
        """Check if an agent is stalled."""
        return self._manager.is_agent_stalled(agent_id)

    # --- Queries ---

    def is_safe(self) -> bool:
        """Check if the system is in a safe state."""
        return self._manager.is_safe()

    def snapshot(self) -> ag.SystemSnapshot:
        """Get a snapshot of the current system state."""
        return self._manager.get_snapshot()

    @property
    def manager(self) -> ag.ResourceManager:
        """Access the underlying ResourceManager for advanced usage."""
        return self._manager

    # --- Lifecycle ---

    def start(self):
        """Start the background processor (if not auto-started)."""
        if not self._started:
            self._manager.start()
            self._started = True

    def stop(self):
        """Stop the background processor."""
        if self._started:
            self._manager.stop()
            self._started = False

    def __del__(self):
        try:
            self.stop()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop()
        return False
