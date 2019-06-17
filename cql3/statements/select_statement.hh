/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Copyright (C) 2015 ScyllaDB
 *
 * Modified by ScyllaDB
 */

/*
 * This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "cql3/statements/raw/cf_statement.hh"
#include "cql3/statements/raw/select_statement.hh"
#include "cql3/cql_statement.hh"
#include "cql3/selection/selection.hh"
#include "cql3/selection/raw_selector.hh"
#include "cql3/restrictions/statement_restrictions.hh"
#include "cql3/result_set.hh"
#include "exceptions/unrecognized_entity_exception.hh"
#include "service/client_state.hh"
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/distributed.hh>
#include "validation.hh"
#include "transport/messages/result_message.hh"

namespace cql3 {

namespace statements {

/**
 * Encapsulates a completely parsed SELECT query, including the target
 * column family, expression, result count, and ordering clause.
 *
 */
class select_statement : public cql_statement {
public:
    using parameters = raw::select_statement::parameters;
    using ordering_comparator_type = raw::select_statement::ordering_comparator_type;
protected:
    static constexpr int DEFAULT_COUNT_PAGE_SIZE = 10000;
    static thread_local const ::shared_ptr<parameters> _default_parameters;
    schema_ptr _schema;
    uint32_t _bound_terms;
    ::shared_ptr<parameters> _parameters;
    ::shared_ptr<selection::selection> _selection;
    ::shared_ptr<restrictions::statement_restrictions> _restrictions;
    ::shared_ptr<std::vector<size_t>> _group_by_cell_indices; ///< Indices in result row of cells holding GROUP BY values.
    bool _is_reversed;
    ::shared_ptr<term> _limit;
    ::shared_ptr<term> _per_partition_limit;

    template<typename T>
    using compare_fn = raw::select_statement::compare_fn<T>;

    using result_row_type = raw::select_statement::result_row_type;

    /**
     * The comparator used to orders results when multiple keys are selected (using IN).
     */
    ordering_comparator_type _ordering_comparator;

    query::partition_slice::option_set _opts;
    cql_stats& _stats;
protected :
    virtual future<::shared_ptr<cql_transport::messages::result_message>> do_execute(service::storage_proxy& proxy,
        service::query_state& state, const query_options& options);
    friend class select_statement_executor;
public:
    select_statement(schema_ptr schema,
            uint32_t bound_terms,
            ::shared_ptr<parameters> parameters,
            ::shared_ptr<selection::selection> selection,
            ::shared_ptr<restrictions::statement_restrictions> restrictions,
            ::shared_ptr<std::vector<size_t>> group_by_cell_indices,
            bool is_reversed,
            ordering_comparator_type ordering_comparator,
            ::shared_ptr<term> limit,
            ::shared_ptr<term> per_partition_limit,
            cql_stats& stats);

    virtual bool uses_function(const sstring& ks_name, const sstring& function_name) const override;

    virtual ::shared_ptr<const cql3::metadata> get_result_metadata() const override;
    virtual uint32_t get_bound_terms() override;
    virtual future<> check_access(const service::client_state& state) override;
    virtual void validate(service::storage_proxy&, const service::client_state& state) override;
    virtual bool depends_on_keyspace(const sstring& ks_name) const;
    virtual bool depends_on_column_family(const sstring& cf_name) const;

    virtual future<::shared_ptr<cql_transport::messages::result_message>> execute(service::storage_proxy& proxy,
        service::query_state& state, const query_options& options) override;

    future<::shared_ptr<cql_transport::messages::result_message>> execute(service::storage_proxy& proxy,
        lw_shared_ptr<query::read_command> cmd, dht::partition_range_vector&& partition_ranges, service::query_state& state,
         const query_options& options, gc_clock::time_point now);

    struct primary_key {
        dht::decorated_key partition;
        clustering_key_prefix clustering;
    };

    shared_ptr<cql_transport::messages::result_message> process_results(foreign_ptr<lw_shared_ptr<query::result>> results,
        lw_shared_ptr<query::read_command> cmd, const query_options& options, gc_clock::time_point now);

    const sstring& keyspace() const;

    const sstring& column_family() const;

    query::partition_slice make_partition_slice(const query_options& options);

    ::shared_ptr<restrictions::statement_restrictions> get_restrictions() const;

    bool has_group_by() { return _group_by_cell_indices && !_group_by_cell_indices->empty(); }

protected:
    uint32_t do_get_limit(const query_options& options, ::shared_ptr<term> limit) const;
    uint32_t get_limit(const query_options& options) const {
        return do_get_limit(options, _limit);
    }
    uint32_t get_per_partition_limit(const query_options& options) const {
        return do_get_limit(options, _per_partition_limit);
    }
    bool needs_post_query_ordering() const;
    virtual void update_stats_rows_read(int64_t rows_read) {
        _stats.rows_read += rows_read;
    }
};

class primary_key_select_statement : public select_statement {
public:
    primary_key_select_statement(schema_ptr schema,
                     uint32_t bound_terms,
                     ::shared_ptr<parameters> parameters,
                     ::shared_ptr<selection::selection> selection,
                     ::shared_ptr<restrictions::statement_restrictions> restrictions,
                     ::shared_ptr<std::vector<size_t>> group_by_cell_indices,
                     bool is_reversed,
                     ordering_comparator_type ordering_comparator,
                     ::shared_ptr<term> limit,
                     ::shared_ptr<term> per_partition_limit,
                     cql_stats &stats);
};

class indexed_table_select_statement : public select_statement {
    secondary_index::index _index;
    ::shared_ptr<restrictions::restrictions> _used_index_restrictions;
    schema_ptr _view_schema;
    noncopyable_function<dht::partition_range_vector(const query_options&)> _get_partition_ranges_for_posting_list;
    noncopyable_function<query::partition_slice(const query_options&)> _get_partition_slice_for_posting_list;
public:
    static ::shared_ptr<cql3::statements::select_statement> prepare(database& db,
                                                                    schema_ptr schema,
                                                                    uint32_t bound_terms,
                                                                    ::shared_ptr<parameters> parameters,
                                                                    ::shared_ptr<selection::selection> selection,
                                                                    ::shared_ptr<restrictions::statement_restrictions> restrictions,
                                                                    ::shared_ptr<std::vector<size_t>> group_by_cell_indices,
                                                                    bool is_reversed,
                                                                    ordering_comparator_type ordering_comparator,
                                                                    ::shared_ptr<term> limit,
                                                                     ::shared_ptr<term> per_partition_limit,
                                                                    cql_stats &stats);

    indexed_table_select_statement(schema_ptr schema,
                                   uint32_t bound_terms,
                                   ::shared_ptr<parameters> parameters,
                                   ::shared_ptr<selection::selection> selection,
                                   ::shared_ptr<restrictions::statement_restrictions> restrictions,
                                   ::shared_ptr<std::vector<size_t>> group_by_cell_indices,
                                   bool is_reversed,
                                   ordering_comparator_type ordering_comparator,
                                   ::shared_ptr<term> limit,
                                   ::shared_ptr<term> per_partition_limit,
                                   cql_stats &stats,
                                   const secondary_index::index& index,
                                   ::shared_ptr<restrictions::restrictions> used_index_restrictions,
                                   schema_ptr view_schema);

private:
    virtual future<::shared_ptr<cql_transport::messages::result_message>> do_execute(service::storage_proxy& proxy,
                                                                                     service::query_state& state, const query_options& options) override;

    ::shared_ptr<const service::pager::paging_state> generate_view_paging_state_from_base_query_results(::shared_ptr<const service::pager::paging_state> paging_state,
            const foreign_ptr<lw_shared_ptr<query::result>>& results, service::storage_proxy& proxy, service::query_state& state, const query_options& options) const;

    future<dht::partition_range_vector, ::shared_ptr<const service::pager::paging_state>> find_index_partition_ranges(service::storage_proxy& proxy,
                                                                    service::query_state& state,
                                                                    const query_options& options);

    future<std::vector<primary_key>, ::shared_ptr<const service::pager::paging_state>> find_index_clustering_rows(service::storage_proxy& proxy,
                                                                service::query_state& state,
                                                                const query_options& options);

    shared_ptr<cql_transport::messages::result_message>
    process_base_query_results(
            foreign_ptr<lw_shared_ptr<query::result>> results,
            lw_shared_ptr<query::read_command> cmd,
            service::storage_proxy& proxy,
            service::query_state& state,
            const query_options& options,
            gc_clock::time_point now,
            ::shared_ptr<const service::pager::paging_state> paging_state);

    lw_shared_ptr<query::read_command>
    prepare_command_for_base_query(const query_options& options, service::query_state& state, gc_clock::time_point now, bool use_paging);

    future<foreign_ptr<lw_shared_ptr<query::result>>, lw_shared_ptr<query::read_command>>
    do_execute_base_query(
            service::storage_proxy& proxy,
            dht::partition_range_vector&& partition_ranges,
            service::query_state& state,
            const query_options& options,
            gc_clock::time_point now,
            ::shared_ptr<const service::pager::paging_state> paging_state);
    future<shared_ptr<cql_transport::messages::result_message>>
    execute_base_query(
            service::storage_proxy& proxy,
            dht::partition_range_vector&& partition_ranges,
            service::query_state& state,
            const query_options& options,
            gc_clock::time_point now,
            ::shared_ptr<const service::pager::paging_state> paging_state);

    // Function for fetching the selected columns from a list of clustering rows.
    // It is currently used only in our Secondary Index implementation - ordinary
    // CQL SELECT statements do not have the syntax to request a list of rows.
    // FIXME: The current implementation is very inefficient - it requests each
    // row separately (and, incrementally, in parallel). Even multiple rows from a single
    // partition are requested separately. This last case can be easily improved,
    // but to implement the general case (multiple rows from multiple partitions)
    // efficiently, we will need more support from other layers.
    // Keys are ordered in token order (see #3423)
    future<foreign_ptr<lw_shared_ptr<query::result>>, lw_shared_ptr<query::read_command>>
    do_execute_base_query(
            service::storage_proxy& proxy,
            std::vector<primary_key>&& primary_keys,
            service::query_state& state,
            const query_options& options,
            gc_clock::time_point now,
            ::shared_ptr<const service::pager::paging_state> paging_state);
    future<shared_ptr<cql_transport::messages::result_message>>
    execute_base_query(
            service::storage_proxy& proxy,
            std::vector<primary_key>&& primary_keys,
            service::query_state& state,
            const query_options& options,
            gc_clock::time_point now,
            ::shared_ptr<const service::pager::paging_state> paging_state);

    virtual void update_stats_rows_read(int64_t rows_read) override {
        _stats.rows_read += rows_read;
        _stats.secondary_index_rows_read += rows_read;
    }

    future<::shared_ptr<cql_transport::messages::result_message::rows>>read_posting_list(
            service::storage_proxy& proxy,
            const query_options& options,
            int32_t limit,
            service::query_state& state,
            gc_clock::time_point now,
            db::timeout_clock::time_point timeout,
            bool include_base_clustering_key);

    dht::partition_range_vector get_partition_ranges_for_local_index_posting_list(const query_options& options) const;
    dht::partition_range_vector get_partition_ranges_for_global_index_posting_list(const query_options& options) const;

    query::partition_slice get_partition_slice_for_local_index_posting_list(const query_options& options) const;
    query::partition_slice get_partition_slice_for_global_index_posting_list(const query_options& options) const;

};

}

}
