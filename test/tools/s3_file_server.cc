#include "s3_file_server.hh"
#include <seastar/http/file_handler.hh>
#include <seastar/http/transformers.hh>
#include "log.hh"
#include <seastar/http/handlers.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/fstream.hh>
#include <algorithm>
#include <seastar/core/coroutine.hh>
#include <seastar/core/fstream.hh>

using namespace seastar;

namespace s3 {
logging::logger filelog("s3file");


/**
 * A base class for handlers that interact with files.
 * directory and file handlers both share some common logic
 * with regards to file handling.
 * they both needs to read a file from the disk, optionally transform it,
 * and return the result or page not found on error
 */
class file_writer_handler : public handler_base {
    sstring _base_path;
public:
    file_writer_handler(const sstring& base_path) : _base_path(base_path) {

    }
    sstring get_file_name(const sstring& url) {
        return _base_path + url;
    }
    future<std::unique_ptr<reply>> handle(const sstring& path,
            std::unique_ptr<request> req, std::unique_ptr<reply> rep) override;

    ~file_writer_handler() = default;
};

future<std::unique_ptr<reply>> file_writer_handler::handle(const sstring& path,
           std::unique_ptr<request> req, std::unique_ptr<reply> rep) {
    filelog.debug("file_writer_handler {} req {} ", req->_method, get_file_name(path));
    auto file = co_await seastar::open_file_dma(get_file_name(path), seastar::open_flags::create | seastar::open_flags::wo);
    auto os = co_await seastar::make_file_output_stream(file);
    co_await copy(*(req->content_stream), os);
    co_await os.close();
    co_return std::move(rep);
}

future<> init(const s3_context& ctx, seastar::httpd::http_server_control& http_server) {
    return http_server.server().invoke_on_all([](seastar::httpd::http_server& server){
        server.set_content_streaming(true);
    }).then([&ctx, &http_server]{
    return http_server.set_routes([&ctx](routes& r) {
            r.put(GET, "/", new httpd::file_handler(ctx.base_dir + "/index.html",
                    new content_replace("html")));
            r.add(PUT, url("").remainder("path"), new file_writer_handler(ctx.base_dir));
            r.add(GET, url("").remainder("path"), new httpd::directory_handler(ctx.base_dir));
        });
    });
}
}
