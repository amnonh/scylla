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

std::tuple<int, int> split_range(const sstring& range) {
    auto eql = range.find("=");
    auto pos = range.find("-", eql + 1);
    size_t end;
    end = pos + 1;
    return std::make_tuple(std::stoi(range.substr(eql+1, pos)), std::stoi(range.substr(end)));
}

seastar::future<> copy_n(input_stream<char>& is, uint64_t size, output_stream<char>& os) {
    bool eof = false;
    while(size >0  && !eof && !is.eof()) {
        auto buf = co_await is.read();
        if (buf.size() > size) {
            buf.trim(size);
        }
        if (!buf.empty()) {
            size -= buf.size();
            co_await os.write(buf.get(), buf.size());
        } else {
            eof = true;
        }
    }
    co_return;
}

class directory_handler_s3 : public directory_handler {
public:
    directory_handler_s3(const sstring& path) : directory_handler(path) {
    }
    virtual future<std::unique_ptr<reply>> read(sstring file_name, std::unique_ptr<request> req,
            std::unique_ptr<reply> rep) override;
};

future<std::unique_ptr<reply>> directory_handler_s3::read(
        sstring file_name, std::unique_ptr<request> req,
        std::unique_ptr<reply> rep) {
    sstring extension = get_extension(file_name);
    rep->write_body(extension, [req = std::move(req), extension, file_name, this] (output_stream<char>&& s) mutable -> seastar::future<> {
        auto range = req->get_header("Range");
        auto os = output_stream<char>(get_stream(std::move(req), extension, std::move(s)));
        auto file = co_await open_file_dma(file_name, open_flags::ro);
        auto is = make_file_input_stream(file);
        if (range != "") {
            auto [from, to] = split_range(range);
            co_await is.skip(from);
            co_await copy_n(is, (to-from + 1), os);
        } else {
            co_await copy(is, os);
        }
        co_await os.close();
        co_await is.close();
        co_return ;
    });
    return make_ready_future<std::unique_ptr<reply>>(std::move(rep));
}
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
            r.add(GET, url("").remainder("path"), new directory_handler_s3(ctx.base_dir));
        });
    });
}
}
