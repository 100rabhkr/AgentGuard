"""Tests for the AgentGuard high-level wrapper."""

import pytest
import agentguard as ag
from agentguard.langgraph import AgentGuard


class TestAgentGuard:
    def test_create_default(self):
        """AgentGuard can be created with defaults."""
        with AgentGuard() as g:
            assert g.is_safe()

    def test_add_resource(self):
        """add_resource returns the resource type ID."""
        with AgentGuard() as g:
            rid = g.add_resource("api", 10)
            assert rid == 1

    def test_add_resource_idempotent(self):
        """Adding same resource name twice returns same ID."""
        with AgentGuard() as g:
            r1 = g.add_resource("api", 10)
            r2 = g.add_resource("api", 10)
            assert r1 == r2

    def test_add_resource_with_category(self):
        """add_resource accepts category string."""
        with AgentGuard() as g:
            rid = g.add_resource("tokens", 1000, category="token_budget")
            assert rid >= 1

    def test_register_agent(self):
        """register_agent returns an agent ID."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            assert aid >= 1

    def test_register_agent_with_priority(self):
        """register_agent respects priority."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", priority=ag.PRIORITY_HIGH, max_needs={"api": 5})
            assert aid >= 1

    def test_acquire_context_manager(self):
        """acquire() works as context manager and auto-releases."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            with g.acquire(aid, "api", 3) as status:
                assert status == ag.RequestStatus.Granted
            # Resource should be released after exiting context

    def test_acquire_releases_on_exception(self):
        """acquire() releases resources even on exception."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            with pytest.raises(ValueError):
                with g.acquire(aid, "api", 3):
                    raise ValueError("test error")
            # Should still be able to acquire after exception
            with g.acquire(aid, "api", 3):
                pass

    def test_acquire_batch(self):
        """acquire_batch() acquires multiple resources atomically."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            g.add_resource("browser", 5)
            aid = g.register_agent("worker", max_needs={"api": 5, "browser": 2})
            with g.acquire_batch(aid, {"api": 3, "browser": 1}) as status:
                assert status == ag.RequestStatus.Granted

    def test_acquire_unknown_resource_raises(self):
        """acquire() with unknown resource name raises KeyError."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            with pytest.raises(KeyError, match="Unknown resource"):
                with g.acquire(aid, "nonexistent", 1):
                    pass

    def test_is_safe(self):
        """is_safe() returns True for safe system."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            assert g.is_safe()

    def test_snapshot(self):
        """snapshot() returns a SystemSnapshot."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            snap = g.snapshot()
            assert isinstance(snap, ag.SystemSnapshot)

    def test_deregister_agent(self):
        """deregister_agent removes the agent."""
        with AgentGuard() as g:
            g.add_resource("api", 10)
            aid = g.register_agent("worker", max_needs={"api": 5})
            result = g.deregister_agent(aid)
            assert result is True

    def test_manager_property(self):
        """manager property returns the underlying ResourceManager."""
        with AgentGuard() as g:
            assert isinstance(g.manager, ag.ResourceManager)
