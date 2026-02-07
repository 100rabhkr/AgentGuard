"""Shared pytest fixtures for AgentGuard binding tests."""

import pytest
import agentguard as ag


@pytest.fixture
def config():
    """Default config with short timeouts for testing."""
    c = ag.Config()
    c.default_request_timeout = __import__("datetime").timedelta(seconds=2)
    c.processor_poll_interval = __import__("datetime").timedelta(milliseconds=10)
    return c


@pytest.fixture
def manager(config):
    """ResourceManager that auto-stops after each test."""
    m = ag.ResourceManager(config)
    yield m
    try:
        m.stop()
    except Exception:
        pass


@pytest.fixture
def started_manager(manager):
    """ResourceManager that is started and auto-stops."""
    manager.start()
    return manager


@pytest.fixture
def resource():
    """A simple test resource."""
    return ag.Resource(1, "test_api", ag.ResourceCategory.ApiRateLimit, 10)


@pytest.fixture
def agent():
    """A simple test agent."""
    a = ag.Agent(0, "test_agent")
    a.declare_max_need(1, 5)
    return a
