#include "net.h"
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <vector>

// net::client
void net::client::ensure_curl_initialized()
{
    static bool initialized = false;
    if (!initialized)
    {
        curl_global_init(CURL_GLOBAL_ALL);
        atexit(curl_global_cleanup);
        initialized = true;
    }
}

net::client::client(net::url url_to_send_to)
    : default_url(std::move(url_to_send_to)),
      default_method(http_method::HTTP_METHOD_NULL),
      curl_(curl_easy_init(), &curl_deleter), header_list_(nullptr),
      cookie_list_(nullptr)
{
    ensure_curl_initialized();
    if (!curl_)
    {
        throw std::runtime_error("CURL initialization failed");
    }
    curl_easy_setopt(curl_.get(), CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl_.get(), CURLOPT_COOKIEJAR, "");
}

net::client::~client()
{
    if (header_list_ != nullptr)
    {
        curl_slist_free_all(header_list_);
    }
    if (cookie_list_ != nullptr)
    {
        curl_slist_free_all(cookie_list_);
    }
}

static void
set_default_content_type(std::unordered_map<std::string, std::string>& headers,
                         const std::string& default_type)
{
    if (headers.find("Content-Type") == headers.end())
        headers["Content-Type"] = default_type;
}

std::string net::client::http_method_to_string(const net::http_method method)
{
    switch (method)
    {
    case net::http_method::GET:
        return "GET";
    case net::http_method::POST:
        return "POST";
    case net::http_method::PUT:
        return "PUT";
    case net::http_method::DELETE:
        return "DELETE";
    case net::http_method::HEAD:
        return "HEAD";
    case net::http_method::OPTIONS:
        return "OPTIONS";
    case net::http_method::PATCH:
        return "PATCH";
    case net::http_method::TRACE:
        return "TRACE";
    case net::http_method::CONNECT:
        return "CONNECT";
    default:
        return "UNKNOWN";
    }
}

std::pair<size_t, size_t> net::find_next_line(const std::string& buf)
{
    for (size_t i = 0; i < buf.size(); ++i)
    {
        if (buf[i] == '\n')
            return {i, 1};
        if (buf[i] == '\r')
        {
            if (i + 1 < buf.size())
            {
                if (buf[i + 1] == '\n')
                    return {i, 2};
                else
                    return {i, 1};
            }
            else
            {
                return {std::string::npos, 0};
            }
        }
    }
    return {std::string::npos, 0};
}

void net::sse_dechunker_callback(const uint8_t* bytes, const size_t size,
                                 void* userp, bool is_header)
{
    if (!is_header)
    {
        sse_dechunker* dechunker = static_cast<sse_dechunker*>(userp);
        std::string&   buffer    = dechunker->next_chunk;
        buffer.append(reinterpret_cast<const char*>(bytes), size);
        static const std::string bom = "\xEF\xBB\xBF";
        if (!dechunker->started && buffer.compare(0, bom.size(), bom) == 0)
            buffer.erase(0, bom.size());

        while (true)
        {
            auto   next_line  = find_next_line(buffer);
            size_t line_start = next_line.first;
            size_t line_len   = next_line.second;
            if (line_start == std::string::npos)
                break;

            std::string line = buffer.substr(0, line_start);
            buffer.erase(0, line_start + line_len);

            if (line.empty())
            {
                if (!dechunker->data_buffer.empty())
                {
                    if (dechunker->data_buffer.back() == '\n')
                        dechunker->data_buffer.pop_back();
                    if (dechunker->callback)
                    {
                        dechunker->callback(dechunker->event_type_buffer,
                                            dechunker->data_buffer);
                    }
                    dechunker->data_buffer.clear();
                    dechunker->event_type_buffer.clear();
                }
                continue;
            }

            if (line[0] == ':')
                continue;

            std::string field, value;
            size_t      colon = line.find(':');
            if (colon != std::string::npos)
            {
                field = line.substr(0, colon);
                value = line.substr(colon + 1);
                if (!value.empty() && value[0] == ' ')
                    value.erase(0, 1);
            }
            else
            {
                field = line;
                value = "";
            }

            if (field == "event")
            {
                dechunker->event_type_buffer = value;
            }
            else if (field == "data")
            {
                dechunker->data_buffer.append(value);
                dechunker->data_buffer.push_back('\n');
            }
        }
    }
}

std::string net::trim_whitespace(const std::string& str)
{
    auto start =
        std::find_if_not(str.begin(), str.end(),
                         [](unsigned char ch) { return std::isspace(ch); });
    auto end =
        std::find_if_not(str.rbegin(), str.rend(),
                         [](unsigned char ch) { return std::isspace(ch); })
            .base();
    return (start < end) ? std::string(start, end) : std::string();
}

void net::client::subscribe(net::write_callback callback, void* userp)
{
    default_subscriptions.emplace_back(callback, userp, false);
}

void net::client::set_default_string(const std::string& text_data)
{
    default_data.assign(text_data.begin(), text_data.end());
}

void net::client::set_default_data(const std::vector<uint8_t>& binary_data)
{
    default_data = binary_data;
    set_default_content_type(default_headers, "application/octet-stream");
}

void net::client::set_default_json(const nlohmann::json& json_data)
{
    std::string json_str = json_data.dump();
    default_data.assign(json_str.begin(), json_str.end());
    set_default_content_type(default_headers, "application/json");
}

struct write_callback_internal
{
    net::response*                 response;
    std::vector<net::subscription> subscribers;
    std::string                    raw_headers;
};

static void call_subscriber(net::subscription& subscription, void* contents,
                            size_t total, bool header)
{
    subscription.callback(static_cast<uint8_t*>(contents), total,
                          subscription.userp, header);
}

size_t net::client::write_header_callback(void* contents, size_t size,
                                          size_t nmemb, void* userp)
{
    write_callback_internal* user_callback_data =
        static_cast<write_callback_internal*>(userp);
    user_callback_data->raw_headers.append(static_cast<char*>(contents),
                                           size * nmemb);

    for (auto& subscription : user_callback_data->subscribers)
    {
        call_subscriber(subscription, contents, size * nmemb, true);
    }
    return size * nmemb;
}

size_t net::client::write_data_callback(void* contents, size_t size,
                                        size_t nmemb, void* userp)
{
    write_callback_internal* user_callback_data =
        static_cast<write_callback_internal*>(userp);
    user_callback_data->response->body.insert(
        user_callback_data->response->body.end(),
        static_cast<uint8_t*>(contents),
        static_cast<uint8_t*>(contents) + (size * nmemb));
    for (auto& subscription : user_callback_data->subscribers)
    {
        call_subscriber(subscription, contents, size * nmemb, false);
    }
    return size * nmemb;
}

static void
parse_raw_headers(const std::string&                            raw_headers,
                  std::unordered_map<std::string, std::string>& headers,
                  std::string&                                  status_line)
{
    std::istringstream stream(raw_headers);
    std::string        line;
    if (std::getline(stream, line))
    {
        status_line = net::trim_whitespace(line);
    }

    while (std::getline(stream, line))
    {
        line = net::trim_whitespace(line);

        if (line.empty())
            continue;

        std::size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos)
            continue;

        std::string key   = net::trim_whitespace(line.substr(0, colon_pos));
        std::string value = net::trim_whitespace(line.substr(colon_pos + 1));

        if (headers.find(key) != headers.end())
            headers[key] += ", " + value;
        else
            headers[key] = value;
    }
}

std::vector<std::string> net::client::get_cookies()
{
    std::vector<std::string> cookies;
    if (cookie_list_ != nullptr)
    {
        curl_slist_free_all(cookie_list_);
        cookie_list_ = nullptr;
    }

    CURLcode res =
        curl_easy_getinfo(curl_.get(), CURLINFO_COOKIELIST, &cookie_list_);
    if (!res && cookie_list_)
    {
        struct curl_slist* each = cookie_list_;
        while (each)
        {
            cookies.push_back(each->data);
            each = each->next;
        }
    }
    return cookies;
}

void net::client::set_cookie(const std::string& cookie)
{
    curl_easy_setopt(curl_.get(), CURLOPT_COOKIE, cookie.c_str());
}

net::response net::client::send(const net::request& request)
{
    net::response response;

    net::url url_to_send_to;

    if (!request.req_url.domain.empty())
        url_to_send_to = request.req_url;
    else if (!default_url.domain.empty())
        url_to_send_to = default_url;
    else
        url_to_send_to = net::url("http://localhost");

    for (const auto& param : default_url.query_parameters)
        url_to_send_to.query_parameters[param.first] = param.second;

    for (const auto& param : request.req_url.query_parameters)
        url_to_send_to.query_parameters[param.first] = param.second;

    curl_easy_setopt(curl_.get(), CURLOPT_URL,
                     url_to_send_to.to_string().c_str());
    curl_easy_setopt(curl_.get(), CURLOPT_FOLLOWLOCATION,
                     follow_redirects ? 1L : 0L);

    if (!request.data.empty())
    {
        curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDS, request.data.data());
        curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDSIZE,
                         request.data.size());
    }
    else if (!default_data.empty())
    {
        curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDS, default_data.data());
        curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDSIZE,
                         default_data.size());
    }

    if (request.method != net::http_method::HTTP_METHOD_NULL)
    {
        curl_easy_setopt(curl_.get(), CURLOPT_CUSTOMREQUEST,
                         http_method_to_string(request.method).c_str());
    }
    else if (default_method != net::http_method::HTTP_METHOD_NULL)
    {
        curl_easy_setopt(curl_.get(), CURLOPT_CUSTOMREQUEST,
                         http_method_to_string(default_method).c_str());
    }

    std::unordered_map<std::string, std::string> headers_to_send;

    for (const auto& header : default_headers)
        headers_to_send[header.first] = header.second;

    for (const auto& header : request.headers)
        headers_to_send[header.first] = header.second;

    if (header_list_ != nullptr)
    {
        curl_slist_free_all(header_list_);
        header_list_ = nullptr;
    }

    if (!headers_to_send.empty())
    {
        for (const auto& header : headers_to_send)
        {
            std::string header_string = header.first + ": " + header.second;
            header_list_ =
                curl_slist_append(header_list_, header_string.c_str());
            curl_easy_setopt(curl_.get(), CURLOPT_HTTPHEADER, header_list_);
        }
    }

    std::vector<net::subscription> subscriptions{};
    subscriptions.reserve(default_subscriptions.size() +
                          request.subscriptions.size());
    subscriptions.insert(subscriptions.end(), default_subscriptions.begin(),
                         default_subscriptions.end());
    subscriptions.insert(subscriptions.end(), request.subscriptions.begin(),
                         request.subscriptions.end());

    write_callback_internal callback_data = {&response, subscriptions, {}};

    curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, write_data_callback);
    curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &callback_data);

    curl_easy_setopt(curl_.get(), CURLOPT_HEADERFUNCTION,
                     write_header_callback);
    curl_easy_setopt(curl_.get(), CURLOPT_HEADERDATA, &callback_data);

    curl_easy_setopt(curl_.get(), CURLOPT_COOKIEFILE, cookie_file.c_str());
    curl_easy_setopt(curl_.get(), CURLOPT_COOKIEJAR, cookie_file.c_str());

    response.curl_code = curl_easy_perform(curl_.get());
    if (response.curl_code == CURLE_OK)
    {
        long http_code = 0;
        curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code);
        response.response_code = static_cast<int>(http_code);
    }

    parse_raw_headers(callback_data.raw_headers, response.headers,
                      response.status_line);
    return response;
}

// net::url
net::url::url(const std::string& url_string) { parse(url_string); }

net::url::url(const net::url& other)
    : protocol(other.protocol), domain(other.domain), port(other.port),
      path(other.path), query_parameters(other.query_parameters)
{
}

net::url::url(net::url&& other) noexcept
    : protocol(std::move(other.protocol)), domain(std::move(other.domain)),
      port(std::move(other.port)), path(std::move(other.path)),
      query_parameters(std::move(other.query_parameters))
{
}

net::url& net::url::operator=(const net::url& other)
{
    if (this != &other)
    {
        protocol         = other.protocol;
        domain           = other.domain;
        port             = other.port;
        path             = other.path;
        query_parameters = other.query_parameters;
    }
    return *this;
}

net::url& net::url::operator=(net::url&& other) noexcept
{
    if (this != &other)
    {
        protocol         = std::move(other.protocol);
        domain           = std::move(other.domain);
        port             = std::move(other.port);
        path             = std::move(other.path);
        query_parameters = std::move(other.query_parameters);
    }
    return *this;
}

void net::url::parse(const std::string& url_string)
{
    std::string::const_iterator it  = url_string.begin();
    std::string::const_iterator end = url_string.end();

    auto protocol_end = std::find(it, end, ':');
    if (protocol_end != end && (protocol_end + 1) != end &&
        *(protocol_end + 1) == '/' && *(protocol_end + 2) == '/')
    {
        protocol = std::string(it, protocol_end);
        it       = protocol_end + 3;
    }
    else
    {
        protocol = "http";
    }

    auto domain_end = std::find(it, end, '/');
    auto port_start = std::find(it, domain_end, ':');
    if (port_start != domain_end)
    {
        domain = std::string(it, port_start);
        port   = std::string(port_start + 1, domain_end);
    }
    else
    {
        domain = std::string(it, domain_end);
        set_default_port();
    }

    if (domain.empty())
    {
        throw std::invalid_argument("URL is missing a domain");
    }

    it = domain_end;

    auto path_end = std::find(it, end, '?');
    path          = std::string(it, path_end);
    it            = path_end;

    if (it != end && *it == '?')
    {
        ++it;
        parse_query_string(std::string(it, end));
    }
}

void net::url::set_default_port()
{
    if (protocol == "http")
        port = "80";
    else if (protocol == "https")
        port = "443";
    else if (protocol == "ftp")
        port = "21";
    else if (protocol == "sftp")
        port = "22";
    else
        port.erase();
}

std::string net::url::encode(const std::string& str)
{
    CURL*       curl    = curl_easy_init();
    char*       encoded = curl_easy_escape(curl, str.c_str(), str.length());
    std::string result  = encoded ? std::string(encoded) : "";
    if (encoded)
        curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

std::string net::url::decode(const std::string& str)
{
    CURL* curl = curl_easy_init();
    int   outlength;
    char* decoded =
        curl_easy_unescape(curl, str.c_str(), str.length(), &outlength);
    std::string result = decoded ? std::string(decoded, outlength) : "";
    if (decoded)
        curl_free(decoded);
    curl_easy_cleanup(curl);
    return result;
}

void net::url::parse_query_string(const std::string& query)
{
    std::istringstream query_stream(query);
    std::string        pair;
    while (std::getline(query_stream, pair, '&'))
    {
        auto equal_pos = pair.find('=');
        if (equal_pos != std::string::npos)
        {
            std::string key       = decode(pair.substr(0, equal_pos));
            std::string value     = decode(pair.substr(equal_pos + 1));
            query_parameters[key] = value;
        }
        else
        {
            query_parameters[decode(pair)] = "";
        }
    }
}

std::string net::url::to_string() const
{
    std::ostringstream oss;
    oss << protocol << "://" << domain;
    if (!((protocol == "http" && port == "80") ||
          (protocol == "https" && port == "443")))
        oss << ":" << port;

    if (path.empty())
        oss << '/';
    else if (path[0] != '/')
        oss << '/' << path;
    else
        oss << path;

    if (!query_parameters.empty())
    {
        oss << "?";
        bool first = true;
        for (const auto& param : query_parameters)
        {
            if (!first)
            {
                oss << "&";
            }
            oss << encode(param.first) << "=" << encode(param.second);
            first = false;
        }
    }

    return oss.str();
}

// net::request
net::request::request(const net::url& r)
    : req_url(r), method(net::http_method::HTTP_METHOD_NULL), headers{}
{
}

net::request::request(const std::string& url_string)
    : request(net::url(url_string))
{
}

net::request::request(const net::url& r, net::http_method m)
    : req_url(r), method(m), headers{}
{
}

net::request::request(const std::string& url_string, net::http_method m)
    : request(net::url(url_string), m)
{
}

net::request::request(const net::url& r, net::http_method m,
                      const std::unordered_map<std::string, std::string>& h)
    : req_url(r), method(m), headers(h)
{
}

net::request::request(const std::string& url_string, net::http_method m,
                      const std::unordered_map<std::string, std::string>& h)
    : request(net::url(url_string), m, h)
{
}

net::request::request(const net::url& r, net::http_method m,
                      const std::unordered_map<std::string, std::string>& h,
                      const nlohmann::json& json_data)
    : req_url(r), method(m), headers(h)
{
    set_json(json_data);
}

net::request::request(const std::string& url_string, net::http_method m,
                      const std::unordered_map<std::string, std::string>& h,
                      const nlohmann::json& json_data)
    : request(net::url(url_string), m, h, json_data)
{
}

void net::request::subscribe(net::write_callback callback, void* userp)
{
    subscriptions.emplace_back(callback, userp, false);
}

void net::request::set_string(const std::string& text_data)
{
    data.assign(text_data.begin(), text_data.end());
}

void net::request::set_data(const std::vector<uint8_t>& binary_data)
{
    data = binary_data;
    set_default_content_type(headers, "application/octet-stream");
}

void net::request::set_json(const nlohmann::json& json_data)
{
    std::string json_str = json_data.dump();
    data.assign(json_str.begin(), json_str.end());
    set_default_content_type(headers, "application/json");
}

net::response net::request::send()
{
    net::response response;
    net::client   client;
    return client.send(*this);
}
