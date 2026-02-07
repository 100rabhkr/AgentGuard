"""LangChain callback handler for automatic tool resource management."""

from __future__ import annotations

import datetime
import threading
from typing import Any, Dict, Optional

from agentguard.langgraph.guard import AgentGuard

try:
    from langchain_core.callbacks import BaseCallbackHandler

    _HAS_LANGCHAIN = True
except ImportError:
    _HAS_LANGCHAIN = False


if _HAS_LANGCHAIN:

    class AgentGuardCallbackHandler(BaseCallbackHandler):
        """Auto-instruments LangChain tool calls with resource acquisition/release.

        Usage::

            guard = AgentGuard()
            guard.add_resource("tools", 5)
            agent_id = guard.register_agent("agent")

            handler = AgentGuardCallbackHandler(
                guard=guard,
                agent_id=agent_id,
                tool_resources={"search": {"tools": 1}},
            )
            # Pass handler to LangChain's callback system
        """

        def __init__(
            self,
            guard: AgentGuard,
            agent_id: int,
            tool_resources: Optional[Dict[str, Dict[str, int]]] = None,
            timeout: Optional[float] = None,
            report_progress: bool = True,
        ):
            super().__init__()
            self._guard = guard
            self._agent_id = agent_id
            self._tool_resources = tool_resources or {}
            self._timeout = timeout
            self._report_progress = report_progress
            self._active: Dict[str, Dict[str, int]] = {}
            self._lock = threading.Lock()
            self._step_count = 0

        def on_tool_start(
            self,
            serialized: Dict[str, Any],
            input_str: str,
            *,
            run_id: Any = None,
            **kwargs: Any,
        ) -> None:
            tool_name = serialized.get("name", "")
            resources = self._tool_resources.get(tool_name, {})
            if not resources:
                return

            key = str(run_id) if run_id else tool_name
            dur = (
                datetime.timedelta(seconds=self._timeout)
                if self._timeout
                else None
            )

            for res_name, qty in resources.items():
                rid = self._guard._resolve_resource(res_name)
                status = self._guard.manager.request_resources(
                    self._agent_id, rid, qty, dur
                )
                if status.name != "Granted":
                    raise RuntimeError(
                        f"AgentGuard: Failed to acquire '{res_name}' "
                        f"for tool '{tool_name}': {status.name}"
                    )

            with self._lock:
                self._active[key] = resources

        def on_tool_end(
            self, output: str, *, run_id: Any = None, **kwargs: Any
        ) -> None:
            key = str(run_id) if run_id else ""
            with self._lock:
                resources = self._active.pop(key, {})

            for res_name, qty in resources.items():
                rid = self._guard._resolve_resource(res_name)
                self._guard.manager.release_resources(self._agent_id, rid, qty)

            if self._report_progress:
                self._step_count += 1
                self._guard.report_progress(
                    self._agent_id, "tool_calls", self._step_count
                )

        def on_tool_error(
            self,
            error: BaseException,
            *,
            run_id: Any = None,
            **kwargs: Any,
        ) -> None:
            self.on_tool_end("", run_id=run_id)

else:

    class AgentGuardCallbackHandler:  # type: ignore[no-redef]
        """Placeholder when langchain-core is not installed."""

        def __init__(self, *args: Any, **kwargs: Any):
            raise ImportError(
                "AgentGuardCallbackHandler requires langchain-core. "
                "Install with: pip install agentguard[langgraph]"
            )
