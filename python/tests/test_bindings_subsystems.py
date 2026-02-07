"""Tests for SafetyChecker, DemandEstimator, progress, delegation, and adaptive subsystems."""

import datetime
import time
import pytest
import agentguard as ag


# ===========================================================================
# SafetyChecker
# ===========================================================================

class TestSafetyCheckerSafe:
    def test_check_safety_safe_state(self):
        checker = ag.SafetyChecker()
        inp = ag.SafetyCheckInput()
        inp.total = {1: 10}
        inp.available = {1: 5}
        inp.allocation = {0: {1: 3}, 1: {1: 2}}
        inp.max_need = {0: {1: 7}, 1: {1: 4}}
        result = checker.check_safety(inp)
        assert result.is_safe is True
        assert len(result.safe_sequence) == 2

    def test_check_safety_unsafe_state(self):
        checker = ag.SafetyChecker()
        inp = ag.SafetyCheckInput()
        inp.total = {1: 10}
        inp.available = {1: 1}
        inp.allocation = {0: {1: 5}, 1: {1: 4}}
        inp.max_need = {0: {1: 10}, 1: {1: 10}}
        result = checker.check_safety(inp)
        assert result.is_safe is False


class TestSafetyCheckerHypothetical:
    def test_check_hypothetical(self):
        checker = ag.SafetyChecker()
        inp = ag.SafetyCheckInput()
        inp.total = {1: 10}
        inp.available = {1: 5}
        inp.allocation = {0: {1: 3}, 1: {1: 2}}
        inp.max_need = {0: {1: 7}, 1: {1: 4}}
        # Check: what if agent 0 requests 2 more of resource 1?
        result = checker.check_hypothetical(inp, 0, 1, 2)
        assert result.is_safe is True


class TestSafetyCheckerProbabilistic:
    def test_check_safety_probabilistic(self):
        checker = ag.SafetyChecker()
        inp = ag.SafetyCheckInput()
        inp.total = {1: 10}
        inp.available = {1: 5}
        inp.allocation = {0: {1: 3}, 1: {1: 2}}
        inp.max_need = {0: {1: 7}, 1: {1: 4}}
        result = checker.check_safety_probabilistic(inp, 0.95)
        assert isinstance(result, ag.ProbabilisticSafetyResult)
        assert hasattr(result, "is_safe")
        assert hasattr(result, "confidence_level")


# ===========================================================================
# DemandEstimator
# ===========================================================================

class TestDemandEstimator:
    def test_record_request_and_estimate(self):
        estimator = ag.DemandEstimator()
        # Record several requests for agent 0, resource 1
        for _ in range(10):
            estimator.record_request(0, 1, 3)
        est = estimator.estimate_max_need(0, 1, 0.95)
        # With 10 identical requests of 3, the estimate should be at least 3
        assert est >= 3

    def test_set_agent_demand_mode(self):
        estimator = ag.DemandEstimator()
        estimator.set_agent_demand_mode(0, ag.DemandMode.Adaptive)
        mode = estimator.get_agent_demand_mode(0)
        assert mode == ag.DemandMode.Adaptive


# ===========================================================================
# Progress Monitoring (via ResourceManager)
# ===========================================================================

class TestProgressReporting:
    @pytest.fixture
    def progress_manager(self):
        c = ag.Config()
        c.default_request_timeout = datetime.timedelta(seconds=2)
        c.processor_poll_interval = datetime.timedelta(milliseconds=10)
        c.progress.enabled = True
        c.progress.default_stall_threshold = datetime.timedelta(seconds=1)
        c.progress.check_interval = datetime.timedelta(milliseconds=50)
        c.progress.auto_release_on_stall = False
        m = ag.ResourceManager(c)
        m.start()
        yield m
        try:
            m.stop()
        except Exception:
            pass

    def test_report_progress(self, progress_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        progress_manager.register_resource(res)
        a = ag.Agent(0, "prog_agent")
        a.declare_max_need(1, 5)
        aid = progress_manager.register_agent(a)
        progress_manager.request_resources(aid, 1, 2)
        # Report progress (should not raise)
        progress_manager.report_progress(aid, "tokens_processed", 42.0)
        progress_manager.release_resources(aid, 1, 2)

    def test_is_agent_stalled_false_after_progress(self, progress_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        progress_manager.register_resource(res)
        a = ag.Agent(0, "stall_agent")
        a.declare_max_need(1, 5)
        aid = progress_manager.register_agent(a)
        progress_manager.request_resources(aid, 1, 2)
        progress_manager.report_progress(aid, "step", 1.0)
        stalled = progress_manager.is_agent_stalled(aid)
        assert stalled is False
        progress_manager.release_resources(aid, 1, 2)


# ===========================================================================
# Delegation Tracking (via ResourceManager)
# ===========================================================================

class TestDelegation:
    @pytest.fixture
    def delegation_manager(self):
        c = ag.Config()
        c.default_request_timeout = datetime.timedelta(seconds=2)
        c.processor_poll_interval = datetime.timedelta(milliseconds=10)
        c.delegation.enabled = True
        c.delegation.cycle_action = ag.DelegationCycleAction.RejectDelegation
        m = ag.ResourceManager(c)
        m.start()
        yield m
        try:
            m.stop()
        except Exception:
            pass

    def test_report_and_complete_delegation(self, delegation_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        delegation_manager.register_resource(res)
        a0 = ag.Agent(0, "delegator")
        a0.declare_max_need(1, 5)
        a1 = ag.Agent(1, "delegatee")
        a1.declare_max_need(1, 5)
        aid0 = delegation_manager.register_agent(a0)
        aid1 = delegation_manager.register_agent(a1)

        result = delegation_manager.report_delegation(aid0, aid1, "summarize doc")
        assert isinstance(result, ag.DelegationResult)
        assert result.accepted is True

        delegation_manager.complete_delegation(aid0, aid1)
        delegations = delegation_manager.get_all_delegations()
        # After completion, the delegation should be removed
        assert len(delegations) == 0

    def test_find_delegation_cycle_no_cycle(self, delegation_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        delegation_manager.register_resource(res)
        a0 = ag.Agent(0, "d0")
        a0.declare_max_need(1, 5)
        a1 = ag.Agent(1, "d1")
        a1.declare_max_need(1, 5)
        aid0 = delegation_manager.register_agent(a0)
        aid1 = delegation_manager.register_agent(a1)

        delegation_manager.report_delegation(aid0, aid1, "task")
        cycle = delegation_manager.find_delegation_cycle()
        # find_delegation_cycle returns None or empty list when no cycle exists
        assert cycle is None or len(cycle) == 0


# ===========================================================================
# Adaptive Demands (via ResourceManager)
# ===========================================================================

class TestAdaptiveDemands:
    @pytest.fixture
    def adaptive_manager(self):
        c = ag.Config()
        c.default_request_timeout = datetime.timedelta(seconds=2)
        c.processor_poll_interval = datetime.timedelta(milliseconds=10)
        c.adaptive.enabled = True
        c.adaptive.default_confidence_level = 0.95
        c.adaptive.history_window_size = 100
        m = ag.ResourceManager(c)
        m.start()
        yield m
        try:
            m.stop()
        except Exception:
            pass

    def test_set_agent_demand_mode_via_manager(self, adaptive_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        adaptive_manager.register_resource(res)
        a = ag.Agent(0, "adapt_agent")
        a.declare_max_need(1, 5)
        aid = adaptive_manager.register_agent(a)
        # Should not raise
        adaptive_manager.set_agent_demand_mode(aid, ag.DemandMode.Adaptive)

    def test_request_resources_adaptive(self, adaptive_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        adaptive_manager.register_resource(res)
        a = ag.Agent(0, "adapt_req")
        a.declare_max_need(1, 5)
        aid = adaptive_manager.register_agent(a)
        adaptive_manager.set_agent_demand_mode(aid, ag.DemandMode.Adaptive)
        status = adaptive_manager.request_resources_adaptive(aid, 1, 2)
        assert status == ag.RequestStatus.Granted
        adaptive_manager.release_resources(aid, 1, 2)

    def test_check_safety_probabilistic_via_manager(self, adaptive_manager):
        res = ag.Resource(1, "api", ag.ResourceCategory.ApiRateLimit, 10)
        adaptive_manager.register_resource(res)
        a = ag.Agent(0, "prob_agent")
        a.declare_max_need(1, 5)
        aid = adaptive_manager.register_agent(a)
        result = adaptive_manager.check_safety_probabilistic(0.95)
        assert isinstance(result, ag.ProbabilisticSafetyResult)
