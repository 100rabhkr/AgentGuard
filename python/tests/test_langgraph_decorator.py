"""Tests for the @guarded_tool decorator."""

import pytest
import agentguard as ag
from agentguard.langgraph import AgentGuard, guarded_tool


class TestGuardedTool:
    def test_single_resource(self):
        """Decorator with single resource name string."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})

            @guarded_tool(g, aid, "api")
            def call_api():
                return "result"

            assert call_api() == "result"

    def test_resource_dict(self):
        """Decorator with resource dict."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            g.add_resource("browser", 5)
            aid = g.register_agent("worker", max_needs={"api": 3, "browser": 2})

            @guarded_tool(g, aid, {"api": 2, "browser": 1})
            def research(query):
                return f"researched: {query}"

            assert research("test") == "researched: test"

    def test_preserves_function_name(self):
        """Decorator preserves function metadata via functools.wraps."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})

            @guarded_tool(g, aid, "api")
            def my_tool():
                """My docstring."""
                pass

            assert my_tool.__name__ == "my_tool"
            assert my_tool.__doc__ == "My docstring."

    def test_releases_on_exception(self):
        """Resources are released even if the function raises."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})

            @guarded_tool(g, aid, "api")
            def failing_tool():
                raise ValueError("boom")

            with pytest.raises(ValueError, match="boom"):
                failing_tool()

            # Should be able to acquire again
            @guarded_tool(g, aid, "api")
            def ok_tool():
                return "ok"

            assert ok_tool() == "ok"

    def test_with_timeout(self):
        """Decorator with timeout parameter."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})

            @guarded_tool(g, aid, "api", timeout=5.0)
            def timed_tool():
                return "done"

            assert timed_tool() == "done"

    def test_passes_args_and_kwargs(self):
        """Arguments and keyword arguments pass through correctly."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})

            @guarded_tool(g, aid, "api")
            def process(a, b, mode="fast"):
                return f"{a}+{b}:{mode}"

            assert process(1, 2, mode="slow") == "1+2:slow"
