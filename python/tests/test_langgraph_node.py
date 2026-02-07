"""Tests for GuardedToolNode (requires langgraph)."""

import pytest

langgraph_available = False
try:
    import langgraph
    from langchain_core.tools import tool as lc_tool
    langgraph_available = True
except ImportError:
    pass

pytestmark = pytest.mark.skipif(
    not langgraph_available,
    reason="langgraph and langchain-core not installed",
)


@pytest.mark.skipif(not langgraph_available, reason="langgraph not installed")
class TestGuardedToolNode:
    def test_import_when_available(self):
        """GuardedToolNode can be imported when langgraph is available."""
        from agentguard.langgraph import GuardedToolNode
        assert GuardedToolNode is not None

    def test_construction(self):
        """GuardedToolNode can be constructed."""
        from agentguard.langgraph import AgentGuard, GuardedToolNode
        import agentguard as ag

        @lc_tool
        def search(query: str) -> str:
            """Search tool."""
            return f"results for {query}"

        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            node = GuardedToolNode(
                tools=[search],
                guard=g,
                agent_id=aid,
                tool_resources={"search": {"api": 1}},
            )
            assert node is not None

    def test_construction_with_timeout(self):
        """GuardedToolNode accepts timeout and adaptive parameters."""
        from agentguard.langgraph import AgentGuard, GuardedToolNode
        import agentguard as ag

        @lc_tool
        def search(query: str) -> str:
            """Search tool."""
            return f"results for {query}"

        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            node = GuardedToolNode(
                tools=[search],
                guard=g,
                agent_id=aid,
                tool_resources={"search": {"api": 1}},
                timeout=10.0,
                adaptive=True,
            )
            assert node is not None
            assert node._timeout == 10.0
            assert node._adaptive is True

    def test_tool_resources_default_empty(self):
        """GuardedToolNode defaults tool_resources to empty dict."""
        from agentguard.langgraph import AgentGuard, GuardedToolNode
        import agentguard as ag

        @lc_tool
        def search(query: str) -> str:
            """Search tool."""
            return f"results for {query}"

        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            node = GuardedToolNode(
                tools=[search],
                guard=g,
                agent_id=aid,
            )
            assert node._tool_resources == {}


class TestGuardedToolNodeFallback:
    def test_import_error_without_langgraph(self):
        """Without langgraph, importing GuardedToolNode raises ImportError."""
        if langgraph_available:
            pytest.skip("langgraph is installed")
        from agentguard.langgraph.node import GuardedToolNode
        with pytest.raises(ImportError, match="langgraph"):
            GuardedToolNode()
