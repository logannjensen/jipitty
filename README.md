# jipitty

An OpenAI Large Language Model CLI, written in C++

`jipitty` is a command-line interface for interacting with OpenAI's GPT models. It supports conversation history, system prompts, file import/export, and more, all from your terminal.

## Dependencies

- [libcurl](https://curl.se/libcurl/)
- [GNU Readline](https://tiswww.case.edu/php/chet/readline/rltop.html)
- [nlohmann/json](https://github.com/nlohmann/json) (header is included in this repository)

## Dependency Installation

Install the required development packages for your distribution:

<details>
<summary><strong>Debian, Ubuntu</strong></summary>

```bash
sudo apt-get update
sudo apt-get install -y g++ libcurl4-openssl-dev libreadline-dev
```
</details>

<details>
<summary><strong>Fedora</strong></summary>

```bash
sudo dnf makecache
sudo dnf install -y gcc-c++ libcurl-devel readline-devel
```
</details>

<details>
<summary><strong>Arch Linux</strong></summary>

```bash
sudo pacman -Sy
sudo pacman -S --noconfirm base-devel curl readline
```
</details>

<details>
<summary><strong>Alpine Linux</strong></summary>

```bash
sudo apk update
sudo apk add g++ curl-dev readline-dev argp-standalone
```
</details>

If your distribution is not listed, please install the equivalent packages for:
- C++ compiler (`g++`)
- libcurl development files
- GNU Readline development files

---

## Build

After installing dependencies, build jipitty with the following command for your distribution:

<details>
<summary><strong>Debian, Ubuntu, Fedora, Arch (glibc-based)</strong></summary>

```bash
mkdir -p ./build
g++ -o ./build/jipitty -O3 code/* -lcurl -lreadline -I .
```
</details>

<details>
<summary><strong>Alpine Linux (musl-based)</strong></summary>

```bash
mkdir -p ./build
g++ -o ./build/jipitty -O3 code/* -lcurl -lreadline -largp -I .
```
</details>

Find the binary at `./build/jipitty`.

---

## Install (optional)

To install system-wide (requires root):

```bash
sudo install -m 755 ./build/jipitty /usr/local/bin/jipitty
```

Jipitty is compatible with any OpenAI completions endpoint. You can add something like this to your .bashrc to make seperate commands for various API models:
<details>
<summary><strong>~/.bashrc</strong></summary>

```bash
export OPENAI_API_KEY=your_openai_api_key
export XAI_API_KEY=your_grok_api_key
export DASHSCOPE_API_KEY=your_qwen_api_key
export DEEPSEEK_API_KEY=your_deepseek_api_key
export GEMINI_API_KEY=your_gemini_api_key
export ANTHROPIC_API_KEY=your_anthropic_api_key
alias gpt="jipitty"
alias o3="jipitty --model='o3-2025-04-16'"
alias grok="jipitty --apikey="$XAI_API_KEY" --url='https://api.x.ai' --model='grok-4'"
alias qwen="jipitty --apikey="$DASHSCOPE_API_KEY" --url='https://dashscope-intl.aliyuncs.com/compatible-mode/v1/chat/completions' --model='qwen-max'"
alias deepseek="jipitty --apikey="$DEEPSEEK_API_KEY" --url='https://api.deepseek.com/chat/completions' --model='deepseek-chat'"
alias gemini="jipitty --apikey="$GEMINI_API_KEY" --url='https://generativelanguage.googleapis.com/v1beta/openai/chat/completions' --model='gemini-2.5-pro'"
alias claude="jipitty --apikey="$ANTHROPIC_API_KEY" --url='https://api.anthropic.com' --model='claude-sonnet-4-20250514'"
```

</details>

---

## Notes

- You need an API key from https://platform.openai.com/account/api-keys
- The API key can be passed with `-a` or by exporting the environment variable `OPENAI_API_KEY`.

---

## Usage

```bash
jipitty -a your_openai_api_key
```

Or set the API key as an environment variable:

```bash
export OPENAI_API_KEY=your_openai_api_key
jipitty
```

You can use jipitty in a script with pipes:

```bash
echo 'Why am I talking to a cow?' | jipitty -t 0.85 -s 'You are a talking cow that speaks in short riddles and cryptic symbolism' | cowsay
```

---

## Commands

Once running, type `:help` for a list of commands.

Example:

```
:help
:file myfile.txt
:system "You are a sarcastic, cruel, and uncaring programming assistant"
:reset
:exit
```

---

## License

This project is licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later).  
See the [LICENSE](LICENSE) file for details.
