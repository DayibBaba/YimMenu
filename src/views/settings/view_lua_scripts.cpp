#include "fiber_pool.hpp"
#include "lua/lua_manager.hpp"
#include "views/view.hpp"
#include "widgets/TextEditor.hpp"

namespace big
{

	struct lua_editor
	{
		TextEditor editor;
		std::stringstream file_content_buffer;
		std::filesystem::path file_path;

		lua_editor(std::filesystem::path path) :
		    file_path(path)
		{
			std::ifstream file(file_path);
			if (!file)
			{
				LOG(WARNING) << "Error: Cannot open file " << file_path << std::endl;
				return;
			}
			file_content_buffer << file.rdbuf();
			editor.SetText(file_content_buffer.str());
			editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
			editor.SetPalette(TextEditor::GetDarkPalette());
		}

		void save_changes()
		{
			std::ofstream file(file_path);
			if (!file)
			{
				LOG(WARNING) << "Error: Cannot open file " << file_path << std::endl;
				return;
			}

			file << editor.GetText();
			file.close();
		}
	};


	static std::weak_ptr<lua_module> selected_module{};
	static std::vector<lua_editor> editors;

	void add_module_to_editor_window()
	{
		if (!selected_module.lock().get())
			return;

		auto found = std::find_if(editors.begin(), editors.end(), [&](const lua_editor& editor) {
			return editor.file_path == selected_module.lock()->module_path();
		});

		if (found != editors.end())
			return;

		editors.push_back(lua_editor(selected_module.lock()->module_path()));
	}

	void render_editor()
	{
		if (ImGui::Begin("Lua Editor"))
		{
			if (ImGui::BeginTabBar("LuaFiles"))
			{
				for (auto& lua_editor : editors)
				{
					auto file_name = lua_editor.file_path.filename().string();
					if (ImGui::BeginTabItem(file_name.data()))
					{
						if (ImGui::Button("Save"))
						{
							lua_editor.save_changes();
						}
						ImGui::SameLine();
						if (ImGui::Button("Revert changes"))
						{
							lua_editor.editor.SetText(lua_editor.file_content_buffer.str());
						}

						lua_editor.editor.Render(file_name.data());

						ImGui::EndTabItem();
					}
				}
				ImGui::EndTabBar();
			}

			g.self.hud.typing = TYPING_TICKS;

			ImGui::End();
		}
	}

		void view::lua_scripts()
		{
		    if (editors.size() > 0)
			    render_editor();

			ImGui::PushItemWidth(250);
			components::sub_title("VIEW_LUA_SCRIPTS_LOADED_LUA_SCRIPTS"_T);

			if (components::button("VIEW_LUA_SCRIPTS_RELOAD_ALL"_T))
			{
				g_fiber_pool->queue_job([] {
					g_lua_manager->trigger_event<menu_event::ScriptsReloaded>();
					g_lua_manager->unload_all_modules();
					g_lua_manager->load_all_modules();
				});
			}
			ImGui::SameLine();
			ImGui::Checkbox("VIEW_LUA_SCRIPTS_AUTO_RELOAD_CHANGED_SCRIPTS"_T.data(), &g.lua.enable_auto_reload_changed_scripts);

			if (components::button("VIEW_LUA_SCRIPTS_OPEN_LUA_SCRIPTS_FOLDER"_T))
			{
				// TODO: make utility function instead
				std::string command = "explorer.exe /select," + g_lua_manager->get_scripts_folder().get_path().string();

				std::system(command.c_str());
			}

			ImGui::BeginGroup();
			components::sub_title("ENABLED_LUA_SCRIPTS"_T);
			{
				if (ImGui::BeginListBox("##empty", ImVec2(200, 200)))
				{
					g_lua_manager->for_each_module([](auto& module) {
					    if (ImGui::Selectable(module->module_name().c_str(),
					            !selected_module.expired() && selected_module.lock().get() == module.get()))
						    selected_module = module;
					});

					ImGui::EndListBox();
				}
			}
			ImGui::EndGroup();
			ImGui::SameLine();
			ImGui::BeginGroup();
			components::sub_title("DISABLED_LUA_SCRIPTS"_T);
			{
				if (ImGui::BeginListBox("##disabled_empty", ImVec2(200, 200)))
				{
					g_lua_manager->for_each_disabled_module([](auto& module) {
						if (ImGui::Selectable(module->module_name().c_str(),
						        !selected_module.expired() && selected_module.lock().get() == module.get()))
							selected_module = module;
					});

					ImGui::EndListBox();
				}
			}
			ImGui::EndGroup();

			ImGui::BeginGroup();
			if (!selected_module.expired())
			{
				ImGui::Separator();

				ImGui::Text(std::format("{}: {}",
				    "VIEW_LUA_SCRIPTS_SCRIPTS_REGISTERED"_T,
				    selected_module.lock()->m_registered_scripts.size())
				                .c_str());
				ImGui::Text(std::format("{}: {}",
				    "VIEW_LUA_SCRIPTS_MEMORY_PATCHES_REGISTERED"_T,
				    selected_module.lock()->m_registered_patches.size())
				                .c_str());
				ImGui::Text(
				    std::format("{}: {}", "VIEW_LUA_SCRIPTS_GUI_TABS_REGISTERED"_T, selected_module.lock()->m_gui.size())
				        .c_str());

				const auto id = selected_module.lock()->module_id();
				if (components::button("VIEW_LUA_SCRIPTS_RELOAD"_T))
				{
					const std::filesystem::path module_path = selected_module.lock()->module_path();

					g_lua_manager->unload_module(id);
					selected_module = g_lua_manager->load_module(module_path);
				}

				const auto is_disabled = selected_module.lock()->is_disabled();
				if (!is_disabled && components::button<ImVec2{0, 0}, ImVec4{0.58f, 0.15f, 0.15f, 1.f}>("DISABLE"_T))
				{
					selected_module = g_lua_manager->disable_module(id);
				}
				else if (is_disabled && components::button("ENABLE"_T.data()))
				{
					selected_module = g_lua_manager->enable_module(id);
				}

				if (components::button("VIEW_LUA_SCRIPTS_OPEN_IN_EDITOR"_T))
				{
					add_module_to_editor_window();
				}
			}

			ImGui::EndGroup();

			if (components::button<ImVec2{0, 0}, ImVec4{0.58f, 0.15f, 0.15f, 1.f}>("DISABLE_ALL_LUA_SCRIPTS"_T))
			{
				g_lua_manager->disable_all_modules();
			}
			ImGui::SameLine();
			if (components::button("ENABLE_ALL_LUA_SCRIPTS"_T))
			{
				g_lua_manager->enable_all_modules();
			}
		}
	}
