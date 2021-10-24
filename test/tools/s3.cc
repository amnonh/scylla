/*
 * Copyright (C) 2019-present ScyllaDB
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

#include <seastar/http/httpd.hh>
#include "log.hh"
#include <seastar/util/defer.hh>
#include "gms/inet_address.hh"
#include <seastar/core/reactor.hh>
#include <seastar/core/app-template.hh>
#include <seastar/core/distributed.hh>
#include <seastar/core/thread.hh>
#include <seastar/core/abort_source.hh>
#include "s3_file_server.hh"
using namespace std::chrono_literals;
logging::logger s3log("s3log");

template <typename Func>
static auto defer_verbose_shutdown(const char* what, Func&& func) {
    auto vfunc = [what, func = std::forward<Func>(func)] () mutable {
        s3log.info("Shutting down {}", what);
        try {
            func();
        } catch (...) {
            s3log.error("Unexpected error shutting down {}: {}", what, std::current_exception());
            throw;
        }
        s3log.info("Shutting down {} was successful", what);
    };

    auto ret = seastar::deferred_action(std::move(vfunc));
    return seastar::make_shared<decltype(ret)>(std::move(ret));
}

class stop_signal {
    bool _caught = false;
    condition_variable _cond;
    sharded<seastar::abort_source> _abort_sources;
    future<> _broadcasts_to_abort_sources_done = make_ready_future<>();
private:
    void signaled() {
        if (_caught) {
            return;
        }
        _caught = true;
        _cond.broadcast();
        _broadcasts_to_abort_sources_done = _broadcasts_to_abort_sources_done.then([this] {
            return _abort_sources.invoke_on_all(&abort_source::request_abort);
        });
    }
public:
    stop_signal() {
        _abort_sources.start().get();
        engine().handle_signal(SIGINT, [this] { signaled(); });
        engine().handle_signal(SIGTERM, [this] { signaled(); });
    }
    ~stop_signal() {
        // There's no way to unregister a handler yet, so register a no-op handler instead.
        engine().handle_signal(SIGINT, [] {});
        engine().handle_signal(SIGTERM, [] {});
        _broadcasts_to_abort_sources_done.get();
        _abort_sources.stop().get();
    }
    future<> wait() {
        return _cond.wait([this] { return _caught; });
    }
    bool stopping() const {
        return _caught;
    }
    abort_source& as_local_abort_source() { return _abort_sources.local(); }
    sharded<abort_source>& as_sharded_abort_source() { return _abort_sources; }
};

int main(int argc, char* argv[]) {
    namespace bpo = boost::program_options;

    seastar::app_template::config app_cfg;
    app_cfg.name = "s3";
    seastar::sstring api_address = "0.0.0.0";
    uint16_t api_port = 9990;
    app_cfg.default_task_quota = 500us;
    app_cfg.auto_handle_sigint_sigterm = false;
    seastar::app_template app(std::move(app_cfg));
    app.add_options()
                ("port", bpo::value<uint>(), "port to listen to")
                ("address", bpo::value<sstring>(), "ip address to listen to");
    app.add_positional_options({
        {"basedir", bpo::value<std::vector<sstring>>(), "the base directory to server files from", 1},
    });

    seastar::httpd::http_server_control http_server;
    s3::s3_context ctx;

    return app.run(argc, argv, [&] () -> seastar::future<int> {
        if (!app.configuration().contains("basedir")) {
            s3log.info("Base directory is missing!");
            return make_ready_future<int>(-1);
        }
        if (app.configuration().contains("address")) {
            api_address = app.configuration()["address"].as<sstring>();
        }
        if (app.configuration().contains("port")) {
            api_port = app.configuration()["port"].as<uint>();
        }
        ctx.base_dir = app.configuration()["basedir"].as<std::vector<sstring>>()[0];
        s3log.info("serving files from: {}", ctx.base_dir);
        return seastar::async([&app, &http_server, &api_address, &api_port, &ctx] {
            http_server.start("API").get();
            ::stop_signal stop_signal;
            s3::init(ctx, http_server).get();
            auto family = std::make_optional(seastar::net::inet_address::family::INET);
            auto ip = [&] {
                try {
                    return gms::inet_address::lookup(api_address, family, std::nullopt).get0();
                } catch (...) {
                    std::throw_with_nested(std::runtime_error(fmt::format("Unable to resolve api_address {}", api_address)));
                }
            }();
            auto stop_http_server = defer_verbose_shutdown("API server", [&http_server] {
                http_server.stop().get();
            });
            http_server.listen(seastar::socket_address{ip, api_port}).get();
            s3log.info("S3 file server listening on {}:{} ...", api_address, api_port);
            stop_signal.wait().get();
            s3log.info("Signal received; shutting down");
            return 0;
        });
    });
}

