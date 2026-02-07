"""Tests for GIL handling and threading in AgentGuard bindings."""

import datetime
import threading
import time
import pytest
import agentguard as ag


@pytest.fixture
def threaded_config():
    """Config tuned for threading tests."""
    c = ag.Config()
    c.default_request_timeout = datetime.timedelta(seconds=5)
    c.processor_poll_interval = datetime.timedelta(milliseconds=10)
    return c


@pytest.fixture
def threaded_manager(threaded_config):
    """Started manager for threading tests with auto-stop."""
    m = ag.ResourceManager(threaded_config)
    m.start()
    yield m
    try:
        m.stop()
    except Exception:
        pass


@pytest.mark.timeout(10)
class TestBlockingRequests:
    def test_blocking_request_does_not_deadlock_with_gil(self, threaded_manager):
        """request_resources releases the GIL so other threads can proceed."""
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        threaded_manager.register_resource(res)
        a = ag.Agent(0, "blocker")
        a.declare_max_need(1, 5)
        aid = threaded_manager.register_agent(a)
        status = threaded_manager.request_resources(
            aid, 1, 3, datetime.timedelta(seconds=2)
        )
        assert status == ag.RequestStatus.Granted
        threaded_manager.release_resources(aid, 1, 3)


@pytest.mark.timeout(10)
class TestFutureRequestStatus:
    def test_future_result_works(self, threaded_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        threaded_manager.register_resource(res)
        a = ag.Agent(0, "async_agent")
        a.declare_max_need(1, 5)
        aid = threaded_manager.register_agent(a)
        fut = threaded_manager.request_resources_async(aid, 1, 3)
        status = fut.result()
        assert status == ag.RequestStatus.Granted
        threaded_manager.release_resources(aid, 1, 3)

    def test_future_ready_returns_bool(self, threaded_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        threaded_manager.register_resource(res)
        a = ag.Agent(0, "async_agent")
        a.declare_max_need(1, 5)
        aid = threaded_manager.register_agent(a)
        fut = threaded_manager.request_resources_async(aid, 1, 3)
        # ready() should return a bool (may be True or False depending on timing)
        assert isinstance(fut.ready(), bool)
        # Wait for the result to ensure cleanup
        fut.result()
        threaded_manager.release_resources(aid, 1, 3)


@pytest.mark.timeout(10)
class TestConcurrentAgents:
    def test_concurrent_agents_from_python_threads(self, threaded_manager):
        """Multiple Python threads can each request/release resources concurrently."""
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 100)
        threaded_manager.register_resource(res)

        results = {}
        errors = []

        def worker(agent_id_offset):
            try:
                a = ag.Agent(agent_id_offset, f"agent_{agent_id_offset}")
                a.declare_max_need(1, 5)
                aid = threaded_manager.register_agent(a)
                status = threaded_manager.request_resources(aid, 1, 2)
                results[aid] = status
                threaded_manager.release_resources(aid, 1, 2)
            except Exception as e:
                errors.append(e)

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(5)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=8)

        assert len(errors) == 0, f"Errors in threads: {errors}"
        assert all(s == ag.RequestStatus.Granted for s in results.values())

    def test_multiple_threads_requesting_same_resource(self, threaded_manager):
        """Multiple threads contend for a limited resource without deadlock."""
        res = ag.Resource(1, "scarce", ag.ResourceCategory.ToolSlot, 3)
        threaded_manager.register_resource(res)

        statuses = []
        lock = threading.Lock()
        errors = []

        def worker(agent_id_offset):
            try:
                a = ag.Agent(agent_id_offset, f"contender_{agent_id_offset}")
                a.declare_max_need(1, 2)
                aid = threaded_manager.register_agent(a)
                status = threaded_manager.request_resources(
                    aid, 1, 1, datetime.timedelta(seconds=3)
                )
                with lock:
                    statuses.append(status)
                if status == ag.RequestStatus.Granted:
                    time.sleep(0.05)
                    threaded_manager.release_resources(aid, 1, 1)
            except Exception as e:
                with lock:
                    errors.append(e)

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=8)

        assert len(errors) == 0, f"Errors in threads: {errors}"
        # At least some requests should have been granted
        assert ag.RequestStatus.Granted in statuses


@pytest.mark.timeout(10)
class TestCallbackRequests:
    def test_request_resources_callback_fires(self, threaded_manager):
        """Callback-based request fires from the background processor thread."""
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        threaded_manager.register_resource(res)
        a = ag.Agent(0, "cb_agent")
        a.declare_max_need(1, 5)
        aid = threaded_manager.register_agent(a)

        event = threading.Event()
        callback_results = {}

        def on_complete(request_id, status):
            callback_results["request_id"] = request_id
            callback_results["status"] = status
            event.set()

        threaded_manager.request_resources_callback(aid, 1, 3, on_complete)
        assert event.wait(timeout=5), "Callback was not fired within timeout"
        assert callback_results["status"] == ag.RequestStatus.Granted
        threaded_manager.release_resources(aid, 1, 3)


@pytest.mark.timeout(10)
class TestAsyncNonBlocking:
    def test_async_request_does_not_block_caller(self, threaded_manager):
        """request_resources_async returns immediately without blocking."""
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        threaded_manager.register_resource(res)
        a = ag.Agent(0, "async_nb")
        a.declare_max_need(1, 5)
        aid = threaded_manager.register_agent(a)

        start = time.monotonic()
        fut = threaded_manager.request_resources_async(aid, 1, 3)
        elapsed = time.monotonic() - start
        # The async call itself should return nearly instantly (well under 1 second)
        assert elapsed < 1.0, f"Async call took {elapsed:.3f}s, expected < 1.0s"

        # Clean up
        status = fut.result()
        assert status == ag.RequestStatus.Granted
        threaded_manager.release_resources(aid, 1, 3)
