#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <iomanip>
#include <regex>
#include <json/json.h>

using namespace std;
namespace fs = std::filesystem;

// Callback function for CURL to write data to a string
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class AutoUpdater
{
    public:
        // Constructor now takes just a date string (YYYY-MM-DD)
        AutoUpdater(const string& github_repo_owner, 
                const string& github_repo_name, 
                const string& current_release_date,
                const string& asset_name,
                bool verbose) 
            : github_repo_owner(github_repo_owner),
            github_repo_name(github_repo_name),
            current_release_date(current_release_date),
            asset_name(asset_name),
            verbose(verbose),
            curl(nullptr),
            initialized(false)
        {
            if (!initCurl()) 
            {
                throw runtime_error("Failed to initialize curl");
            }
            log("Ready. Current release date: " + current_release_date);
        }
        
        // Delete default constructor
        AutoUpdater() = delete;

        ~AutoUpdater() {
            if (curl)
            {
                curl_easy_cleanup(curl);
            }
        }


        // Public members exactly as you want them
        bool verbose;

        bool update()
        {
            if (release_url.empty())
            {
                log("Please run is_update_available() first");
                return false;
            }
            string tmp_path = create_temp_directory();
            if (tmp_path.empty())
            {
                return false;
            }
            download_update(tmp_path, release_url);
            return true;
        }
    
        bool is_update_available()
        {
            log("Checking for updates");
            if (!initialized && !initCurl())
            {
                return false;
            }
            
            string url = "https://api.github.com/repos/" + github_repo_owner + "/" + github_repo_name + "/releases/latest";
            // https://api.github.com/repos/owner/repo
            string response;
            
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Fix for SSL cert issue
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // Fix for SSL cert issue
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "AutoUpdater/1.0");
            
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                log(string("Curl failed: ") + curl_easy_strerror(res));
                return false;
            }
            
            istringstream iss(response);
            Json::Value root;
            Json::CharReaderBuilder builder;
            string errors;
            bool parsingSuccessful = Json::parseFromStream(builder, iss, &root, &errors);

            if (!parsingSuccessful)
            {
                log("Failed to parse json from github api: " + errors);
                return false;
            }

            // Get the published date
            if (!root.isMember("published_at"))
            {
                log("No published_at field in response");
                return false;
            }

            // Extract just the date part (first 10 chars of ISO string)
            string latest_full_date = root["published_at"].asString();
            string latest_date = latest_full_date.substr(0, 10);  // "2025-06-08"

            // Simple string comparison works for YYYY-MM-DD format
            bool is_newer = latest_date > current_release_date;

            auto [assets, tag_name, asset_ids] = parse_github_api_response(response);

            log("Current release date: " + current_release_date);
            log("Latest release date: " + latest_date);

            log(string("Latest tag ") + tag_name);
            log("Assets:");
            // Print the results
            for (const auto& [name, url] : assets)
            {
                cout << "    " << name << " (id: " << asset_ids[name] << ") => " << url << endl;
            }

            // Find the asset with the matching name
            if (assets.find(asset_name) != assets.end())
            {
                log("Selected asset: " + asset_name);
                release_url = assets[asset_name];
            }
            else
            {
                log("Could not find asset with name: " + asset_name);
                return false;
            }

            
            if (is_newer)
            {
                log("Newer release available");
                return true;
            }
            else
            {
                log("No newer releases found");
                return false;
            }
        }

    private:
        string repourl_;
        CURL* curl;
        string latestVersion;
        string release_url;
        bool initialized;
        string current_release_date;  // Stored as "YYYY-MM-DD"
        string github_repo_owner;
        string github_repo_name;
        string asset_name;

        // Helper to parse ISO 8601 dates (GitHub format)
        time_t parse_iso8601(const string& datetime_str)
        {
            tm tm = {};
            istringstream ss(datetime_str);
            ss >> get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            return timegm(&tm);  // UTC time
        }

        // Helper to format time for logging
        string format_time(time_t time)
        {
            tm* tm_info = gmtime(&time);
            char buffer[26];
            strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
            return string(buffer);
        }
        
        // Helper function to initialize CURL
        bool initCurl() {
            curl = curl_easy_init();
            if (!curl) {
                if (verbose) cerr << "Failed to initialize CURL" << endl;
                return false;
            }
            initialized = true;
            return true;
        }

        // Helper function to clean up CURL
        void cleanupCurl() {
            if (initialized) {
                curl_easy_cleanup(curl);
                initialized = false;
            }
        }

        string create_temp_directory()
        {
            error_code ec;
            
            // Get system temp directory
            fs::path temp_dir = fs::temp_directory_path(ec);
            if (ec)
            {
                log("Failed to get temp directory: " + ec.message());
                return "";
            }
            
            // Create unique directory name
            auto now = chrono::system_clock::now();
            auto timestamp = chrono::duration_cast<chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            temp_dir /= "autoupdater_" + to_string(timestamp);
            
            // Create the directory
            if (!fs::create_directory(temp_dir, ec))
            {
                log("Failed to create temp directory: " + ec.message());
                return "";
            }
            
            return temp_dir.string();
        }

        tuple<map<string, string>, string, map<string, int>> parse_github_api_response(const string& jsonResponse)
        {
            map<string, string> assets;  // name -> download_url
            map<string, int> asset_ids;  // name -> id
            string tag_name;             // latest version
            
            Json::Value root;
            Json::CharReaderBuilder builder;
            Json::CharReader* reader = builder.newCharReader();
            
            string errors;
            bool parsingSuccessful = reader->parse(
                jsonResponse.c_str(),
                jsonResponse.c_str() + jsonResponse.size(),
                &root,
                &errors
            );
            
            delete reader;
            
            if (!parsingSuccessful)
            {
                log("Failed to parse JSON: " + errors);
                return make_tuple(assets, tag_name, asset_ids);
            }
            
            // Get tag_name if it exists
            if (root.isMember("tag_name"))
            {
                tag_name = root["tag_name"].asString();
            }
            
            // Check if "assets" exists and is an array
            if (root.isMember("assets") && root["assets"].isArray())
            {
                const Json::Value& assetsArray = root["assets"];
                
                for (const Json::Value& asset : assetsArray) {
                    if (asset.isMember("name") && asset.isMember("browser_download_url") && asset.isMember("id"))
                    {
                        string name = asset["name"].asString();
                        string url = asset["browser_download_url"].asString();
                        int id = asset["id"].asInt();
                        
                        assets[name] = url;
                        asset_ids[name] = id;
                    }
                }
            }
            
            return make_tuple(assets, tag_name, asset_ids);
        }

        // Download the update
        bool download_update(string destination_dir, string download_url)
        {
            if (!initialized && !initCurl())
            {
                return false;
            }
            
            if (download_url.empty())
            {
                log("No download URL available");
                return false;
            }
            
            // Create proper file path inside the temp directory
            fs::path file_path = fs::path(destination_dir) / asset_name;
            
            FILE* fp = fopen(file_path.string().c_str(), "wb");
            if (!fp)
            {
                log("Failed to open file for writing: " + file_path.string());
                return false;
            }
            
            curl_easy_setopt(curl, CURLOPT_URL, download_url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            
            log("Downloading update from: " + download_url);
            log("Saving to: " + file_path.string());
            
            CURLcode res = curl_easy_perform(curl);
            fclose(fp);
            
            if (res != CURLE_OK) {
                log(string("Download failed: ") + curl_easy_strerror(res));
                return false;
            }
            
            log("Update downloaded successfully");
            return true;
        }

        void log(string log_string)
        {
            if (!verbose)
            {
                return;
            }

            // Get current time
            auto now = chrono::system_clock::now();
            auto now_time = chrono::system_clock::to_time_t(now);
            
            // Convert to local time
            tm local_time;
            localtime_r(&now_time, &local_time);  // Use localtime_s for Windows
            
            // Print timestamp and message
            cout << "AutoUpdater at " << put_time(&local_time, "%H:%M:%S") << ": " << log_string << endl;
        }
};