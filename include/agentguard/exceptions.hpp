#pragma once

#include "agentguard/types.hpp"
#include <stdexcept>
#include <string>

namespace agentguard {

class AgentGuardException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class AgentNotFoundException : public AgentGuardException {
public:
    explicit AgentNotFoundException(AgentId id)
        : AgentGuardException("Agent not found: " + std::to_string(id))
        , agent_id_(id) {}

    AgentId agent_id() const noexcept { return agent_id_; }

private:
    AgentId agent_id_;
};

class ResourceNotFoundException : public AgentGuardException {
public:
    explicit ResourceNotFoundException(ResourceTypeId id)
        : AgentGuardException("Resource type not found: " + std::to_string(id))
        , resource_type_id_(id) {}

    ResourceTypeId resource_type_id() const noexcept { return resource_type_id_; }

private:
    ResourceTypeId resource_type_id_;
};

class InvalidRequestException : public AgentGuardException {
public:
    using AgentGuardException::AgentGuardException;
};

class MaxClaimExceededException : public InvalidRequestException {
public:
    MaxClaimExceededException(AgentId agent, ResourceTypeId resource,
                              ResourceQuantity requested, ResourceQuantity max_claim)
        : InvalidRequestException(
            "Agent " + std::to_string(agent) +
            " requested " + std::to_string(requested) +
            " of resource " + std::to_string(resource) +
            " but max claim is " + std::to_string(max_claim)) {}
};

class ResourceCapacityExceededException : public InvalidRequestException {
public:
    ResourceCapacityExceededException(ResourceTypeId resource,
                                       ResourceQuantity requested,
                                       ResourceQuantity total)
        : InvalidRequestException(
            "Requested " + std::to_string(requested) +
            " of resource " + std::to_string(resource) +
            " but total capacity is " + std::to_string(total)) {}
};

class QueueFullException : public AgentGuardException {
public:
    QueueFullException()
        : AgentGuardException("Request queue is full") {}
};

class AgentAlreadyRegisteredException : public AgentGuardException {
public:
    explicit AgentAlreadyRegisteredException(AgentId id)
        : AgentGuardException("Agent already registered: " + std::to_string(id))
        , agent_id_(id) {}

    AgentId agent_id() const noexcept { return agent_id_; }

private:
    AgentId agent_id_;
};

} // namespace agentguard
