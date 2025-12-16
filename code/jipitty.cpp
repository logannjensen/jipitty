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
constexpr char    COMMAND_SYMBOL       = ':';
const std::string BASE_URL             = "https://api.openai.com";
const std::string COMPLETIONS_ENDPOINT = "/v1/chat/completions";
constexpr float   TEMPERATURE          = 0.0f;
constexpr float   PRESENCE_PENALTY     = 0.0f;
constexpr float   FREQUENCY_PENALTY    = 0.0f;
constexpr float   TOP_P                = 0.0f;
constexpr int     MAX_TOKENS           = 0;
const std::string SYSTEM_PROMPT        = "";
const std::string MODEL                = "gpt-4.1";
const std::string API_KEY_ENV          = "OPENAI_API_KEY";
const std::string FILE_DELIMITER       = std::string(3, char(96));
const std::string VERSION              = "0.5";
const std::string NAME                 = "jipitty";
const std::string DESCRIPTION =
    "An OpenAI Large Language Model CLI, written in C++";
const int         TERMINAL_HEIGHT = 24;
const std::string PAGER           = "less";
} // namespace defaults

class chat_config
{
public:
    chat_config()
        : command_symbol(defaults::COMMAND_SYMBOL),
          base_url(defaults::BASE_URL), temperature(defaults::TEMPERATURE),
          top_p(defaults::TOP_P), presence(defaults::PRESENCE_PENALTY),
          frequency(defaults::FREQUENCY_PENALTY),
          max_tokens(defaults::MAX_TOKENS), system(defaults::SYSTEM_PROMPT),
          model(defaults::MODEL), pager(defaults::PAGER), show_version(false),
          extract_code(false), extract_language_ident_filters{}
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

    float                    temperature;
    float                    top_p;
    float                    presence;
    float                    frequency;
    int                      max_tokens;
    std::string              system;
    std::string              model;
    std::string              pager;
    bool                     show_version;
    bool                     extract_code;
    std::vector<std::string> extract_language_ident_filters;

    void reset()
    {
        temperature = defaults::TEMPERATURE;
        top_p       = defaults::TOP_P;

        presence  = defaults::PRESENCE_PENALTY;
        frequency = defaults::FREQUENCY_PENALTY;

        max_tokens = defaults::MAX_TOKENS;
        system.clear();
        model = defaults::MODEL;
    }

    void import(json j)
    {
        if (j.is_object())
        {
            temperature = defaults::TEMPERATURE;
            if (j["temperature"].is_number())
                temperature = j["temperature"].get<float>();

            top_p = defaults::TOP_P;
            if (j["top_p"].is_number())
                top_p = j["top_p"].get<float>();

            presence = defaults::PRESENCE_PENALTY;

            if (j["presence_penalty"].is_number())
                presence = j["presence_penalty"].get<float>();

            frequency = defaults::FREQUENCY_PENALTY;
            if (j["frequency_penalty"].is_number())
                frequency = j["frequency_penalty"].get<float>();

            max_tokens = defaults::MAX_TOKENS;
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
        return defaults::NAME + " -- " + defaults::DESCRIPTION;
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
            {"top_p", -1, "NUMBER", 0,
             "(0 - 1) Nucleus Sampling, alternative to temperature", 0},
            {"presence", 'p', "NUMBER", 0,
             "(-2 - 2) Penalize tokens by presence", 0},
            {"frequency", 'f', "NUMBER", 0,
             "(-2 - 2) Penalize tokens by frequency", 0},
            {"max_tokens", 'n', "INTEGER", 0, "Maximum tokens to output", 0},
            {"system", 's', "STRING", 0,
             "Set system prompt for this conversation", 0},
            {"model", 'm', "STRING", 0,
             "Set the name of the language model to use", 0},
            {"extract", 'x', "STRING", OPTION_ARG_OPTIONAL,
             "Extract the last code block with language identifier STRING from "
             "the response or simply the last if STRING isn't provided",
             0},
            {"pager", 'P', "COMMAND", 0,
             "The pager command to use for long output (e.g., 'glow -p')", 0},
            {"url", 'u', "URL", 0, "OpenAI API base url", 0},
            {"version", 'v', 0, 0, "Show version", 0}};
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
        case 'P':
            cfg.pager = arg;
            break;
        case 'x':
        {
            cfg.extract_code = true;
            if (arg)
                cfg.extract_language_ident_filters.push_back(arg);
        }
        break;
        case 'u':
            cfg.base_url = net::url(arg);
            break;
        case 'v':
            cfg.show_version = true;
            break;
        case -1:
            cfg.top_p = atof(arg);
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
    chat_completion() {};
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
        json request_object      = json::object();
        request_object["model"]  = cfg.model;
        request_object["stream"] = true;

        if (cfg.temperature != defaults::TEMPERATURE)
            request_object["temperature"] = cfg.temperature;

        if (cfg.top_p != defaults::TOP_P)
            request_object["top_p"] = cfg.top_p;

        if (cfg.max_tokens != defaults::MAX_TOKENS)
            request_object["max_tokens"] = cfg.max_tokens;

        if (cfg.presence != defaults::PRESENCE_PENALTY)
            request_object["presence_penalty"] = cfg.presence;

        if (cfg.frequency != defaults::FREQUENCY_PENALTY)
            request_object["frequency_penalty"] = cfg.frequency;

        request_object["messages"] = json::array();
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
        : cfg(c), completion(), client(), input(), prompt_builder(),
          building_prompt(false), prompt(), input_file(), sse(),
          script_mode(!cfg.input_file_name.empty() || !isatty(STDOUT_FILENO) ||
                      !isatty(STDIN_FILENO))
    {
        commands = {
            {"exit", "Exit the program.",
             [&]()
             {
                 prompt.keep_alive = false;
                 return false;
             }},

            {"prompt",
             "Start buffering a prompt with input and commands, or print the "
             "current prompt if already buffering.",
             [&]()
             {
                 if (!building_prompt)
                 {
                     building_prompt = true;
                 }
                 else if (less_output_with_fallback(prompt_builder.str()))
                 {
                     std::cout << cli::set_format(prompt_builder.str(),
                                                  cli::format::YELLOW)
                               << std::endl;
                 }
                 return false;
             }},

            {"send", "Send the current prompt.",
             [&]()
             {
                 if (building_prompt && !prompt_builder.str().empty())
                 {
                     building_prompt = false;
                     input.str(prompt_builder.str());
                     prompt_builder.str("");
                     return true;
                 }
                 else
                 {
                     std::cerr << chat_cli::error_tag_string("Command Error")
                               << "No prompt to send" << std::endl;
                     return false;
                 }
             }},

            {"file <file_path1> [<file_path2> ...]",
             "Upload one or more labeled files to OpenAI or "
             "append to current prompt.",
             [&]() { return add_files_to_prompt(); }},

            {"line <file_path> <number> [[+|-]number]",
             "Add to prompt lines from the first argument, starting from the "
             "second argument either to the third, or to plus/minus the third.",
             [&]()
             {
                 std::string              file_name = prompt.get_next_arg();
                 std::ifstream            fs(file_name);
                 std::stringstream        file_data_buffer;
                 std::string              file_data;
                 std::vector<std::string> lines = {};
                 if (fs.is_open())
                 {
                     file_data_buffer
                         << std::string((std::istreambuf_iterator<char>(fs)),
                                        std::istreambuf_iterator<char>());
                     lines = split_lines(file_data_buffer.str());
                     if (lines.empty())
                     {
                         std::cerr
                             << chat_cli::error_tag_string("Command Error")
                             << "File '" << file_name << "' was empty"
                             << std::endl;
                         return false;
                     }
                 }
                 else
                 {
                     std::cerr << file_error_tag_string(file_name) << std::endl;
                     return false;
                 }

                 int start_line = 0;
                 try
                 {
                     start_line = std::stoi(prompt.get_next_arg());
                 }
                 catch (const std::exception& e)
                 {
                     std::cerr << chat_cli::error_tag_string("Command Error")
                               << "Expected a line number in second argument"
                               << std::endl;
                     return false;
                 }
                 int         end_line  = start_line;
                 std::string third_arg = prompt.get_next_arg();
                 if (!third_arg.empty())
                 {
                     int direction = 0;
                     if (third_arg[0] == '+')
                         direction = 1;
                     else if (third_arg[0] == '-')
                         direction = -1;
                     if (direction && third_arg.size() > 1)
                     {
                         int               amount = 0;
                         std::stringstream ss(third_arg.substr(1));
                         if (ss >> amount)
                             end_line += (direction * amount);
                     }
                     else
                     {
                         std::stringstream ss(third_arg);
                         ss >> end_line;
                     }
                 }

                 int last_line = lines.size();
                 start_line =
                     std::max(0, std::min(start_line - 1, last_line - 1));
                 end_line = std::max(0, std::min(end_line - 1, last_line - 1));

                 if (end_line < start_line)
                     std::swap(end_line, start_line);

                 input.str("");
                 input << std::endl << defaults::FILE_DELIMITER << file_name << ":"
                       << start_line + 1;
                 if (end_line > start_line)
                     input << "," << end_line + 1;
                 input << std::endl;
                 do
                 {
                     input << lines[start_line++] << std::endl;
                 } while (start_line <= end_line);
                 input << defaults::FILE_DELIMITER << std::endl;
                 return true;
             }},

            {"shell <command>",
             "Execute a shell command and send it with standard output to "
             "OpenAI or "
             "append to the current prompt. Use at your own peril.",
             [&]()
             {
                 std::string shell_cmd = prompt.get_next_arg();
                 try
                 {
                     int rc = pipe_from_shell(shell_cmd);
                     if (rc)
                     {
                         std::cerr
                             << chat_cli::error_tag_string("Command Error")
                             << "Shell command exited with code " << rc
                             << std::endl;
                     }
                 }
                 catch (const std::exception& e)
                 {
                     std::cerr << chat_cli::error_tag_string("Command Error")
                               << e.what() << std::endl;
                     return false;
                 }
                 return true;
             }},

            {"clear", "Clear the current prompt and stop buffering.",
             [&]()
             {
                 prompt_builder.str("");
                 building_prompt = false;
                 return false;
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
                 if (prompt.get_arg_count() > 1)
                     cfg.system = system_prompt;
                 std::cout << config_tag_string("System Prompt") << cfg.system
                           << std::endl;
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
                 std::cout << config_tag_string(
                                  "Conversation and parameters reset")
                           << std::endl;
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
                 std::cout << config_tag_string("Temperature");
                 if (cfg.temperature == defaults::TEMPERATURE)
                     std::cout << "API default" << std::endl;
                 else
                     std::cout << cfg.temperature << std::endl;
                 return false;
             }},
            {"top_p <number>",
             "(0 - 1) Nucleus Sampling, alternative to temperature.",
             [&]()
             {
                 float             num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.top_p = num;
                 std::cout << config_tag_string("Top P");
                 if (cfg.top_p == defaults::TOP_P)
                     std::cout << "API default" << std::endl;
                 else
                     std::cout << cfg.top_p << std::endl;
                 return false;
             }},
            {"presence <number>", "(-2 - 2) Penalize tokens by presence.",
             [&]()
             {
                 float             num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.presence = num;
                 std::cout << config_tag_string("Presence Penalty");
                 if (cfg.presence == defaults::PRESENCE_PENALTY)
                     std::cout << "API default" << std::endl;
                 else
                     std::cout << cfg.presence << std::endl;
                 return false;
             }},
            {"frequency <number>", "(-2 - 2) Penalize tokens by frequency.",
             [&]()
             {
                 float             num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.frequency = num;
                 std::cout << config_tag_string("Frequency Penalty");
                 if (cfg.frequency == defaults::FREQUENCY_PENALTY)
                     std::cout << "API default" << std::endl;
                 else
                     std::cout << cfg.frequency << std::endl;
                 return false;
             }},
            {"maxtokens <number>", "Maximum number of tokens to output.",
             [&]()
             {
                 int               num;
                 std::stringstream ss(prompt.get_next_arg());
                 if (ss >> num)
                     cfg.max_tokens = num;
                 std::cout << config_tag_string("Maximum Tokens");
                 if (cfg.max_tokens == defaults::MAX_TOKENS)
                     std::cout << "API default" << std::endl;
                 else
                     std::cout << cfg.max_tokens << std::endl;
                 return false;
             }},
            {"model <name>", "Set the name of the language model to use.",
             [&]()
             {
                 std::string model = prompt.get_next_arg();
                 if (!model.empty())
                     cfg.model = model;
                 std::cout << config_tag_string("Model") << cfg.model
                           << std::endl;
                 return false;
             }},
            {"pager <command>", "Set the pager command to use for long output.",
             [&]()
             {
                 std::string pager_cmd = prompt.get_next_arg();
                 if (!pager_cmd.empty())
                     cfg.pager = pager_cmd;
                 std::cout << config_tag_string("Pager") << cfg.pager
                           << std::endl;
                 return false;
             }},
            {"url <url>", "OpenAI API base url.",
             [&]()
             {
                 std::string url = prompt.get_next_arg();
                 if (!url.empty())
                     cfg.base_url = net::url(url);
                 std::cout << config_tag_string("API Base URL")
                           << cfg.base_url.to_string() << std::endl;
                 return false;
             }},
            {"print", "Re-print the entire conversation.",
             [&]()
             {
                 print_messages();
                 return false;
             }},
            {"less",
             "View the currently selected exchange in page reader if "
             "available.",
             [&]()
             {
                 int target_index = (int)response_index - 1;
                 if (target_index >= 0)
                 {
                     message           msg = completion.messages[target_index];
                     std::stringstream ss("");
                     ss << "[User] " << msg.user << std::endl;
                     ss << "[Bot] " << msg.assistant << std::endl;

                     try
                     {
                         int rc = pipe_to_shell(ss.str(), cfg.pager);
                         if (rc)
                         {
                             throw std::runtime_error(
                                 "Pager command exited non-zero");
                         }
                     }
                     catch (const std::exception& e)
                     {
                         std::cerr
                             << chat_cli::error_tag_string("Command Error")
                             << "Failed to page with '" << cfg.pager << "'"
                             << std::endl;
                     }
                 }
                 else
                 {
                     std::cerr << chat_cli::error_tag_string("Command Error")
                               << "No messages to page" << std::endl;
                 }
                 return false;
             }},
            {"extract <command> [number]",
             "Extract the code block n places before last in the selected "
             "response and redirect it to a shell command like 'less', 'diff "
             "./my_file - ', 'xclip -selection clipboard', or 'cat > "
             "my_file'. ",
             [&]() { //
                 int target_index = (int)response_index - 1;
                 if (target_index >= 0)
                 {
                     message     msg       = completion.messages[target_index];
                     std::string shell_cmd = prompt.get_next_arg();
                     int         num       = 0;
                     if (prompt.get_arg_count() > 2)
                     {
                         std::string number_arg = prompt.get_next_arg();
                         try
                         {
                             num = std::stoi(number_arg);
                         }
                         catch (const std::exception& e)
                         {
                             std::cerr
                                 << chat_cli::error_tag_string("Command Error")
                                 << "Expected a number for second extract "
                                    "command argument"
                                 << std::endl;
                             return false;
                         }
                     }

                     std::string code =
                         extract_code_block(msg.assistant, {}, num);

                     if (!code.empty())
                     {
                         try
                         {
                             int rc = pipe_to_shell(code, shell_cmd);
                             if (rc)
                             {
                                 std::cerr << chat_cli::error_tag_string(
                                                  "Command Error")
                                           << "Shell command exited with code "
                                           << rc << std::endl;
                             }
                         }
                         catch (const std::exception& e)
                         {
                             std::cerr
                                 << chat_cli::error_tag_string("Command Error")
                                 << e.what() << std::endl;
                         }
                     }
                     else
                     {
                         std::cerr
                             << chat_cli::error_tag_string("Command Error")
                             << "No code found in selected message"
                             << std::endl;
                     }
                 }
                 else
                 {
                     std::cerr << chat_cli::error_tag_string("Command Error")
                               << "No message selected" << std::endl;
                 }
                 return false;
             }},
            {"help [first] [count]",
             "Show [count] help messages starting from [first].",
             [&]()
             {
                 int         first = 0, count = 0;
                 std::string arg1 = prompt.get_next_arg();
                 std::string arg2 = prompt.get_next_arg();
                 if (!arg1.empty())
                 {
                     std::stringstream ss(arg1);
                     if (!(ss >> first) || first < 0)
                         first = 0;
                 }
                 if (!arg2.empty())
                 {
                     std::stringstream ss(arg2);
                     if (!(ss >> count) || count < 0)
                         count = 0;
                 }
                 std::string help_string = print_commands(first, count).str();
                 if (less_output_with_fallback(help_string))
                 {
                     std::cout << help_string;
                 }
                 return false;
             }},

        };

        std::vector<std::string> cmd_completions{};
        for (auto cmd : commands)
        {
            std::string title_trimmed;
            size_t      first_space_pos = cmd.title.find(' ');
            if (first_space_pos != std::string::npos)
                title_trimmed = cmd.title.substr(0, first_space_pos);
            else
                title_trimmed = cmd.title;
            cmd_completions.push_back(cfg.command_symbol + title_trimmed);
        }
        prompt.set_command_completions(cmd_completions);
    }

    chat_config                  cfg;
    chat_completion              completion;
    net::client                  client;
    std::ostringstream           input;
    std::ostringstream           prompt_builder;
    bool                         building_prompt;
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
    static std::string config_tag_string(std::string name)
    {
        return "[" + cli::set_format(name, cli::format::YELLOW) + "] ";
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

    static std::string
    extract_code_block(const std::string&             content,
                       const std::vector<std::string> filters = {}, int n = 0)
    {
        n                        = std::max(0, n);
        const std::string& delim = defaults::FILE_DELIMITER;
        std::vector<std::pair<std::string, std::string>> blocks;
        size_t                                           pos = 0;
        while (true)
        {
            size_t start = content.find(delim, pos);
            if (start == std::string::npos)
                break;
            size_t ident_start = start + delim.size();
            size_t ident_end   = content.find('\n', ident_start);
            if (ident_end == std::string::npos)
                break;
            std::string language_ident = net::trim_whitespace(
                content.substr(ident_start, ident_end - ident_start));
            size_t code_start = ident_end + 1;
            size_t end        = content.find(delim, code_start);
            if (end == std::string::npos)
                break;
            std::string code = content.substr(code_start, end - code_start);
            blocks.emplace_back(language_ident, code);
            pos = end + delim.size();
        }

        if (filters.empty())
        {
            if (blocks.empty() || n >= (int)blocks.size())
                return "";
            int         back_index = blocks.size() - 1;
            const auto& code       = blocks[back_index - n].second;
            return net::trim_whitespace(code);
        }
        else
        {
            for (auto it = blocks.rbegin(); it != blocks.rend(); it++)
            {
                for (const auto& f : filters)
                {
                    if (net::trim_whitespace(it->first) ==
                        net::trim_whitespace(f))
                    {
                        return net::trim_whitespace(it->second);
                    }
                }
            }
        }
        return "";
    }

    bool add_files_to_prompt()
    {
        bool send       = false;
        int  file_count = std::max<int>((int)prompt.get_arg_count() - 1, 0);
        input.str("");
        input << std::endl;
        if (file_count == 0)
        {
            std::cerr << chat_cli::error_tag_string("Command Error")
                      << "No files given" << std::endl;
        }

        for (int file_index = 0; file_index < file_count; file_index++)
        {
            std::string   file_name = prompt.get_next_arg();
            std::ifstream fs(file_name);
            if (fs.is_open())
            {
                input << defaults::FILE_DELIMITER << file_name << std::endl;
                input << std::string((std::istreambuf_iterator<char>(fs)),
                                     std::istreambuf_iterator<char>());
                input << defaults::FILE_DELIMITER << std::endl;
                send = true;
            }
            else
            {
                std::cerr << file_error_tag_string(file_name) << std::endl;
            }
        }

        return send;
    }

    bool less_output_with_fallback(const std::string& output_str)
    {
        bool fallback = false;
        try
        {
            int line_count =
                std::count(output_str.begin(), output_str.end(), '\n');
            if (line_count > defaults::TERMINAL_HEIGHT)
                fallback =
                    pipe_to_shell(
                        cli::set_format(
                            "[Controls: q to exit, j/k to scroll, h for more]",
                            cli::format::BOLD) +
                            "\n\n" + output_str,
                        cfg.pager) != 0;
            else
                fallback = true;
        }
        catch (const std::exception& e)
        {
            fallback = true;
        }

        return fallback;
    }

    int pipe_to_shell(const std::string& text, const std::string& shell_cmd)
    {
        if (shell_cmd.empty())
            throw std::invalid_argument("No shell command provided");

        FILE* pipe = popen(shell_cmd.c_str(), "w");

        if (!pipe)
            throw std::runtime_error("Failed to run shell command");

        size_t      total_written = 0;
        size_t      to_write      = text.size();
        const char* data          = text.data();

        while (total_written < to_write)
        {
            size_t written =
                fwrite(data + total_written, 1, to_write - total_written, pipe);
            if (written == 0)
            {
                if (ferror(pipe))
                {
                    pclose(pipe);
                    throw std::runtime_error("Error writing to shell command");
                }
                break;
            }
            total_written += written;
        }

        return pclose(pipe);
    }

    int pipe_from_shell(const std::string& shell_cmd)
    {
        if (shell_cmd.empty())
            throw std::invalid_argument("No shell command provided");
        std::string output;
        FILE*       pipe = popen(shell_cmd.c_str(), "r");
        if (!pipe)
            throw std::runtime_error("Failed to run shell command");

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
            output += buffer;

        int rc = pclose(pipe);
        input.str("");
        input << defaults::FILE_DELIMITER << shell_cmd << std::endl
              << output << defaults::FILE_DELIMITER << std::endl;

        return rc;
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

        if (cfg.show_version)
        {
            std::cout << defaults::NAME << " v" << defaults::VERSION
                      << std::endl;
            return 0;
        }

        if (!script_mode)
        {
            cfg.extract_code = false;
            std::cout << "["
                      << cli::set_format(defaults::NAME + " v" +
                                             defaults::VERSION,
                                         cli::format::YELLOW)
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
        do
        {
            input.str("");
            bool send_chat = true;
            if (!script_mode)
            {
                input << prompt.read_para(
                    building_prompt ? ">" : chat_cli::user_tag_string());
                if (!input.str().empty())
                {
                    if (input.str()[0] == cfg.command_symbol)
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
                if (building_prompt)
                {
                    prompt_builder << input.str() << std::endl;
                }
                else
                {
                    if (completion.messages.size() > response_index)
                        completion.messages.resize(response_index);
                    json request_object = completion.create_request(cfg);
                    json next_message   = {{"role", "user"},
                                           {"content", input.str()}};

                    request_object["messages"].push_back(next_message);

                    sse = message_sse_dechunker();
                    sse.callback =
                        [&](const std::string&, const std::string& data)
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

#if 0
                    std::cout << cli::set_format(
                                     next_message["content"].get<std::string>(),
                                     cli::format::RED)
                              << std::endl;
#endif
                    if (cfg.extract_code)
                        request_object["stream"] = false;
                    net::url req_url(cfg.base_url);
                    if (req_url.path.empty() || req_url.path == "/")
                        req_url.path = defaults::COMPLETIONS_ENDPOINT;
                    net::request req = {
                        req_url, net::http_method::POST, {}, request_object};
                    if (!cfg.extract_code)
                        req.subscribe(net::sse_dechunker_callback, &sse);
                    net::response response = client.send(req);

#if 0
                    std::cout << cli::set_format(response.to_string(),
                                                 cli::format::MAGENTA)
                              << std::endl;
#endif

                    if (response.curl_code != CURLE_OK)
                    {
                        std::cerr << chat_cli::error_tag_string("Network Error")
                                  << curl_easy_strerror(response.curl_code);
                    }
                    else if (response.response_code != 200)
                    {
                        try
                        {
                            json response_json =
                                json::parse(response.to_string());
                            std::string api_error_message =
                                response_json["error"]["message"]
                                    .get<std::string>();
                            std::cerr << chat_cli::error_tag_string("API Error")
                                      << api_error_message;
                        }
                        catch (const std::exception& e)
                        {
                            std::cerr
                                << chat_cli::error_tag_string("HTTP Error")
                                << response.status_line << std::endl;
                            if (!response.body.empty())
                                std::cerr << response.to_string() << std::endl;
                        }
                    }
                    else if (sse.unexpected_response)
                    {
                        throw std::runtime_error("Unexpected server response");
                    }
                    else
                    {
                        completion.messages.push_back(
                            {input.str(), sse.message});
                        response_index++;

                        if (cfg.extract_code)
                        {
                            json response_json =
                                json::parse(response.to_string());

                            std::string content =
                                response_json["choices"][0]["message"]
                                             ["content"]
                                                 .get<std::string>();

                            std::string code_block = extract_code_block(
                                content, cfg.extract_language_ident_filters);
                            if (code_block.empty())
                                return -1;
                            std::cout << code_block;
                        }
                    }
                    std::cout << std::endl;
                }
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
        input.str("");
        do
        {
            stream.get(c);
            if (stream.eof())
            {
                prompt.keep_alive = false;
                send_chat         = !input.str().empty();
            }
            else
            {
                input << c;
            }
        } while (prompt.keep_alive);
        return send_chat;
    }

    bool process_commands()
    {
        bool send_chat   = false;
        bool invalid_cmd = true;
        if (prompt.parse() > 0)
        {
            std::string command = prompt.get_next_arg().substr(1);
            if (!command.empty())
            {
                std::vector<const runtime_command*> matches;
                for (const auto& cmd : commands)
                {
                    if (cmd.title.substr(0, command.size()) == command)
                        matches.push_back(&cmd);
                }

                if (matches.size() == 1)
                {
                    send_chat   = matches[0]->action();
                    invalid_cmd = false;
                }
                else if (matches.size() > 1)
                {
                    std::cerr << chat_cli::error_tag_string("Command Error")
                              << "Ambigous command '" << command << "' matches "
                              << matches.size() << std::endl;

                    invalid_cmd = false;
                }
            }
        }

        if (invalid_cmd)
        {
            std::cerr << chat_cli::error_tag_string("Command Error")
                      << "Invalid command, try " << cfg.command_symbol << "help"
                      << std::endl;
        }

        return send_chat;
    }

    std::stringstream print_commands(int first = 0, int count = 0) const
    {
        std::stringstream ss;
        std::string       sym(1, cfg.command_symbol);
        ss << cli::set_format(defaults::NAME, cli::format::BOLD) << ", "
           << defaults::DESCRIPTION << "\n";
        ss << "All commands have the prefix '" << sym << "'\n";
        ss << "Anything else is uploaded to OpenAI as a message.\n\n";

        int total = (int)commands.size();
        if (first < 0)
            first = 0;
        if (count < 0)
            count = 0;

        int last = (count == 0) ? total : std::min(total, first + count);

        for (int i = first; i < last; ++i)
        {
            const auto& cmd       = commands[i];
            std::string cmd_name  = cmd.title;
            auto        space_pos = cmd_name.find(' ');
            if (space_pos != std::string::npos)
                cmd_name = cmd_name.substr(0, space_pos);
            ss << cli::set_format(sym + cmd.title, cli::format::BOLD) << "\n";
            ss << "\t" << cmd.doc << "\n\n";
        }
        return ss;
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

    std::vector<std::string> split_lines(const std::string& multi_line)
    {
        std::vector<std::string> lines;
        size_t                   start = 0;
        size_t                   pos   = 0;

        while (pos <= multi_line.length())
        {
            if (pos == multi_line.length() || multi_line[pos] == '\n' ||
                multi_line[pos] == '\r')
            {
                lines.push_back(multi_line.substr(start, pos - start));
                start = pos + 1;
            }
            pos++;
        }

        return lines;
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
