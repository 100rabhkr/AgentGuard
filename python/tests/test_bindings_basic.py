"""Tests for enums, structs, constants, and exceptions in AgentGuard bindings."""

import pytest
import agentguard as ag


# ---------------------------------------------------------------------------
# Enum accessibility
# ---------------------------------------------------------------------------

class TestRequestStatusEnum:
    def test_all_values_accessible(self):
        assert ag.RequestStatus.Pending is not None
        assert ag.RequestStatus.Granted is not None
        assert ag.RequestStatus.Denied is not None
        assert ag.RequestStatus.TimedOut is not None
        assert ag.RequestStatus.Cancelled is not None


class TestAgentStateEnum:
    def test_all_values_accessible(self):
        assert ag.AgentState.Registered is not None
        assert ag.AgentState.Active is not None
        assert ag.AgentState.Waiting is not None
        assert ag.AgentState.Releasing is not None
        assert ag.AgentState.Deregistered is not None


class TestResourceCategoryEnum:
    def test_has_all_nine_values(self):
        categories = [
            ag.ResourceCategory.ApiRateLimit,
            ag.ResourceCategory.TokenBudget,
            ag.ResourceCategory.ToolSlot,
            ag.ResourceCategory.MemoryPool,
            ag.ResourceCategory.DatabaseConn,
            ag.ResourceCategory.GpuCompute,
            ag.ResourceCategory.FileHandle,
            ag.ResourceCategory.NetworkSocket,
            ag.ResourceCategory.Custom,
        ]
        assert len(categories) == 9
        # Ensure all are distinct
        assert len(set(categories)) == 9


class TestDemandModeEnum:
    def test_has_three_values(self):
        modes = [
            ag.DemandMode.Static,
            ag.DemandMode.Adaptive,
            ag.DemandMode.Hybrid,
        ]
        assert len(modes) == 3
        assert len(set(modes)) == 3


class TestEventTypeEnum:
    def test_has_all_24_values(self):
        event_types = [
            ag.EventType.AgentRegistered,
            ag.EventType.AgentDeregistered,
            ag.EventType.ResourceRegistered,
            ag.EventType.ResourceCapacityChanged,
            ag.EventType.RequestSubmitted,
            ag.EventType.RequestGranted,
            ag.EventType.RequestDenied,
            ag.EventType.RequestTimedOut,
            ag.EventType.RequestCancelled,
            ag.EventType.ResourcesReleased,
            ag.EventType.SafetyCheckPerformed,
            ag.EventType.UnsafeStateDetected,
            ag.EventType.QueueSizeChanged,
            ag.EventType.AgentProgressReported,
            ag.EventType.AgentStalled,
            ag.EventType.AgentStallResolved,
            ag.EventType.AgentResourcesAutoReleased,
            ag.EventType.DelegationReported,
            ag.EventType.DelegationCompleted,
            ag.EventType.DelegationCancelled,
            ag.EventType.DelegationCycleDetected,
            ag.EventType.DemandEstimateUpdated,
            ag.EventType.ProbabilisticSafetyCheck,
            ag.EventType.AdaptiveDemandModeChanged,
        ]
        assert len(event_types) == 24
        assert len(set(event_types)) == 24


# ---------------------------------------------------------------------------
# Config defaults
# ---------------------------------------------------------------------------

class TestConfigDefaults:
    def test_max_agents_positive(self):
        c = ag.Config()
        assert c.max_agents > 0

    def test_thread_safe_default_true(self):
        c = ag.Config()
        assert c.thread_safe is True


class TestProgressConfigDefaults:
    def test_default_construction(self):
        pc = ag.ProgressConfig()
        assert pc.enabled is not None  # accessible
        assert isinstance(pc.auto_release_on_stall, bool)


class TestDelegationConfigDefaults:
    def test_default_construction(self):
        dc = ag.DelegationConfig()
        assert dc.enabled is not None
        assert dc.cycle_action is not None


class TestAdaptiveConfigDefaults:
    def test_default_construction(self):
        ac = ag.AdaptiveConfig()
        assert ac.enabled is not None
        assert ac.default_confidence_level > 0.0
        assert ac.history_window_size > 0


# ---------------------------------------------------------------------------
# Data structs construction
# ---------------------------------------------------------------------------

class TestSafetyCheckInput:
    def test_construction(self):
        inp = ag.SafetyCheckInput()
        inp.total = {1: 10}
        inp.available = {1: 5}
        inp.allocation = {0: {1: 3}}
        inp.max_need = {0: {1: 8}}
        assert inp.total[1] == 10
        assert inp.available[1] == 5


class TestSafetyCheckResult:
    def test_construction(self):
        r = ag.SafetyCheckResult()
        # Fields should be accessible
        assert r.is_safe is not None or r.is_safe is None  # just access it
        assert hasattr(r, "safe_sequence")
        assert hasattr(r, "reason")


class TestSystemSnapshot:
    def test_construction(self):
        snap = ag.SystemSnapshot()
        assert hasattr(snap, "timestamp")
        assert hasattr(snap, "total_resources")
        assert hasattr(snap, "available_resources")
        assert hasattr(snap, "agents")
        assert hasattr(snap, "pending_requests")
        assert hasattr(snap, "is_safe")


class TestDelegationInfo:
    def test_fields(self):
        di = ag.DelegationInfo()
        di.from_agent = 0
        di.to_agent = 1
        di.task_description = "summarize document"
        assert di.from_agent == 0
        assert di.to_agent == 1
        assert di.task_description == "summarize document"


class TestDelegationResult:
    def test_fields(self):
        dr = ag.DelegationResult()
        assert hasattr(dr, "accepted")
        assert hasattr(dr, "cycle_detected")
        assert hasattr(dr, "cycle_path")


class TestProbabilisticSafetyResult:
    def test_fields(self):
        psr = ag.ProbabilisticSafetyResult()
        assert hasattr(psr, "is_safe")
        assert hasattr(psr, "confidence_level")
        assert hasattr(psr, "max_safe_confidence")
        assert hasattr(psr, "safe_sequence")
        assert hasattr(psr, "reason")
        assert hasattr(psr, "estimated_max_needs")


# ---------------------------------------------------------------------------
# Priority constants
# ---------------------------------------------------------------------------

class TestPriorityConstants:
    def test_priority_low(self):
        assert ag.PRIORITY_LOW == 0

    def test_priority_normal(self):
        assert ag.PRIORITY_NORMAL == 50

    def test_priority_high(self):
        assert ag.PRIORITY_HIGH == 100

    def test_priority_critical(self):
        assert ag.PRIORITY_CRITICAL == 200


# ---------------------------------------------------------------------------
# Exception hierarchy
# ---------------------------------------------------------------------------

class TestExceptionHierarchy:
    def test_agentguard_error_inherits_runtime_error(self):
        assert issubclass(ag.AgentGuardError, RuntimeError)

    def test_agent_not_found_inherits_agentguard_error(self):
        assert issubclass(ag.AgentNotFoundError, ag.AgentGuardError)

    def test_resource_not_found_inherits_agentguard_error(self):
        assert issubclass(ag.ResourceNotFoundError, ag.AgentGuardError)

    def test_invalid_request_inherits_agentguard_error(self):
        assert issubclass(ag.InvalidRequestError, ag.AgentGuardError)

    def test_max_claim_exceeded_inherits_invalid_request(self):
        assert issubclass(ag.MaxClaimExceededError, ag.InvalidRequestError)

    def test_resource_capacity_exceeded_inherits_invalid_request(self):
        assert issubclass(ag.ResourceCapacityExceededError, ag.InvalidRequestError)

    def test_queue_full_inherits_agentguard_error(self):
        assert issubclass(ag.QueueFullError, ag.AgentGuardError)

    def test_agent_already_registered_inherits_agentguard_error(self):
        assert issubclass(ag.AgentAlreadyRegisteredError, ag.AgentGuardError)


# ---------------------------------------------------------------------------
# UsageStats methods
# ---------------------------------------------------------------------------

class TestUsageStats:
    def test_mean_method(self):
        stats = ag.UsageStats()
        # Default-constructed, mean should return 0 or be callable
        result = stats.mean()
        assert isinstance(result, float)

    def test_variance_method(self):
        stats = ag.UsageStats()
        result = stats.variance()
        assert isinstance(result, float)

    def test_stddev_method(self):
        stats = ag.UsageStats()
        result = stats.stddev()
        assert isinstance(result, float)
        assert result >= 0.0
