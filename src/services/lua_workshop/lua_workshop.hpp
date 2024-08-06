#include "./http_client/http_client.hpp"
#include "./thread_pool.hpp"
#include "lua/lua_manager.hpp"

namespace big
{
	enum class eGithubRepoFileType
	{
		FILE, // .lua most likely
		DIR,
		SUBMODULE,
		SYMLINK,
		UNKNOWN
	};

	static inline eGithubRepoFileType to_github_repo_file_type(const std::string type)
	{
		if (type == "file")
			return eGithubRepoFileType::FILE;
		if (type == "dir")
			return eGithubRepoFileType::DIR;
		if (type == "submodule")
			return eGithubRepoFileType::SUBMODULE;
		if (type == "symlink")
			return eGithubRepoFileType::SYMLINK;
		return eGithubRepoFileType::UNKNOWN;
	}

	struct lua_repo_file
	{
		std::string name;
		eGithubRepoFileType type;
		std::string download_url;
		std::vector<lua_repo_file> files; // If type is DIR

		lua_repo_file(nlohmann::json json)
		{
			LOG(INFO) << json;
			name                 = json["name"];
			std::string filetype = json["type"];
			type = to_github_repo_file_type(filetype);

			if (type == eGithubRepoFileType::FILE)
				download_url = json["download_url"];
			else if (type == eGithubRepoFileType::DIR) // Recursively cache files within directories
			{
				const auto response = g_http_client.get(std::string(json["url"]));
				if (response.status_code == 200)
				{
					nlohmann::json obj = nlohmann::json::parse(response.text);
					for (const auto& file : obj)
					{
						files.emplace_back(lua_repo_file(file));
					}
				}
			}
		}
	};

	struct lua_repo
	{
		std::uint32_t id;
		std::string name;
		std::string description;
		std::tm created_at;
		std::tm updated_at;
		int watchers;
		std::vector<lua_repo_file> files;

		lua_repo(nlohmann::json json)
		{
			id          = json["id"];
			name        = json["name"];
			description = json["description"];
			watchers    = json["watchers"];

			std::string created_at_str = json["created_at"];
			std::string updated_at_str = json["updated_at"];

			std::istringstream created_at_ss(created_at_str);
			std::istringstream updated_at_ss(updated_at_str);

			created_at_ss >> std::get_time(&created_at, "%Y-%m-%dT%H:%M:%S");
			updated_at_ss >> std::get_time(&updated_at, "%Y-%m-%dT%H:%M:%S");

			cache_files(json["contents_url"]);

			debug_func_dump_repo_to_log();
		}

		void cache_files(std::string contents_url)
		{
			std::string contents_url_trimmed = contents_url.substr(0, contents_url.find("{"));
			LOG(INFO) << "Caching files for " << name << " from " << contents_url_trimmed;
			const auto response = g_http_client.get(contents_url_trimmed);

			if (response.status_code == 200)
			{
				nlohmann::json obj = nlohmann::json::parse(response.text);
				for (const auto& file : obj)
				{
					files.emplace_back(lua_repo_file(file));
				}
			}
		};

		void debug_func_dump_repo_to_log()
		{
			LOG(INFO) << "Repo: " << name;
			LOG(INFO) << "Description: " << description;
			LOG(INFO) << "Watchers: " << watchers;
			LOG(INFO) << "Created at: " << std::put_time(&created_at, "%Y-%m-%d %H:%M:%S");
			LOG(INFO) << "Updated at: " << std::put_time(&updated_at, "%Y-%m-%d %H:%M:%S");
			LOG(INFO) << "Files: ";
			for (const auto& file : files)
			{
				if (file.type == eGithubRepoFileType::FILE)
					LOG(INFO) << "File: " << file.name << " URL: " << file.download_url;
				else if (file.type == eGithubRepoFileType::DIR)
				{
					LOG(INFO) << "Directory: " << file.name;
					for (const auto& subfile : file.files)
					{
						LOG(INFO) << "File: " << subfile.name << " URL: " << subfile.download_url;
					}
				}
			}
		};
	};


	class lua_workshop
	{
		const std::string m_lua_repos_api_url = "https://api.github.com/orgs/YimMenu-Lua/repos";
		std::vector<lua_repo> m_lua_repos;

	private:
		void fetch_and_parse_repos()
		{
			g_thread_pool->push([this]() {
				const auto response = g_http_client.get(m_lua_repos_api_url);

				if (response.status_code == 200)
				{
					nlohmann::json obj = nlohmann::json::parse(response.text);
					for (const auto& repo : obj)
					{
						if (repo["name"] == "submission" || repo["name"] == "Example")
							continue;

						m_lua_repos.emplace_back(lua_repo(repo));
					}
				}
			});
		}

	public:
		void initialize()
		{
			fetch_and_parse_repos();
		}

		bool download_lua_repo(lua_repo&);

		void render_lua_workshop_ui();

	};

	inline lua_workshop g_lua_workshop_service;
}