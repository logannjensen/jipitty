#ifndef LJ_NET
#define LJ_NET

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

namespace net
{
using sse_event_callback =
    std::function<void(const std::string&, const std::string&)>;

struct sse_dechunker
{
    sse_dechunker(sse_event_callback cb = nullptr) : callback(cb) {}
    std::string        data_buffer, event_type_buffer, next_chunk;
    bool               started = false;
    sse_event_callback callback;
};

void sse_dechunker_callback(const uint8_t* bytes, size_t size, void* userp,
                            bool is_header);
std::pair<size_t, size_t> find_next_line(const std::string& buf);
std::string               trim_whitespace(const std::string& str);

class url
{
public:
    url() = default;
    explicit url(const std::string& url_string);
    url(const url& other);
    url(url&& other) noexcept;
    url& operator=(const url& other);
    url& operator=(url&& other) noexcept;

    std::string                                  protocol, domain, port, path;
    std::unordered_map<std::string, std::string> query_parameters;
    std::string                                  to_string() const;
    static std::string                           encode(const std::string& str);
    static std::string                           decode(const std::string& str);

private:
    void parse(const std::string& url_string);
    void parse_query_string(const std::string& query);
    void set_default_port();
};

enum class http_method
{
    HTTP_METHOD_NULL = 0,
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH,
    TRACE,
    CONNECT
};

using write_callback = void (*)(const uint8_t*, size_t, void*, bool);

struct subscription
{
    write_callback callback;
    void*          userp;
    subscription(write_callback cb, void* up, bool /*unused*/ = false)
        : callback(cb), userp(up)
    {
    }
};

struct response
{
    int                                          response_code = 0;
    std::string                                  status_line;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t>                         body;
    std::string                                  to_string() const
    {
        return std::string(body.begin(), body.end());
    }
    CURLcode curl_code = CURLE_OK;
};

struct request
{
    explicit request(const url& req_url);
    explicit request(const std::string& url_string);
    request(const url& req_url, http_method method);
    request(const std::string& url_string, http_method method);
    request(const url& req_url, http_method method,
            const std::unordered_map<std::string, std::string>& headers);
    request(const std::string& url_string, http_method method,
            const std::unordered_map<std::string, std::string>& headers);
    request(const url& req_url, http_method method,
            const std::unordered_map<std::string, std::string>& headers,
            const nlohmann::json&                               json_data);
    request(const std::string& url_string, http_method method,
            const std::unordered_map<std::string, std::string>& headers,
            const nlohmann::json&                               json_data);

    url         req_url;
    http_method method = http_method::HTTP_METHOD_NULL;
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t>                         data;
    std::vector<subscription>                    subscriptions;
    void     subscribe(write_callback callback, void* userp);
    void     set_string(const std::string& text_data);
    void     set_data(const std::vector<uint8_t>& binary_data);
    void     set_json(const nlohmann::json& json_data);
    response send();
};

class client
{
public:
    explicit client(url url_to_send_to = {});
    ~client();

    response send(const request& request);

    url         default_url;
    http_method default_method = http_method::HTTP_METHOD_NULL;
    std::unordered_map<std::string, std::string> default_headers;
    std::vector<uint8_t>                         default_data;
    std::vector<subscription>                    default_subscriptions;
    std::string                                  cookie_file;
    bool                                         follow_redirects = false;

    void subscribe(write_callback callback, void* userp);
    void set_default_string(const std::string& text_data);
    void set_default_data(const std::vector<uint8_t>& binary_data);
    void set_default_json(const nlohmann::json& json_data);
    std::vector<std::string> get_cookies();
    void                     set_cookie(const std::string& cookie);
    static std::string       http_method_to_string(http_method method);

private:
    static void curl_deleter(CURL* curl) { curl_easy_cleanup(curl); }
    std::unique_ptr<CURL, decltype(&curl_deleter)> curl_{nullptr,
                                                         &curl_deleter};
    static void                                    ensure_curl_initialized();
    static size_t write_data_callback(void* contents, size_t size, size_t nmemb,
                                      void* userp);
    static size_t write_header_callback(void* contents, size_t size,
                                        size_t nmemb, void* userp);
    struct curl_slist* header_list_ = nullptr;
    struct curl_slist* cookie_list_ = nullptr;
};

} // namespace net
#endif
