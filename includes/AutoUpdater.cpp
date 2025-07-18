/*
 * AutoUpdater - A class for checking and applying updates from GitHub releases
 * 
 * Features:
 * - Checks GitHub releases for newer versions
 * - Downloads update assets
 * - Handles self-updating of the executable
 * - Supports verbose logging
 * - Cross-platform (Windows/Linux/macOS)
 */


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


#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#include <windows.h>
#define localtime_r(now, result) localtime_s(result, now)
#define timegm _mkgmtime
#endif


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
        /* 
        * Constructor - Initializes the updater with repository info
        * 
        * @param github_repo_owner: Owner of the GitHub repository
        * @param github_repo_name: Name of the GitHub repository
        * @param current_release_date: Current version date (YYYY-MM-DD)
        * @param asset_name: Name of the asset to download
        * @param verbose: Enable detailed logging
        */
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
        
        // Prevent default construction
        AutoUpdater() = delete;

        // Destructor - cleans up CURL resources
        ~AutoUpdater() {
            if (curl)
            {
                curl_easy_cleanup(curl);
            }
        }

        /*
        * Main update function - applies updates
        * 
        * Workflow:
        * 1. Downloads the update
        * 2. Creates backup
        * 3. Replaces executable
        */
        bool update()
        {
            if (release_url.empty())
            {
                log("Please run is_update_available() first");
                return false;
            }

            // Create temp directory for downloads
            string tmp_path = create_temp_directory();
            if (tmp_path.empty())
            {
                log("Got empty tmp path");
                return false;
            }

            // Download the update file
            string downloaded_file = download_update(tmp_path, release_url);
            if (downloaded_file.empty())
            {
                log("Could not download release");
                fs::remove_all(tmp_path);
                return false;
            }

            // Get current executable path (platform-specific)
            fs::path current_exe;
            try
            {
                current_exe = fs::canonical("/proc/self/exe"); // Linux
            }
            catch (...)
            {
                #ifdef _WIN32
                    char path[MAX_PATH];
                    GetModuleFileNameA(NULL, path, MAX_PATH);
                    current_exe = fs::path(path);
                #else
                    log("Could not determine current executable path");
                    fs::remove_all(tmp_path);
                    return false;
                #endif
            }

            // Create backup before replacing
            fs::path backup_path = fs::path(tmp_path) / (current_exe.filename().string() + ".bak");
            try
            {
                log("Creating backup of current executeble at " + tmp_path + "/" + (current_exe.filename().string() + ".bak"));
                fs::copy_file(current_exe, backup_path, fs::copy_options::overwrite_existing);
            }
            catch (...)
            {
                log("Failed to create backup of current executable");
                fs::remove_all(tmp_path);
                return false;
            }

            // Replace current executable
            try
            {
                #ifdef _WIN32
                    // Windows needs special handling
                    MoveFileExA(downloaded_file.c_str(), current_exe.string().c_str(), 
                            MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING);
                    log("Update scheduled for next restart");
                #else
                    // Linux/macOS - attempt direct replacement
                    log("Attempting to replace current executable");
                    
                    // First close curl handles
                    if (curl)
                    {
                        curl_easy_cleanup(curl);
                        curl = nullptr;
                    }
                    
                    // Get file size before replacement for verification
                    auto orig_size = fs::file_size(current_exe);
                    auto tar_size = fs::file_size(downloaded_file);
                    log("Current executable size: " + to_string(orig_size) + " bytes");
                    log("Downloaded file size: " + to_string(tar_size) + " bytes");

                    // Make the correct file permissions
                    fs::permissions(downloaded_file, 
                                fs::perms::owner_all | 
                                fs::perms::group_read |
                                fs::perms::others_read);

                    // Remove original executable
                    fs::remove(current_exe);
                    
                    // Copy the downloaded file to original executable's location
                    fs::copy(downloaded_file, current_exe);

                    // Verify after copy
                    auto new_size = fs::file_size(current_exe);

                    if (new_size == tar_size)
                    {
                        log("Replacement successful");
                    }
                    else
                    {
                        log("Replacement failed - size mismatch. Restoring backup");
                        fs::copy(backup_path, current_exe, fs::copy_options::overwrite_existing);
                    }
                #endif
            }
            catch (const exception& e)
            {
                log(string("Replacement failed: ") + e.what());
                
                // Attempt to restore backup
                try
                {
                    fs::copy(backup_path, current_exe, fs::copy_options::overwrite_existing);
                    log("Restored from backup");
                }
                catch (...)
                {
                    log("Critical: Failed to restore from backup!");
                }
                
                fs::remove_all(tmp_path);
                return false;
            }

            // Clean up (except on Windows where we need to keep files for reboot)
            #ifndef _WIN32
                fs::remove_all(tmp_path);
            #endif

            return true;
        }

        /*
        * Checks if a newer release is available on GitHub
        * 
        * Compares published dates and finds matching asset
        */
        bool is_update_available()
        {
            log("Checking for updates");
            if (!initialized && !initCurl())
            {
                return false;
            }

            // Get latest release info from GitHub API
            string url = "https://api.github.com/repos/" + github_repo_owner + "/" + github_repo_name + "/releases/latest";
            string response;
            
            // Set curl options
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

            // Parse JSON response
            istringstream iss(response);
            Json::Value root;
            Json::CharReaderBuilder builder;
            string errors;
            bool parsingSuccessful = Json::parseFromStream(builder, iss, &root, &errors);

            if (!parsingSuccessful)
            {
                log("Failed to parse json from github api: " + errors);
                log("Github API response: " + response);
                return false;
            }
            
            if (root.isMember("message") && root["message"].asString() == "Not Found")
            {
                log("Repository not found");
                log("Github API response: " + response);
                return false;
            }

            // Get the published date
            if (!root.isMember("published_at"))
            {
                log("No published_at field in response");
                log("Github API response: " + response);
                return false;
            }

            // Extract just the date part (first 10 chars of ISO string)
            string latest_full_date = root["published_at"].asString();
            string latest_date = latest_full_date.substr(0, 10);  // "2025-06-08"

            // Compare dates
            bool is_newer = latest_date > current_release_date;

            auto [assets, tag_name, asset_ids] = parse_github_api_response(response);

            log("Current release date: " + current_release_date);
            log("Latest release date: " + latest_date);

            log(string("Latest tag ") + tag_name);
            log("Assets:");

            // Print the results
            if (verbose)
            {
                for (const auto& [name, url] : assets)
                {
                    cout << "    " << name << " (id: " << asset_ids[name] << ") => " << url << endl;
                }
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
        CURL* curl;
        bool verbose;
        string release_url;
        bool initialized;
        string current_release_date;
        string github_repo_owner;
        string github_repo_name;
        string asset_name;

        // Progress tracking
        chrono::time_point<chrono::steady_clock> last_progress_update;
        static constexpr chrono::milliseconds progress_update_interval{100};

        /*
        * Progress callback for CURL - shows download progress bar
        */
        static int progress_callback(void* clientp, 
                                curl_off_t dltotal, 
                                curl_off_t dlnow, 
                                curl_off_t ultotal, 
                                curl_off_t ulnow)
        {
            AutoUpdater* self = static_cast<AutoUpdater*>(clientp);
            if (self && self->verbose && dltotal > 0) {
                const int bar_width = 50;
                float progress = static_cast<float>(dlnow) / dltotal;
                int pos = static_cast<int>(bar_width * progress);

                cout << "\r[";
                for (int i = 0; i < bar_width; ++i)
                {
                    if (i < pos) cout << "=";
                    else if (i == pos) cout << ">";
                    else cout << " ";
                }
                cout << "] " << static_cast<int>(progress * 100.0) << "% "
                    << dlnow/1024 << "KB/" << dltotal/1024 << "KB";
                cout.flush();
            }
            return 0;
        }

        void update_progress_bar(curl_off_t dlnow, curl_off_t dltotal)
        {
            auto now = chrono::steady_clock::now();
            if (now - last_progress_update < progress_update_interval)
            {
                return;
            }
            last_progress_update = now;

            const int bar_width = 50;
            float progress = static_cast<float>(dlnow) / dltotal;
            int pos = static_cast<int>(bar_width * progress);

            cout << "\r[";
            for (int i = 0; i < bar_width; ++i) {
                if (i < pos) cout << "=";
                else if (i == pos) cout << ">";
                else cout << " ";
            }
            cout << "] " << static_cast<int>(progress * 100.0) << "% "
                << dlnow/1024 << "KB/" << dltotal/1024 << "KB";
            cout.flush();
        }

        // Clear line
        void finish_progress_bar()
        {
            cout << "\r" << string(100, ' ') << "\r";
            cout.flush();
        }

        // Helper to parse ISO 8601 dates
        time_t parse_iso8601(const string& datetime_str)
        {
            tm tm = {};
            istringstream ss(datetime_str);
            ss >> get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            return timegm(&tm);  // UTC time
        }

        // Helper to format time for logging
        string format_time(time_t time) {
            tm tm_info;
            #ifdef _MSC_VER
                gmtime_s(&tm_info, &time);
            #else
                tm_info = *gmtime(&time);
            #endif
            char buffer[26];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_info);
            return string(buffer);
        }
        
        // Helper to initialize CURL
        bool initCurl()
        {
            curl = curl_easy_init();
            if (!curl)
            {
                if (verbose) cerr << "Failed to initialize CURL" << endl;
                return false;
            }
            initialized = true;
            return true;
        }

        // Helper function to clean up CURL
        void cleanupCurl()
        {
            if (initialized)
            {
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
        string download_update(string destination_dir, string download_url)
        {
            if (!initialized && !initCurl())
            {
                return "";
            }
            
            if (download_url.empty())
            {
                log("No download URL available");
                return "";
            }
            
            // Create proper file path inside the temp directory
            fs::path file_path = fs::path(destination_dir) / asset_name;
            
            # ifdef _WIN32
            FILE* fp = nullptr;
            if (fopen_s(&fp, file_path.string().c_str(), "wb") != 0 || !fp)
            {
                log("Failed to open file for writing: " + file_path.string());
                return "";
            }
            #else
            FILE* fp = fopen(file_path.string().c_str(), "wb");
            if (!fp)
            {
                log("Failed to open file for writing: " + file_path.string());
                return "";
            }
            #endif
            
            curl_easy_setopt(curl, CURLOPT_URL, download_url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // Important for GitHub redirects
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);     // Fail on HTTP errors

            // Add progress callback if verbose
            if (verbose)
            {
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            }
            
            log("Downloading update from: " + download_url);
            log("Saving to: " + file_path.string());
            
            CURLcode res = curl_easy_perform(curl);
            fclose(fp);

            if (verbose)
            {
                finish_progress_bar();
            }
            
            if (res != CURLE_OK) {
                log(string("Download failed: ") + curl_easy_strerror(res));
                fs::remove(file_path);
                return "";
            }

            // Verify download size
            error_code ec;
            auto file_size = fs::file_size(file_path, ec);
            if (ec || file_size == 0) {
                log("Downloaded file is empty or inaccessible");
                fs::remove(file_path);
                return "";
            }
            
            log("Latest release downloaded successfully");
            return file_path.string();
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
            #ifdef _WIN32
            localtime_s(&local_time, &now_time);
            #else
            localtime_r(&now_time, &local_time);
            #endif
            
            // Print timestamp and message
            cout << "AutoUpdater at " << put_time(&local_time, "%H:%M:%S") << ": " << log_string << endl;
        }
};