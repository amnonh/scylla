/*
 * Copyright 2024-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include <seastar/core/metrics_registration.hh>
#include "utils/rjson.hh"
#include "db/consistency_level_type.hh"
#include "replica/database.hh"

namespace alternator {

/**
 * \brief consumed_capacity_counter is a base class that holds the bookkeeping
 *  to calculate RCU and WCU
 *
 * We prefer to use integer for counting the unit while DynamoDB API uses double.
 *
 * So the internal calculation is done using internal units and the consumed capacity
 * is reported as a double.
 *
 * We use consumed_capacity_counter for calculation of a specific action
 *
 * It is also used to update the response if needed and optionally update a metric with internal units.
 */
class consumed_capacity_counter {
public:
    consumed_capacity_counter() = default;
    consumed_capacity_counter(bool should_add) : _should_add(should_add){}
    bool operator()() const noexcept {
        return _should_add;
    }

    consumed_capacity_counter& operator +=(uint64_t bytes);
    double get_consumed_capacity_units() const noexcept;
    void add_consumed_capacity_to_response_if_needed(rjson::value& response) const noexcept;
    virtual bool is_dummy() const noexcept {
        return false;
    }
    virtual ~consumed_capacity_counter() = default;
    void update_metric(replica::consumption_unit_counter& metric) const noexcept;
protected:
    /**
     * \brief get_internal_units calculate the internal units from the total bytes based on the type of the request
     */
    virtual uint64_t get_internal_units() const noexcept = 0;
    bool _should_add = false;
    uint64_t _total_bytes = 0;
};

class dummy_consumed_capacity_counter : public consumed_capacity_counter {
    virtual bool is_dummy() const noexcept {
        return true;
    }
    virtual uint64_t get_internal_units() const noexcept {
        return 0;
    }
};

class rcu_consumed_capacity_counter : public consumed_capacity_counter {
    virtual uint64_t get_internal_units() const noexcept;
    bool _is_quorum = false;
public:
    rcu_consumed_capacity_counter(const rjson::value& request, bool is_quorum);
};

class wcu_consumed_capacity_counter : public consumed_capacity_counter {
    virtual uint64_t get_internal_units() const noexcept;
public:
    wcu_consumed_capacity_counter(const rjson::value& request);
};

}