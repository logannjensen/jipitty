#include <unistd.h>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <stdexcept>
#include "cli.h"
#include "net.h"

using namespace nlohmann;
struct message
{
    std::string user;
    std::string assistant;
};

struct message_sse_dechunker : net::sse_dechunker
{
    std::string message;
    bool        unexpected_response = false;
    bool        done                = false;
};

namespace defaults
{
constexpr char    COMMAND_SYMBOL    = ':';
const std::string BASE_URL          = "https://api.openai.com";
constexpr float   TEMPERATURE       = 0.0f;
constexpr float   PRESENCE_PENALTY  = 0.0f;
constexpr float   FREQUENCY_PENALTY = 0.0f;
constexpr int     MAX_TOKENS        = 0;
const std::string SYSTEM_PROMPT     = "";
const std::string MODEL             = "gpt-4.1";
const std::string API_KEY_ENV       = "OPENAI_API_KEY";
} // namespace defaults

class chat_config
{
public:
    chat_config()
        : command_symbol(defaults::COMMAND_SYMBOL),
          base_url(defaults::BASE_URL), temperature(defaults::TEMPERATURE),
          presence(defaults::PRESENCE_PENALTY),
          frequency(defaults::FREQUENCY_PENALTY),
          max_tokens(defaults::MAX_TOKENS), system(defaults::SYSTEM_PROMPT),
          model(defaults::MODEL)
    {
        char* key_ptr = std::getenv(defaults::API_KEY_ENV.c_str());
        api_key       = key_ptr ? key_ptr : "";
    }

    std::string api_key;
    std::string import_chat_file_name;
    std::string input_file_name;
    std::string export_chat_file_name;
    char        command_symbol;
    net::url    base_url;

    float       temperature;
    float       presence;
    float       frequency;
    int         max_tokens;
    std::string system;
    std::string model;

    void reset()
    {
        temperature = defaults::TEMPERATURE;
        presence    = defaults::PRESENCE_PENALTY;
        frequency   = defaults::FREQUENCY_PENALTY;
        max_tokens  = defaults::MAX_TOKENS;
        system.clear();
        model = defaults::MODEL;
    }

    void import(json j)
    {
        if (j.is_object())
        {
            if (j["temperature"].is_number())
                temperature = j["temperature"].get<float>();

            if (j["presence_penalty"].is_number())
                presence = j["presence_penalty"].get<float>();

            if (j["frequency_penalty"].is_number())
                frequency = j["frequency_penalty"].get<float>();

            if (j["max_tokens"].is_number())
                max_tokens = j["max_tokens"].get<int>();

            if (j["messages"].is_array() && j["messages"].size() >= 1 &&
                j["messages"][0]["role"].is_string() &&
                j["messages"][0]["role"].get<std::string>() == "system" &&
                j["messages"][0]["content"].is_string())
            {
                system = j["messages"][0]["content"].get<std::string>();
            }

            if (j["model"].is_string())
                model = j["model"].get<std::string>();
        }
    }

    static std::string get_shell_doc() { return "[input-file]"; };
    static std::string get_shell_title()
    {
        return "jipitty -- An OpenAI Large Language Model CLI, written in C++";
    }

    static std::vector<argp_option> get_shell_options()
    {
        return {
            {"apikey", 'a', "STRING", 0,
             "Your API key that was created on the OpenAI website", 0},
            {"import", 'i', "FILE", 0,
             "Load a JSON file containing a previous conversation", 0},
            {"export", 'o', "FILE", 0,
             "Save a JSON file containing the conversation", 0},
            {"command", 'c', "CHAR", 0, "Character to prefix runtime commands",
             0},
            {"temperature", 't', "NUMBER", 0,
             "(0 - 2) Higher for less predictable responses", 0},
            {"presence", 'p', "NUMBER", 0,
             "(-2 - 2) Penalize tokens by presence", 0},
            {"frequency", 'f', "NUMBER", 0,
             "(-2 - 2) Penalize tokens by frequency", 0},
            {"max_tokens", 'n', "INTEGER", 0, "Maximum tokens to output", 0},
            {"system", 's', "STRING", 0,
             "Set system prompt for this conversation", 0},
            {"model", 'm', "STRING", 0,
             "Set the name of the language model to use", 0},
            {"url", 'u', "URL", 0, "OpenAI API base url", 0}};
    };

    static int from_shell_arg(int key, char* arg, struct argp_state* state,
                              chat_config& cfg)
    {
        switch (key)
        {
        case 'a':
            cfg.api_key = arg;
            break;
        case 'i':
            cfg.import_chat_file_name = arg;
            break;
        case 'o':
            cfg.export_chat_file_name = arg;
            break;
        case 'c':
            if (arg && arg[0])
                cfg.command_symbol = arg[0];
            break;
        case 't':
            cfg.temperature = atof(arg);
            break;
        case 'p':
            cfg.presence = atof(arg);
            break;
        case 'f':
            cfg.frequency = atof(arg);
            break;
        case 'n':
            cfg.max_tokens = atoi(arg);
            break;
        case 's':
            cfg.system = arg;
            break;
        case 'm':
            cfg.model = arg;
            break;
        case 'u':
            cfg.base_url = net::url(arg);
            break;
        case ARGP_KEY_ARG:
            if (state->arg_num >= 1)
                argp_usage(state);
            cfg.input_file_name = arg;
            break;
        case ARGP_KEY_END:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
        }
        return 0;
    }
};

class chat_completion
{
public:
    chat_completion(){};
    std::vector<message> messages = {};
    size_t               import_messages(json j)
    {
        messages = {};
        if (j.is_object() && j["messages"].is_array())
        {
            bool    have_user    = false;
            message next_message = {};
            for (auto message_json : j["messages"])
            {
                if (message_json["role"].is_string() &&
                    message_json["content"].is_string())
                {
                    if (message_json["role"].get<std::string>() == "user")
                    {
                        next_message.user =
                            message_json["content"].get<std::string>();
                        have_user = true;
                    }
                    else if (have_user &&
                             message_json["role"].get<std::string>() ==
                                 "assistant")
                    {
                        next_message.assistant =
                            message_json["content"].get<std::string>();
                        messages.push_back(next_message);
                        next_message = {};
                        have_user    = false;
                    }
                }
            }
        }
        return messages.size();
    }

    json create_request(chat_config& cfg)
    {
        json request_object           = json::object();
        request_object["model"]       = cfg.model;
        request_object["stream"]      = true;
        request_object["temperature"] = cfg.temperature;
        if (cfg.max_tokens)
            request_object["max_tokens"] = cfg.max_tokens;
        request_object["presence_penalty"]  = cfg.presence;
        request_object["frequency_penalty"] = cfg.frequency;
        request_object["messages"]          = json::array();
        if (!cfg.system.empty())
        {
            request_object["messages"].push_back(
                {{"role", "system"}, {"content", cfg.system}});
        }

        for (auto message : messages)
        {
            request_object["messages"].push_back(
                {{"role", "user"}, {"content", message.user}});
            request_object["messages"].push_back(
                {{"role", "assistant"}, {"content", message.assistant}});
        }
        return request_object;
    }
};
struct runtime_command
{
    std::string           title;
    std::string           doc;
    std::function<bool()> action;
};

class chat_cli
{
public:
    chat_cli(chat_config& c)
        : cfg(c), completion(), client(), input(), prompt(), input_file(),
          sse(), script_mode(!cfg.input_file_name.empty() ||
                             !isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO))
    {
        commands = {
            {"exit", "Exit the program.",
             [&]()
             {
                 prompt.keep_alive = false;
                 return false;
             }},
            {"file <file_path1> [<file_path2> ...]",
             "Upload one or more labeled files to OpenAI as a message.",
             [&]()
             {
                 bool send = false;
                 int  file_count =
                     std::max<size_t>(prompt.get_arg_count(), 1) - 1;
                 input.erase();
                 if (!file_count)
                 {
                     std::cerr << chat_cli::error_tag_string("Command Error")
                               << "No files given" << std::endl;
                 }

                 for (int file_index = 0; file_index < file_count; file_index++)
                 {
                     std::string   file_name = prompt.get_next_arg();
                     std::ifstream fs(file_name);
                     std::string   file_delimiter = "```";
                     if (fs.is_open())
                     {
                         input += (file_delimiter + file_name + "\n");
                         input +=
                             std::string((std::istreambuf_iterator<char>(fs)),
                                         std::istreambuf_iterator<char>());
                         input += (file_delimiter + "\n");
                         send = true;
                     }
                     else
                     {
                         std::cerr << file_error_tag_string(file_name)
                                   << std::endl;
                     }
                 }
                 return send;
             }},
            {"import <file_path>",
             "Import a json file to use as the current request object.",
             [&]()
             {
                 cfg.reset();
                 import_from_file(prompt.get_next_arg());
                 return false;
             }},
            {"export <file_path>",
             "Export the current request object to a file.",
             [&]()
             {
                 export_to_file(prompt.get_next_arg());
                 return false;
             }},
            {"system <prompt>", "Set the next system prompt.",
             [&]()
             {
                 std::string system_prompt = prompt.get_next_arg();
                 cfg.system                = system_prompt;
                 return false;
             }},
            {"prev [number]",
             "Move to previous exchange in conversation history.",
             [&]()
             {
                 int               amount = 0;
                 std::string       arg    = prompt.get_next_arg();
                 std::stringstream ss(arg);
                 ss >> amount;
                 amount = std::max(1, amount);
                 response_index =
                     (size_t)std::max(0, (int)response_index - amount);
                 print_messages();
                 std::cout << message_tag_string() << std::endl;
                 return false;
             }},
            {"next [number]", "Move to next exchange in conversation history.",
             [&]()
             {
                 int               amount = 0;
                 std::string       arg    = prompt.get_next_arg();
                 std::stringstream ss(arg);
                 ss >> amount;
                 amount = std::max(1, amount);
                 response_index =
                     (size_t)std::min((int)completion.messages.size(),
                                      (int)response_index + amount);
                 print_messages();
                 std::cout << message_tag_string() << std::endl;
                 return false;
             }},
            {"reset", "Reset to the default request object.",
             [&]()
             {
                 cfg.reset();
                 completion.messages = {};
                 return false;
             }},
            {"temperature <number>",
             "(0 - 2) Higher for less predictable responses.",
             [&]()
             {
                 float             num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.temperature = num;
                 return false;
             }},
            {"presence <number>", "(-2 - 2) Penalize tokens by presence.",
             [&]()
             {
                 float             num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.presence = num;
                 return false;
             }},
            {"frequency <number>", "(-2 - 2) Penalize tokens by frequency.",
             [&]()
             {
                 float             num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.frequency = num;
                 return false;
             }},
            {"maxtokens <number>", "Maximum number of tokens to output.",
             [&]()
             {
                 int               num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.max_tokens = num;
                 return false;
             }},
            {"model <name>", "Set the name of the language model to use.",
             [&]()
             {
                 std::string model = prompt.get_next_arg();
                 if (!model.empty())
                     cfg.model = model;
                 return false;
             }},
            {"url <url>", "OpenAI API base url.",
             [&]()
             {
                 std::string url = prompt.get_next_arg();
                 if (!url.empty())
                     cfg.base_url = net::url(url);
                 return false;
             }},
            {"print", "Re-print the entire conversation.",
             [&]()
             {
                 print_messages();
                 return false;
             }},
            {"help", "Show this message.",
             [&]()
             {
                 print_commands();
                 return false;
             }},
            {"options", "Show this message.",
             [&]()
             {
                 print_commands();
                 return false;
             }}};
    }

    chat_config                  cfg;
    chat_completion              completion;
    net::client                  client;
    std::string                  input;
    cli::prompt                  prompt;
    std::ifstream                input_file;
    message_sse_dechunker        sse;
    bool                         script_mode;
    size_t                       response_index = 0;
    std::vector<runtime_command> commands;

    static std::string user_tag_string()
    {
        return "[" + cli::set_prompt("User", cli::format::GREEN) + "] ";
    }

    static std::string bot_tag_string()
    {
        return "[" + cli::set_prompt("Bot", cli::format::CYAN) + "] ";
    }
    static std::string error_tag_string(std::string name)
    {
        return "[" + cli::set_format(name, cli::format::RED) + "] ";
    }

    static std::string file_error_tag_string(std::string file_name)
    {
        if (file_name.empty())
            return chat_cli::error_tag_string("File Error") +
                   "File name required";
        else
            return chat_cli::error_tag_string("File Error") +
                   "Failed to open file '" + file_name + '\'';
    }

    std::string message_tag_string()
    {
        std::ostringstream oss;
        oss << "Message " << response_index << " out of "
            << completion.messages.size();
        return "[" + cli::set_format(oss.str(), cli::format::YELLOW) + "] ";
    }

    int command_loop()
    {
        if (cfg.api_key.empty())
        {
            std::cerr << error_tag_string("Api Key Required") << std::endl;
            std::cerr << "Please provide an api key." << std::endl;
            std::cerr << "Alternatively export it as the environment variable "
                         "'"
                      << defaults::API_KEY_ENV << "'." << std::endl;
            std::cerr << "For ChatGPT, create an api key at: "
                         "https://platform.openai.com/account/api-keys"
                      << std::endl;
            return -1;
        }

        if (!script_mode)
        {
            std::cout << "[" << cli::set_format("Chat CLI", cli::format::YELLOW)
                      << "] Enter " << cfg.command_symbol << "help for commands"
                      << std::endl;
        }

        if (!cfg.input_file_name.empty())
        {
            input_file = std::ifstream(cfg.input_file_name);
            if (!input_file.is_open())
            {
                std::cerr << file_error_tag_string(cfg.input_file_name)
                          << std::endl;
                return -1;
            }
        }

        if (!cfg.import_chat_file_name.empty())
            import_from_file(cfg.import_chat_file_name);

        client.default_headers["Authorization"] = "Bearer " + cfg.api_key;
        client.subscribe(net::sse_dechunker_callback, &sse);
        do
        {
            input          = {};
            bool send_chat = true;
            if (!script_mode)
            {
                input = prompt.read_para(chat_cli::user_tag_string());
                if (!input.empty())
                {
                    if (input[0] == cfg.command_symbol)
                        send_chat = process_commands();
                }
            }
            else
            {
                send_chat = process_input_stream(
                    input_file.is_open() ? input_file : std::cin);
            }

            if (send_chat)
            {
                if (completion.messages.size() > response_index)
                    completion.messages.resize(response_index);
                json request_object = completion.create_request(cfg);
                json next_message   = {{"role", "user"}, {"content", input}};
                request_object["messages"].push_back(next_message);

                sse          = message_sse_dechunker();
                sse.callback = [&](const std::string&, const std::string& data)
                {
                    if (!data.empty())
                    {
                        try
                        {
                            nlohmann::json content = nlohmann::json::parse(
                                data)["choices"][0]["delta"]["content"];
                            if (content.is_string())
                            {
                                std::string chunk_string =
                                    content.get<std::string>();
                                if (!sse.started)
                                {
                                    sse.started = true;
                                    std::cout << chat_cli::bot_tag_string();
                                }
                                std::cout << chunk_string;
                                std::cout.flush();
                                sse.message.append(chunk_string);
                            }
                        }
                        catch (const std::exception& e)
                        {
                            if (data.find("[DONE]") != std::string::npos)
                                sse.done = true;
                        }
                    }
                };

                if (script_mode)
                    sse.started = true;
                net::response response = client.send(
                    {cfg.base_url.to_string() + "/v1/chat/completions",
                     net::http_method::POST,
                     {},
                     request_object});

                if (response.curl_code != CURLE_OK)
                {
                    std::cerr << chat_cli::error_tag_string("Network Error")
                              << curl_easy_strerror(response.curl_code);
                }
                else if (response.response_code != 200)
                {
                    try
                    {
                        json response_json = json::parse(response.to_string());
                        std::string api_error_message =
                            response_json["error"]["message"]
                                .get<std::string>();
                        std::cerr << chat_cli::error_tag_string("API Error")
                                  << api_error_message;
                    }
                    catch (const std::exception& e)
                    {
                        std::cerr << chat_cli::error_tag_string("HTTP Error")
                                  << response.status_line;
                    }
                }
                else if (sse.unexpected_response)
                {
                    throw std::runtime_error("Unexpected server response");
                }
                else
                {
                    completion.messages.push_back({input, sse.message});
                    response_index++;
                }
                std::cout << std::endl;
            }
            if (!cfg.export_chat_file_name.empty())
                export_to_file(cfg.export_chat_file_name);
        } while (prompt.keep_alive);
        return 0;
    }

    bool process_input_stream(std::istream& stream)
    {
        char c;
        bool send_chat = true;
        input.erase();
        do
        {
            stream.get(c);
            if (stream.eof())
            {
                prompt.keep_alive = false;
                send_chat         = !input.empty();
            }
            else
            {
                input.push_back(c);
            }
        } while (prompt.keep_alive);
        return send_chat;
    }

    bool process_commands()
    {
        bool send_chat = false;
        bool cmd_err   = true;
        if (prompt.parse() > 0)
        {
            std::string command = prompt.get_next_arg().substr(1);
            if (!command.empty())
            {
                auto it = std::find_if(
                    commands.begin(), commands.end(),
                    [&](const runtime_command& cmd)
                    { return cmd.title.substr(0, command.size()) == command; });
                if (it != commands.end())
                {
                    send_chat = it->action();
                    cmd_err   = false;
                }
            }
        }

        if (cmd_err)
        {
            std::cerr << chat_cli::error_tag_string("Command Error")
                      << "Invalid command, try " << cfg.command_symbol << "help"
                      << std::endl;
        }

        return send_chat;
    }

    void print_commands() const
    {
        std::string sym(1, cfg.command_symbol);
        std::cout << cli::set_format("jipitty", cli::format::BOLD)
                  << ", An OpenAI Large Language Model CLI, written in C++\n";
        std::cout << "All commands have the prefix '" << sym << "'\n";
        std::cout << "Anything else is uploaded to OpenAI as a message.\n\n";
        for (const auto& cmd : commands)
        {
            std::string cmd_name  = cmd.title;
            auto        space_pos = cmd_name.find(' ');
            if (space_pos != std::string::npos)
                cmd_name = cmd_name.substr(0, space_pos);
            std::cout << cli::set_format(sym + cmd.title, cli::format::BOLD)
                      << "\n";
            std::cout << "\t" << cmd.doc << "\n\n";
        }
    }

    void export_to_file(const std::string& file_name)
    {
        json          export_json = completion.create_request(cfg);
        std::ofstream fs(file_name);
        if (!fs.is_open())
        {
            std::cerr << chat_cli::error_tag_string("File Error")
                      << "Failed to open file '" << file_name
                      << "' for writing." << std::endl;
            return;
        }
        try
        {
            fs << export_json.dump();
            if (!fs)
            {
                throw std::runtime_error("Write failed");
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << chat_cli::error_tag_string("File Error")
                      << "Failed to export conversation to '" << file_name
                      << "', " << e.what() << std::endl;
        }
    }

    void print_messages() const
    {
        for (size_t message_index = 0;
             message_index < completion.messages.size() &&
             message_index < response_index;
             message_index++)
        {
            message msg = completion.messages[message_index];
            std::cout << user_tag_string() << msg.user << std::endl;
            std::cout << bot_tag_string() << msg.assistant << std::endl;
        }
    }

    void import_from_file(const std::string& file_name)
    {
        std::ifstream fs = std::ifstream(file_name);
        if (fs.is_open())
        {
            try
            {
                json j = json::parse(
                    std::string((std::istreambuf_iterator<char>(fs)),
                                std::istreambuf_iterator<char>()));
                cfg.import(j);
                response_index = completion.import_messages(j);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << chat_cli::error_tag_string("File Error")
                    << "Couldn't parse '" << file_name
                    << "' into completions request body, see "
                       "https://platform.openai.com/docs/api-reference/chat"
                    << std::endl;
            }
        }
        else
        {
            std::cerr << file_error_tag_string(file_name) << std::endl;
        }
    }
};

int main(int argc, char** argv)
{
    try
    {
        cli::shell_args<chat_config> parsed_args(
            argc, argv, chat_config::get_shell_options(),
            chat_config::from_shell_arg, chat_config::get_shell_doc(),
            chat_config::get_shell_title());
        chat_config cfg = parsed_args.get_arguments();
        chat_cli    cli(cfg);
        return cli.command_loop();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }
}
