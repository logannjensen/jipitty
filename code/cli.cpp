#include "cli.h"
#include <stdexcept>

namespace cli
{
static std::vector<std::string> g_command_completions;
char** cli_completion(const char* text, int start, int end);
void   prompt::set_command_completions(const std::vector<std::string>& commands)
{
    g_command_completions            = commands;
    rl_attempted_completion_function = &cli_completion;
}

char* cli_command_generator(const char* text, int state)
{
    static size_t list_index;
    static size_t len;
    if (state == 0)
    {
        list_index = 0;
        len        = strlen(text);
    }
    if (g_command_completions.empty())
        return nullptr;
    const auto& cmds = g_command_completions;
    while (list_index < cmds.size())
    {
        const std::string& cmd = cmds[list_index++];
        if (cmd.compare(0, len, text, len) == 0)
        {
            return strdup(cmd.c_str());
        }
    }
    return nullptr;
}

char** cli_completion(const char* text, int start, int)
{
    if (!start)
        return rl_completion_matches(text, cli_command_generator);
    return nullptr;
}

std::string format_code(format fmt)
{
    switch (fmt)
    {
    case format::MAGENTA:
        return "\033[0;95m";
    case format::GREY:
        return "\033[0;37m";
    case format::YELLOW:
        return "\033[0;33m";
    case format::GREEN:
        return "\033[0;32m";
    case format::RED:
        return "\033[0;31m";
    case format::BLUE:
        return "\033[0;34m";
    case format::CYAN:
        return "\033[0;36m";
    case format::BOLD:
        return "\033[1m";
    case format::ITALIC:
        return "\033[4m";
    case format::RESET:
        return "\033[0m";
    default:
        return "\033[0m";
    }
}

std::string set_prompt(const std::string& text, format fmt)
{
    return "\001" + format_code(fmt) + "\002" + text + "\001" +
           format_code(format::RESET) + "\002";
}

std::string set_format(const std::string& text, format fmt)
{
    return format_code(fmt) + text + format_code(format::RESET);
}

prompt::prompt()
    : error_flags(0), escape_mode(false), keep_alive(true),
      args_allocated_(false), arg_index_(0)
{
}

prompt::~prompt()
{
    if (args_allocated_)
        wordfree(&args_);
}

std::string prompt::get_error() const
{
    switch (error_flags)
    {
    case error::BADCHAR:
        return "Unquoted special character found in input";
    case error::BADVAL:
        return "Undefined shell variable found in input";
    case error::SUB:
        return "Command substitution found in input";
    case error::SYNTAX:
        return "Shell syntax error_flags in input";
    default:
        return "Multiple errors";
    }
}

std::string prompt::read_line(const std::string& prompt_text)
{
    char* line = readline(prompt_text.c_str());
    if (line)
    {
        input_ = line;
        free(line);
    }
    else
    {
        keep_alive = false;
    }

    if (!input_.empty())
        add_history(input_.c_str());

    return input_;
}

std::string prompt::read_para(const std::string& first_prompt_text,
                              const std::string& prompt_text,
                              char               new_line_char)

{
    bool print_prompt = true;
    bool read_more    = false;
    input_            = "";
    do
    {
        std::string next_line;
        read_more  = false;
        char* line = readline(print_prompt ? first_prompt_text.c_str()
                                           : prompt_text.c_str());
        if (line)
        {
            next_line = line;
            free(line);
            if (!escape_mode && next_line.size() &&
                next_line.at(next_line.size() - 1) == new_line_char)
            {
                next_line.back() = '\n';
                read_more        = true;
                print_prompt     = false;
            }
            input_.append(next_line);
        }
        else
        {
            keep_alive = false;
            break;
        }
    } while (read_more);

    if (!input_.empty())
        add_history(input_.c_str());

    return input_;
}

std::string prompt::get_next_arg()
{
    std::string next_arg_string = "";
    if (!args_allocated_)
        throw std::runtime_error("prompt::get_next_arg called before parse");
    if (args_allocated_)
    {
        if (arg_index_ < args_.we_wordc)
            next_arg_string = args_.we_wordv[arg_index_++];
    }
    return next_arg_string;
}

size_t prompt::get_arg_count() { return args_.we_wordc; }

int prompt::parse()
{
    if (args_allocated_)
        wordfree(&args_);
    int      word_exp_err = wordexp(input_.c_str(), &args_, 0);
    uint32_t error_code   = 0;
    switch (word_exp_err)
    {
    case WRDE_NOSPACE:
        throw std::bad_alloc();
        break;
    case WRDE_BADCHAR:
        error_code = error::BADCHAR;
        break;
    case WRDE_BADVAL:
        error_code = error::BADVAL;
        break;
    case WRDE_CMDSUB:
        error_code = error::SUB;
        break;
    case WRDE_SYNTAX:
        error_code = error::SYNTAX;
        break;
    default:
        arg_index_      = 0;
        args_allocated_ = true;
        break;
    }

    if (word_exp_err != 0)
        args_allocated_ = false;

    error_flags |= error_code;
    return error_code ? -1 : args_.we_wordc;
}
} // namespace cli
