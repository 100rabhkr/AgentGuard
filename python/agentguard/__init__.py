"""AgentGuard: Deadlock prevention for multi-AI-agent systems."""

from agentguard._agentguard import (
    # Enums
    RequestStatus,
    AgentState,
    ResourceCategory,
    DemandMode,
    DelegationCycleAction,
    EventType,
    Verbosity,

    # Config structs
    Config,
    ProgressConfig,
    DelegationConfig,
    AdaptiveConfig,

    # Data structs
    ResourceRequest,
    AgentAllocationSnapshot,
    SystemSnapshot,
    SafetyCheckInput,
    SafetyCheckResult,
    MonitorEvent,
    DelegationInfo,
    DelegationResult,
    ProbabilisticSafetyResult,
    UsageStats,
    ProgressRecord,
    Metrics,

    # Core classes
    Resource,
    Agent,
    ResourceManager,
    SafetyChecker,

    # Monitors
    Monitor,
    ConsoleMonitor,
    MetricsMonitor,
    CompositeMonitor,

    # Policies
    SchedulingPolicy,
    FifoPolicy,
    PriorityPolicy,
    ShortestNeedPolicy,
    DeadlinePolicy,
    FairnessPolicy,

    # Demand estimation
    DemandEstimator,

    # Future wrapper
    FutureRequestStatus,

    # Exceptions
    AgentGuardError,
    AgentNotFoundError,
    ResourceNotFoundError,
    InvalidRequestError,
    MaxClaimExceededError,
    ResourceCapacityExceededError,
    QueueFullError,
    AgentAlreadyRegisteredError,

    # Priority constants
    PRIORITY_LOW,
    PRIORITY_NORMAL,
    PRIORITY_HIGH,
    PRIORITY_CRITICAL,
)

# AI submodule
from agentguard._agentguard import ai

from agentguard._version import __version__

__all__ = [
    # Enums
    "RequestStatus", "AgentState", "ResourceCategory", "DemandMode",
    "DelegationCycleAction", "EventType", "Verbosity",
    # Config
    "Config", "ProgressConfig", "DelegationConfig", "AdaptiveConfig",
    # Data structs
    "ResourceRequest", "AgentAllocationSnapshot", "SystemSnapshot",
    "SafetyCheckInput", "SafetyCheckResult", "MonitorEvent",
    "DelegationInfo", "DelegationResult", "ProbabilisticSafetyResult",
    "UsageStats", "ProgressRecord", "Metrics",
    # Core
    "Resource", "Agent", "ResourceManager", "SafetyChecker",
    # Monitors
    "Monitor", "ConsoleMonitor", "MetricsMonitor", "CompositeMonitor",
    # Policies
    "SchedulingPolicy", "FifoPolicy", "PriorityPolicy",
    "ShortestNeedPolicy", "DeadlinePolicy", "FairnessPolicy",
    # Estimation
    "DemandEstimator",
    # Future
    "FutureRequestStatus",
    # Exceptions
    "AgentGuardError", "AgentNotFoundError", "ResourceNotFoundError",
    "InvalidRequestError", "MaxClaimExceededError",
    "ResourceCapacityExceededError", "QueueFullError",
    "AgentAlreadyRegisteredError",
    # Constants
    "PRIORITY_LOW", "PRIORITY_NORMAL", "PRIORITY_HIGH", "PRIORITY_CRITICAL",
    # Submodules
    "ai",
    # Version
    "__version__",
]
