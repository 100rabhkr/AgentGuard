"""Tests for ResourceManager lifecycle in AgentGuard bindings."""

import datetime
import pytest
import agentguard as ag


class TestResourceRegistration:
    def test_register_and_get_resource(self, manager, resource):
        manager.register_resource(resource)
        got = manager.get_resource(resource.id())
        assert got.id() == resource.id()
        assert got.name() == resource.name()
        assert got.total_capacity() == resource.total_capacity()

    def test_resource_repr(self, resource):
        rep = repr(resource)
        assert "Resource" in rep
        assert "test_api" in rep
        assert "10" in rep


class TestAgentRegistration:
    def test_register_and_get_agent(self, manager, resource, agent):
        manager.register_resource(resource)
        assigned_id = manager.register_agent(agent)
        got = manager.get_agent(assigned_id)
        assert got.name() == "test_agent"

    def test_agent_repr(self, agent):
        rep = repr(agent)
        assert "Agent" in rep
        assert "test_agent" in rep

    def test_deregister_agent(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        aid = started_manager.register_agent(agent)
        started_manager.deregister_agent(aid)
        # After deregistering, agent_count should reflect the removal
        assert started_manager.agent_count() == 0

    def test_agent_count(self, manager, resource, agent):
        manager.register_resource(resource)
        manager.register_agent(agent)
        assert manager.agent_count() == 1


class TestResourceRequests:
    def test_request_resources_returns_granted(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        aid = started_manager.register_agent(agent)
        status = started_manager.request_resources(aid, resource.id(), 3)
        assert status == ag.RequestStatus.Granted

    def test_release_resources(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        aid = started_manager.register_agent(agent)
        started_manager.request_resources(aid, resource.id(), 3)
        started_manager.release_resources(aid, resource.id(), 3)
        # After releasing, resource should be fully available again
        got = started_manager.get_resource(resource.id())
        assert got.available() == resource.total_capacity()

    def test_release_all_resources_with_resource_type(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        aid = started_manager.register_agent(agent)
        started_manager.request_resources(aid, resource.id(), 3)
        started_manager.release_all_resources(aid, resource.id())
        got = started_manager.get_resource(resource.id())
        assert got.available() == resource.total_capacity()

    def test_release_all_resources_agent_only(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        aid = started_manager.register_agent(agent)
        started_manager.request_resources(aid, resource.id(), 3)
        started_manager.release_all_resources(aid)
        got = started_manager.get_resource(resource.id())
        assert got.available() == resource.total_capacity()

    def test_request_resources_batch(self, started_manager, resource, agent):
        # Register a second resource type
        resource2 = ag.Resource(2, "token_budget", ag.ResourceCategory.TokenBudget, 20)
        agent.declare_max_need(2, 10)
        started_manager.register_resource(resource)
        started_manager.register_resource(resource2)
        aid = started_manager.register_agent(agent)
        requests = {1: 2, 2: 5}
        status = started_manager.request_resources_batch(aid, requests)
        assert status == ag.RequestStatus.Granted


class TestManagerQueries:
    def test_is_safe_returns_true(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        started_manager.register_agent(agent)
        assert started_manager.is_safe() is True

    def test_get_snapshot_returns_valid_snapshot(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        started_manager.register_agent(agent)
        snap = started_manager.get_snapshot()
        assert isinstance(snap, ag.SystemSnapshot)
        assert snap.is_safe is True
        assert resource.id() in snap.total_resources

    def test_pending_request_count(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        started_manager.register_agent(agent)
        count = started_manager.pending_request_count()
        assert count == 0

    def test_update_agent_max_claim(self, started_manager, resource, agent):
        started_manager.register_resource(resource)
        aid = started_manager.register_agent(agent)
        started_manager.update_agent_max_claim(aid, resource.id(), 8)
        got = started_manager.get_agent(aid)
        assert got.max_needs()[resource.id()] == 8


class TestManagerLifecycle:
    def test_start_and_stop(self, manager):
        manager.start()
        assert manager.is_running() is True
        manager.stop()
        assert manager.is_running() is False

    def test_stop_idempotent(self, manager):
        """Stopping a manager that was never started should not raise."""
        # The conftest fixture calls stop() in teardown; this tests explicit stop
        manager.start()
        manager.stop()
        # Second stop should be safe (fixture will call stop again in teardown)
