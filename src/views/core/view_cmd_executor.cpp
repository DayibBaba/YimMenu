#include "backend/context/default_command_context.hpp"
#include "gui.hpp"
#include "pointers.hpp"
#include "services/hotkey/hotkey_service.hpp"
#include "util/string_operations.hpp"
#include "views/view.hpp"

namespace big
{
	//TODO Argument suggestions are limited to the last word in the buffer
	//TODO Allow for optional arguments??


	static std::vector<std::string> current_suggestion_list;
	static std::string command_buffer;
	static std::string auto_fill_suggestion;
	static std::string selected_suggestion;
	static int cursor_pos = 0;

	struct argument
	{
		std::string name;
		int index;
		int start_index;
		int end_index;
		bool is_argument = true; // If the argument is the command itself, this will be false
	};

	struct command_scope
	{
		command* cmd;
		std::string raw;
		std::string name; // If the command is not found, this will be the incomplete command
		int name_start_index;
		int name_end_index;
		int index;
		int start_index;
		int end_index;
		int argument_count;
		std::vector<argument> arguments;

		argument* get_argument(int cursor_pos)
		{
			auto found = std::find_if(arguments.begin(), arguments.end(), [&](const argument& arg) {
				return cursor_pos >= arg.start_index && cursor_pos <= arg.end_index;
			});

			if (found != arguments.end())
				return &*found;

			return nullptr;
		}
	};

static void clean_buffer(std::string& buffer)
	{
		std::string new_buffer;
		bool last_char_was_space = false;

		for (size_t i = 0; i < buffer.size(); ++i)
		{
			if (buffer[i] == ' ')
			{
				// Skip consecutive spaces
				if (!last_char_was_space)
				{
					new_buffer += ' ';
					last_char_was_space = true;
				}
			}
			else if (buffer[i] == ';')
			{
				new_buffer += ';';
				// Skip spaces after a semicolon
				while (i + 1 < buffer.size() && buffer[i + 1] == ' ')
				{
					++i;
				}
				last_char_was_space = false;
			}
			else
			{
				new_buffer += buffer[i];
				last_char_was_space = false;
			}
		}

		// Remove leading and trailing spaces (optional, if needed)
		size_t start = new_buffer.find_first_not_of(' ');
		size_t end   = new_buffer.find_last_not_of(' ');
		if (start == std::string::npos || end == std::string::npos)
		{
			buffer.clear(); // No non-space characters found
		}
		else
		{
			buffer = new_buffer.substr(start, end - start + 1);
		}
	}

	class serialized_buffer
	{
		std::string buffer;
		int total_length;
		std::vector<int> delimeter_index_list;
		int command_count;
		std::vector<command_scope> command_scopes;

	public:
		serialized_buffer(std::string buffer) :
		    buffer(buffer),
		    total_length(0),
		    command_count(0)
		{
			if (buffer.empty())
				return;

			clean_buffer(buffer);
			parse_buffer();
		}

		void parse_buffer()
		{
			auto separate_commands = string::operations::split(buffer, ';');

			command_count = separate_commands.size();
			total_length  = 0;

			for (int i = 0; i < command_count; i++)
			{
				auto words = string::operations::split(separate_commands[i], ' ');
				auto cmd   = command::get(rage::joaat(words.front()));

				command_scope scope;
				scope.cmd            = cmd;
				scope.name           = words.front();
				scope.index          = i;
				scope.start_index    = total_length;
				scope.raw            = separate_commands[i];
				scope.argument_count = words.size() - 1;

				size_t buffer_pos = total_length;

				for (int j = 0; j < words.size(); j++)
				{
					size_t word_start = buffer.find(words[j], buffer_pos);

					argument arg;
					arg.name        = words[j];
					arg.index       = j;
					arg.is_argument = j > 0;
					arg.start_index = word_start;
					arg.end_index   = word_start + words[j].size();
					scope.arguments.push_back(arg);

					buffer_pos = word_start + words[j].size();
					if (j < words.size() - 1)
					{
						buffer_pos++; // Move past the space
					}
				}

				scope.end_index = buffer_pos;
				total_length    = buffer_pos + 1; // Move past the semicolon or end of command

				command_scopes.push_back(scope);
			}
		}

		std::string deserialize()
		{
			if (command_count == 0)
				return std::string();

			std::string deserialized_buffer;
			for (auto& command : command_scopes)
			{
				if (!command.cmd)
					deserialized_buffer += command.raw;
				else
					deserialized_buffer += command.cmd->get_name();

				deserialized_buffer += ' ';

				if (!command.argument_count)
					continue;

				for (auto& argument : command.arguments)
				{
					deserialized_buffer += argument.name;

					if (argument.name != command.arguments.back().name)
						deserialized_buffer += ' ';
				}

				if (command.raw != command_scopes.back().raw)
					deserialized_buffer += ';';
			}

			return deserialized_buffer;
		}

		command_scope* get_command_scope(int cursor_pos)
		{
			auto found = std::find_if(command_scopes.begin(), command_scopes.end(), [&](const command_scope& scope) {
				return cursor_pos >= scope.start_index && cursor_pos <= scope.end_index;
			});

			if (found != command_scopes.end())
				return &*found;

			return nullptr;
		}

		bool is_current_index_argument(int cursor_pos)
		{
			auto* scope = get_command_scope(cursor_pos);

			if (!scope)
				return false;

			auto* argument = scope->get_argument(cursor_pos);

			return argument->is_argument;
		}

		int get_argument_index_from_char_index(int cursor_pos)
		{
			auto* scope = get_command_scope(cursor_pos);

			if (!scope)
				return -1;

			auto* argument = scope->get_argument(cursor_pos);

			if (!argument)
				return -1;

			for (size_t i = 0; i < scope->argument_count; i++)
			{
				if (argument->name == scope->arguments[i].name)
					return ++i; // arguments are 1 based
			}

			return -1;
		}

		command* get_command_of_index(int cursor_pos)
		{
			auto* scope = get_command_scope(cursor_pos);

			if (!scope)
				return nullptr;

			return scope->cmd;
		}

		// Updating command would mean updating every start and end index of the entire buffer
		void update_command_of_scope(int cursor_pos, std::string cmd)
		{
			auto* scope = get_command_scope(cursor_pos);

			if (!scope)
				return;

			auto original_cmd_textlen = scope->name.length();
			auto new_cmd_textlen      = cmd.length();
			auto len_diff             = new_cmd_textlen - original_cmd_textlen; // Can be negative
			scope->cmd                = command::get(rage::joaat(cmd));

			for (int i = scope->index; i < command_count; i++)
			{
				auto& current_scope = command_scopes[i];
				if (current_scope.index != scope->index)
				{
					current_scope.start_index += len_diff;
				}
				current_scope.end_index += len_diff;

				for (auto& argument : current_scope.arguments)
				{
					argument.start_index += len_diff;
					argument.end_index += len_diff;
				}
			}

			buffer = deserialize();
		}


		void update_argument_of_scope(int index, int arg, std::string new_argument)
		{
			auto* scope = get_command_scope(index);

			if (!scope)
				return;

			auto* argument = scope->get_argument(index);

			if (!argument)
				return;

			auto original_arg_textlen = argument->name.length();
			auto new_arg_textlen      = new_argument.length();
			auto len_diff             = new_arg_textlen - original_arg_textlen; // Can be negative
			argument->name            = new_argument;

			for (int i = scope->index; i < command_count; i++)
			{
				auto& current_scope = command_scopes[i];

				if (current_scope.index == scope->index)
				{
					current_scope.end_index += len_diff;
				}

				for (auto& current_argument : current_scope.arguments)
				{
					if (current_argument.index == argument->index)
					{
						current_argument.end_index += len_diff;
					}
				}
			}

			buffer = deserialize();
		}

		// Debugging purposes
		void print_scope_and_argument_index(int index)
		{
			auto* scope = get_command_scope(index);

			if (!scope)
				return;

			auto* argument = scope->get_argument(index);

			if (!argument)
			{
				LOG(INFO) << "No argument found";
				return;
			}

			LOG(INFO) << "Scope: " << scope->raw << " Argument: " << argument->name;
			LOG(INFO) << "Argument index: " << get_argument_index_from_char_index(index);
		}
	};

	static serialized_buffer s_buffer(command_buffer);


	void render_debug_info()
	{
		auto s_buffer          = serialized_buffer(command_buffer);
		bool is_index_argument = s_buffer.is_current_index_argument(cursor_pos);

		ImGui::Text("Deserialized buffer: %s", s_buffer.deserialize().c_str());
		ImGui::Text("Is Index Argument: %s", is_index_argument ? "True" : "False");
	}

	void log_command_buffer(std::string buffer)
	{
		serialized_buffer serialized(buffer);
		serialized.print_scope_and_argument_index(cursor_pos);
	}

	bool does_string_exist_in_list(const std::string& command, std::vector<std::string> list)
	{
		auto found = std::find(list.begin(), list.end(), command);
		return found != list.end();
	}

	std::vector<std::string> deque_to_vector(std::deque<std::string> deque)
	{
		std::vector<std::string> vector;
		for (auto& element : deque)
		{
			vector.push_back(element);
		}
		return vector;
	}

	static void add_to_last_used_commands(const std::string& command)
	{
		if (does_string_exist_in_list(command, deque_to_vector(g.cmd.command_history)))
		{
			return;
		}

		if (g.cmd.command_history.size() >= 10)
		{
			g.cmd.command_history.pop_back();
		}

		g.cmd.command_history.push_front(command);
	}

	std::string auto_fill_command(std::string current_buffer)
	{
		if (command::get(rage::joaat(current_buffer)) != nullptr)
			return current_buffer;

		for (auto [key, cmd] : g_commands)
		{
			if (cmd && cmd->get(key) && &cmd->get_name())
			{
				if (cmd->get_name().find(current_buffer) != std::string::npos)
					return cmd->get_name();
			}
		}

		return std::string();
	}

	// What word in the sentence are we currently at
	int current_index(std::string current_buffer)
	{
		auto separate_commands = string::operations::split(current_buffer, ';'); // Split by semicolon to support multiple commands
		auto words = string::operations::split(separate_commands.back(), ' ');
		return words.size();
	}

	std::vector<std::string> suggestion_list_filtered(std::vector<std::string> suggestions, std::string filter)
	{
		std::vector<std::string> suggestions_filtered;
		std::string filter_lowercase = filter;
		string::operations::to_lower(filter_lowercase);
		for (auto suggestion : suggestions)
		{
			std::string suggestion_lowercase = suggestion;
			string::operations::to_lower(suggestion_lowercase);
			auto words = string::operations::split(command_buffer, ' ');
			if (suggestion_lowercase.find(filter_lowercase) != std::string::npos || does_string_exist_in_list(words.back(), current_suggestion_list) /*Need this to maintain suggestion list while navigating it*/)
				suggestions_filtered.push_back(suggestion);
		}

		return suggestions_filtered;
	}

	void get_appropriate_suggestion(std::string current_buffer, std::string& suggestion_)
	{
		auto separate_commands = string::operations::split(current_buffer, ';'); // Split by semicolon to support multiple commands
		auto words           = string::operations::split(separate_commands.back(), ' ');
		auto current_command = command::get(rage::joaat(words.front()));
		auto argument_index  = current_index(current_buffer);

		if (argument_index == 1)
		{
			suggestion_ = auto_fill_command(words.back());
			return;
		}
		else
		{
			if (!current_command)
				return;

			auto suggestions = current_command->get_argument_suggestions(argument_index - 1);

			if (suggestions == std::nullopt)
				return;

			for (auto suggestion : suggestion_list_filtered(suggestions.value(), words.back()))
			{
				std::string guess_lowercase      = words.back();
				std::string suggestion_lowercase = suggestion;
				string::operations::to_lower(suggestion_lowercase);
				string::operations::to_lower(guess_lowercase);

				if (suggestion_lowercase.find(guess_lowercase) != std::string::npos)
				{
					suggestion_ = suggestion;
					break;
				}
			}
		}
	}

	void get_previous_from_list(std::vector<std::string>& list, std::string& current)
	{
		auto found = std::find_if(list.begin(), list.end(), [&](const std::string& cmd) {
			return cmd == current;
		});

		if (found == list.end())
		{
			if (list.size() > 0)
				current = list.back();

			return;
		}

		if (*found == list.front())
		{
			current = list.back();
			return;
		}

		if (found - 1 != list.end())
			current = *(found - 1);
	}

	void get_next_from_list(std::vector<std::string>& list, std::string& current)
	{
		auto found = std::find_if(list.begin(), list.end(), [&](const std::string& cmd) {
			return cmd == current;
		});

		if (found == list.end())
		{
			if (list.size() > 0)
				current = list.front();

			return;
		}

		if (*found == list.back())
		{
			current = list.front();
			return;
		}

		if (found + 1 != list.end())
			current = *(found + 1);
	}

	void rebuild_buffer_with_suggestion(ImGuiInputTextCallbackData* data, std::string suggestion)
	{
		auto separate_commands = string::operations::split(data->Buf, ';'); // Split by semicolon to support multiple commands
		auto words = string::operations::split(separate_commands.back(), ' ');
		std::string new_text;

		// Replace the last word with the suggestion
		words.pop_back();
		words.push_back(suggestion);

		// Replace the last command with the new suggestion
		separate_commands.pop_back();
		separate_commands.push_back(string::operations::join(words, ' '));

		new_text = string::operations::join(separate_commands, ';');

		data->DeleteChars(0, data->BufTextLen);
		data->InsertChars(0, new_text.c_str());
	}

	bool buffer_needs_cleaning(const std::string& input)
	{
		for (size_t i = 0; i < input.size(); ++i)
		{
			if (input[i] == ' ')
			{
				if (i + 1 < input.size() && input[i + 1] == ' ')
				{
					return true; // Consecutive spaces
				}
			}
			else if (input[i] == ';')
			{
				if (i + 1 < input.size() && (input[i + 1] == ';' || input[i + 1] == ' '))
				{
					return true; // Consecutive semicolons or space after semicolon
				}
			}
		}
		return false;
	}

	static int input_callback(ImGuiInputTextCallbackData* data)
	{
		if (!data)
			return 0;

		command_buffer = std::string(data->Buf);
		s_buffer       = serialized_buffer(command_buffer);

		if (cursor_pos != data->CursorPos)
		{
			selected_suggestion = std::string();
			cursor_pos          = data->CursorPos;
			log_command_buffer(data->Buf);

			if (buffer_needs_cleaning(data->Buf))
			{
				std::string cleaned_buffer = data->Buf;
				clean_buffer(cleaned_buffer);
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, cleaned_buffer.c_str());
			}
		}

		if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
		{
			// User has a suggestion selectable higlighted, this takes precedence
			if (!selected_suggestion.empty())
			{
				// This could be a history suggestion with arguments, so we have to check for it
				auto words   = string::operations::split(selected_suggestion, ' ');
				auto command = command::get(rage::joaat(words.front()));

				// Its a command, lets rewrite the entire buffer (history command potentially with arguments)
				if (command)
				{
					data->DeleteChars(0, data->BufTextLen);
					data->InsertChars(0, selected_suggestion.c_str());
				}
				// Its probably an argument suggestion or a raw command, append it
				else
					rebuild_buffer_with_suggestion(data, selected_suggestion);

				selected_suggestion = std::string();
				return 0;
			}

			std::string auto_fill_suggestion;
			get_appropriate_suggestion(data->Buf, auto_fill_suggestion);

			if (auto_fill_suggestion != data->Buf)
			{
				rebuild_buffer_with_suggestion(data, auto_fill_suggestion);
			}
		}
		else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
		{
			if (current_suggestion_list.empty())
				return 0;

			if (data->EventKey == ImGuiKey_UpArrow)
				get_previous_from_list(current_suggestion_list, selected_suggestion);

			else if (data->EventKey == ImGuiKey_DownArrow)
				get_next_from_list(current_suggestion_list, selected_suggestion);
		}

		return 0;
	}

	void view::cmd_executor()
	{
		if (!g.cmd_executor.enabled)
			return;

		float screen_x = (float)*g_pointers->m_gta.m_resolution_x;
		float screen_y = (float)*g_pointers->m_gta.m_resolution_y;

		ImGui::SetNextWindowPos(ImVec2(screen_x * 0.25f, screen_y * 0.2f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.65f);
		ImGui::SetNextWindowSize({screen_x * 0.5f, -1});

		if (ImGui::Begin("cmd_executor", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMouseInputs))
		{
			render_debug_info();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10.f, 15.f});
			components::sub_title("CMD_EXECUTOR_TITLE"_T);

			// set focus by default on input box
			ImGui::SetKeyboardFocusHere(0);

			ImGui::SetNextItemWidth((screen_x * 0.5f) - 30.f);

			if (components::input_text_with_hint("", "CMD_EXECUTOR_TYPE_CMD"_T, command_buffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways, nullptr, input_callback))
			{
				if (command::process(command_buffer, std::make_shared<default_command_context>(), false))
				{
					g.cmd_executor.enabled = false;
					add_to_last_used_commands(command_buffer);
					command_buffer      = {};
					selected_suggestion = std::string();
				}
			}

			if (!command_buffer.empty())
			{
				auto separate_commands = string::operations::split(command_buffer, ';'); // Split by semicolon to support multiple commands
				get_appropriate_suggestion(separate_commands.back(), auto_fill_suggestion);

				if (auto_fill_suggestion != command_buffer)
					ImGui::Text("Suggestion: %s", auto_fill_suggestion.data());
			}

			components::small_text("CMD_EXECUTOR_MULTIPLE_CMDS"_T);
			components::small_text("CMD_EXECUTOR_INSTRUCTIONS"_T);
			ImGui::Separator();
			ImGui::Spacing();

			if (current_suggestion_list.size() > 0)
			{
				for (auto suggestion : current_suggestion_list)
				{
					components::selectable(suggestion, suggestion == selected_suggestion);
				}
			}

			if (current_index(command_buffer) == 1)
			{
				if (!g.cmd.command_history.empty())
				{
					current_suggestion_list = deque_to_vector(g.cmd.command_history);
				}
			}
			// If we are at any index above the first word, suggest arguments
			else if (current_index(command_buffer) > 1)
			{
				auto current_buffer_index = current_index(command_buffer);
				auto separate_commands = string::operations::split(command_buffer, ';'); // Split by semicolon to support multiple commands
				auto buffer_words = string::operations::split(separate_commands.back(), ' ');

				if (auto current_command = command::get(rage::joaat(buffer_words.front())))
				{
					auto argument_suggestions = current_command->get_argument_suggestions(current_buffer_index - 1);
					if (argument_suggestions != std::nullopt)
					{
						auto filtered_suggestions = suggestion_list_filtered(argument_suggestions.value(), buffer_words.back());
						if (filtered_suggestions.size() > 10)
						{
							current_suggestion_list =
							    std::vector<std::string>(filtered_suggestions.begin(), filtered_suggestions.begin() + 10);
						}
						else
						{
							current_suggestion_list = filtered_suggestions;
						}
					}
				}
			}
			ImGui::PopStyleVar();
		}

		ImGui::End();
	}

	bool_command g_cmd_executor("cmdexecutor", "CMD_EXECUTOR", "CMD_EXECUTOR_DESC", g.cmd_executor.enabled, false);
}