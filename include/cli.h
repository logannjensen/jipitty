#ifndef LJ_CLI_H
#define LJ_CLI_H

#include <cstdint>
#include <wordexp.h>
#include <argp.h>
#include <vector>
#include <string>
#include <functional>
#include <cstdio>
#include <readline/readline.h>
#include <readline/history.h>

namespace cli
{
enum class format
{
	MAGENTA,
	GREY,
	YELLOW,
	GREEN,
	RED,
	BLUE,
	CYAN,
	BOLD,
	ITALIC,
	RESET,
	NO_COUNT_ON,
	NO_COUNT_OFF,
};

std::string format_code(format fmt);
std::string set_prompt(const std::string& text, format fmt);
std::string set_format(const std::string& text, format fmt);

class prompt
{
public:
	enum error
	{
		SUCCESS = 0,
		BADCHAR = 1,
		BADVAL	= 1 << 1,
		SUB		= 1 << 2,
		SYNTAX	= 1 << 3
	};

	prompt();
	~prompt();
	void		reset_error_flags() { error_flags = 0; }
	uint32_t	get_error_flags() const { return error_flags; }
	std::string get_error() const;
	std::string read_line(const std::string& prompt_text = ">");
	std::string read_para(const std::string& first_prompt_text = ">",
						  const std::string& prompt_text	   = ">",
						  char				 new_line_char	   = '\\');
	std::string get_next_arg();
	int			parse();
	void		set_prompt(const std::string&) {}
	uint32_t	error_flags;
	bool		keep_alive;

private:
	wordexp_t	args_;
	std::string input_;
	bool		args_allocated_;
	size_t		arg_index_;
};
template <typename T> class shell_args
{
public:
	using parse_function =
		std::function<error_t(int, char*, struct argp_state*, T&)>;

	shell_args(int argc, char** argv, const std::vector<argp_option>& options,
			   parse_function	  parse_func,
			   const std::string& args_doc = get_default_args_doc(),
			   const std::string& doc	   = get_default_doc())
		: arguments_(), parse_func_(parse_func)
	{
		std::vector<argp_option> options_copy = options;
		options_copy.push_back({});

		argp argp = {options_copy.data(),
					 &shell_args::parse_opt_wrapper,
					 args_doc.c_str(),
					 doc.c_str(),
					 nullptr,
					 nullptr,
					 nullptr};

		argp_parse(&argp, argc, argv, 0, nullptr, this);
	}

	T get_arguments() const { return arguments_; }

private:
	T			   arguments_;
	parse_function parse_func_;

	static error_t parse_opt_wrapper(int key, char* arg,
									 struct argp_state* state)
	{
		shell_args* self = (shell_args*)(state->input);
		return self->parse_func_(key, arg, state, self->arguments_);
	}

	static std::string get_default_args_doc() { return "ARG"; }
	static std::string get_default_doc() { return "A program with options"; }
};

} // namespace cli

#endif
