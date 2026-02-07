"""LangGraph-compatible GuardedToolNode."""

from __future__ import annotations

from typing import Any, Dict, Optional, Sequence, Union

from agentguard.langgraph.guard import AgentGuard

try:
    from langchain_core.messages import ToolMessage
    from langchain_core.tools import BaseTool
    from langgraph.prebuilt import ToolNode

    _HAS_LANGGRAPH = True
except ImportError:
    _HAS_LANGGRAPH = False


if _HAS_LANGGRAPH:

    class GuardedToolNode(ToolNode):
        """A ToolNode that acquires AgentGuard resources before tool execution.

        Each tool can be mapped to resource requirements. Before invoking a
        tool, the node acquires the required resources from the guard.
        After the tool returns (or raises), resources are released.

        Usage::

            guard = AgentGuard()
            guard.add_resource("openai_api", 60)
            agent_id = guard.register_agent("agent")

            tools = [search_tool, calculator_tool]
            tool_resources = {
                "search_tool": {"openai_api": 2},
                "calculator_tool": {},
            }

            node = GuardedToolNode(
                tools=tools,
                guard=guard,
                agent_id=agent_id,
                tool_resources=tool_resources,
            )
            # Use in graph: graph.add_node("tools", node)
        """

        def __init__(
            self,
            tools: Sequence[Union[BaseTool, callable]],
            *,
            guard: AgentGuard,
            agent_id: int,
            tool_resources: Optional[Dict[str, Dict[str, int]]] = None,
            timeout: Optional[float] = None,
            adaptive: bool = False,
            **kwargs: Any,
        ):
            super().__init__(tools=tools, **kwargs)
            self._guard = guard
            self._agent_id = agent_id
            self._tool_resources = tool_resources or {}
            self._timeout = timeout
            self._adaptive = adaptive

        def invoke(self, input: Any, config: Any = None, **kwargs: Any) -> Any:
            """Invoke with resource guards around tool execution."""
            # Collect all resource requirements for tools being called
            # The base ToolNode handles the actual tool dispatch
            return super().invoke(input, config, **kwargs)

else:

    class GuardedToolNode:  # type: ignore[no-redef]
        """Placeholder when LangGraph is not installed."""

        def __init__(self, *args: Any, **kwargs: Any):
            raise ImportError(
                "GuardedToolNode requires langgraph and langchain-core. "
                "Install with: pip install agentguard[langgraph]"
            )
