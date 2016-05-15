/*
 * Copyright (C) 2015 ScyllaDB
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

#include <boost/circular_buffer.hpp>
#include "latency.hh"
#include <cmath>
#include "core/timer.hh"
#include <iostream>
namespace utils {
/**
 * An exponentially-weighted moving average.
 */
class ewma {
    double _alpha = 0;
    double _interval_in_ns = 0;
    bool _initialized = false;
    uint64_t _count = 0;
    uint64_t _tick_duration_in_ns;
public:
    double rate = 0;
    ewma(latency_counter::duration interval, latency_counter::duration tick_duration) :
        _interval_in_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count()),
        _tick_duration_in_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(tick_duration).count()) {
        _alpha = 1 - std::exp(-_interval_in_ns/_tick_duration_in_ns);
    }

    void add(uint64_t val = 1) {
        _count += val;
    }

    void update() {
        double instant_rate = _count / _interval_in_ns;
        if (_initialized) {
            rate += (_alpha * (instant_rate - rate));
        } else {
            rate = instant_rate;
            _initialized = true;
        }
        _count = 0;
    }

    bool is_initilized() const {
        return _initialized;
    }

    double rate_in_nano() const {
        return rate;
    }
};

class ihistogram {
public:
    // count holds all the events
    int64_t count;
    // total holds only the events we sample
    int64_t total;
    int64_t min;
    int64_t max;
    int64_t sum;
    int64_t started;
    double mean;
    double variance;
    int64_t sample_mask;
    boost::circular_buffer<int64_t> sample;
    ihistogram(size_t size = 1024, int64_t _sample_mask = 0x80)
            : count(0), total(0), min(0), max(0), sum(0), started(0), mean(0), variance(0),
              sample_mask(_sample_mask), sample(
                    size) {
    }
    void mark(int64_t value) {
        if (total == 0 || value < min) {
            min = value;
        }
        if (total == 0 || value > max) {
            max = value;
        }
        if (total == 0) {
            mean = value;
            variance = 0;
        } else {
            double old_m = mean;
            double old_s = variance;

            mean = ((double)(sum + value)) / (total + 1);
            variance = old_s + ((value - old_m) * (value - mean));
        }
        sum += value;
        total++;
        count++;
        sample.push_back(value);
    }

    void mark(latency_counter& lc) {
        if (lc.is_start()) {
            mark(lc.stop().latency_in_nano());
        } else {
            count++;
        }
    }

    /**
     * Return true if the current event should be sample.
     * In the typical case, there is no need to use this method
     * Call set_latency, that would start a latency object if needed.
     */
    bool should_sample() const {
        return total == 0 || (started & sample_mask);
    }
    /**
     * Set the latency according to the sample rate.
     */
    ihistogram& set_latency(latency_counter& lc) {
        if (should_sample()) {
            lc.start();
        }
        started++;
        return *this;
    }

    /**
     * Allow to use the histogram as a counter
     * Increment the total number of events without
     * sampling the value.
     */
    ihistogram& inc() {
        count++;
        return *this;
    }

    int64_t pending() const {
        return started - count;
    }
};

class meter {
public:
    static constexpr latency_counter::duration DURATION = std::chrono::seconds(10);

    uint64_t count = 0;
    ewma rates[3] = {{DURATION, std::chrono::minutes(1)}, {DURATION, std::chrono::minutes(5)}, {DURATION, std::chrono::minutes(15)}};
    latency_counter::time_point start_time;
    timer<> _timer;

    void set_timer() {
        _timer.set_callback([this] {
            update();
        });
        _timer.arm_periodic(DURATION);
    }
    meter() : start_time(latency_counter::now()) {
        set_timer();
    }

    meter(const meter& m) {
        *this = m;
    }
    meter(meter&&) = default;
    void mark(uint64_t n = 1) {
        count += n;
        for (int i = 0; i < 3; i++) {
            rates[i].add(n);
        }
    }

    double mean_rate_in_ms() const{
        if (count == 0) {
            return 0.0;
        }
        double elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(latency_counter::now() - start_time).count();
        return (count / elapsed);
    }

    void update() {
        for (int i = 0; i < 3; i++) {
            rates[i].update();
        }
    }

    meter& operator=(const meter& m) {
        count = m.count;
        start_time = m.start_time;
        for (int i=0; i<3; i++) {
            rates[i] = m.rates[i];
        }
        return *this;
    }
};

/**
 * A timer metric which aggregates timing durations and provides duration statistics, plus
 * throughput statistics via meter
 */
class api_timer {
public:
    ihistogram hist;
    meter met;
    api_timer() = default;
    api_timer(api_timer&&) = default;
    api_timer(const api_timer&) = default;
    api_timer(size_t size, int64_t _sample_mask = 0x80) : hist(size, _sample_mask) {}
    api_timer& operator=(const api_timer&) = default;
    void mark(int duration) {
        if (duration >= 0) {
            hist.mark(duration);
            met.mark();
        }
    }

    void mark(latency_counter& lc) {
        hist.mark(lc);
        met.mark();
    }

    void set_latency(latency_counter& lc) {
        hist.set_latency(lc);
    }
};

}
