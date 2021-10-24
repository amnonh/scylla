#pragma once
#include <seastar/http/httpd.hh>

namespace s3 {
    struct s3_context {
        seastar::sstring base_dir;
    };

    seastar::future<> init(const s3_context&, seastar::httpd::http_server_control&);
}
