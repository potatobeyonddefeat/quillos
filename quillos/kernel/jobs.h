#pragma once
#include <stdint.h>

// ================================================================
// Job — The unit of work in QuillOS's distributed scheduler
//
// A job has a type, input payload, and produces a uint32_t result.
// Jobs can run locally or be dispatched to a remote node.
// ================================================================

namespace Jobs {

    enum Type : uint8_t {
        JOB_SUM     = 1,   // Sum array of uint32_t
        JOB_PRODUCT = 2,   // Product of array of uint32_t
        JOB_MAX     = 3,   // Max of array of uint32_t
        JOB_PRIME   = 4,   // Count primes up to N
        JOB_ECHO    = 5,   // Echo back (result = payload length)
    };

    enum Status : uint8_t {
        STATUS_PENDING    = 0,
        STATUS_RUNNING    = 1,
        STATUS_COMPLETED  = 2,
        STATUS_FAILED     = 3,
    };

    static constexpr uint16_t MAX_PAYLOAD = 240;

    struct Job {
        uint32_t id;
        Type     type;
        Status   status;
        uint32_t result;
        uint32_t target_ip;        // 0 = local, nonzero = remote node
        uint64_t submitted_at;     // Tick when submitted
        uint64_t completed_at;     // Tick when result arrived
        uint8_t  payload[MAX_PAYLOAD];
        uint16_t payload_len;
    };

    // Execute a job locally and return the result
    uint32_t execute(Type type, const uint8_t* data, uint16_t len);

    // Get human-readable job type name
    const char* type_name(Type type);
}
