# AutoUpdater - Self-Updating C++ Library

![C++](https://img.shields.io/badge/C++-17+-blue)
![Platforms](https://img.shields.io/badge/Platforms-Windows%20%7C%20Linux%20%7C%20macOS-blue)

A modern C++ library for seamless self-updating from GitHub releases with progress tracking and robust error handling.

## ✨ Features

- **Automatic Updates**: Check and apply updates from GitHub releases
- **Cross-Platform (IN PROGRESS)**: Works on Windows, Linux, and macOS
- **Progress Tracking**: Beautiful terminal progress bar during downloads
- **Safe Updates**: Automatic backups and rollback on failure
- **Verbose Logging**: Detailed timestamped logging for debugging
- **Simple API**: Easy integration with just a few lines of code

## 🚀 Quick Start ([Example](example.cpp))

```cpp
#include "includes/AutoUpdater.cpp"

int main()
{
    // AutoUpdater config
    string repo_name = "MyApp"; 
    string repo_author = "Author";
    string current_release_date = "2025-05-02";
    string asset_to_download = "app_linux_x86_64";
    bool verbose = true;

    // Initialize AutoUpdater
    AutoUpdater updater(repo_author, repo_name, current_release_date, asset_to_download, verbose);

    cout << "Checking for updates..." << endl;
    if (updater.is_update_available())
    {
        cout << "Update available! Updating..." << endl;
        if (updater.update())
        {
            cout << "Updated successfully!" << endl;
        }
        else
        {
            cout << "Error updating :(" << endl;
        }
    }
    else
    {
        cout << "Latest version already installed." << endl;
    }

    return 0;
}
```

## 🔧 Configuration

| Parameter | Type | Description |
|-----------|------|-------------|
| github_repo_owner | String | GitHub repository owner |
| github_repo_name | String | Repository name |
| current_release_date | String |Current version date (YYYY-MM-DD) |
| asset_name | String | Name of release asset to download |
| verbose | Bool |Enable detailed logging |

## 🌟 Example output (in verbose mode)

```
AutoUpdater at 14:59:11: Ready. Current release date: 2025-05-02
Checking for updates...
AutoUpdater at 14:59:11: Checking for updates
AutoUpdater at 14:59:11: Current release date: 2025-05-02
AutoUpdater at 14:59:11: Latest release date: 2025-05-15
AutoUpdater at 14:59:11: Latest tag 2.0
AutoUpdater at 14:59:11: Assets:
    app_linux_x86_64 (id: 123) => https://github.com/yourname/yourrepo/releases/download/2.0/app_linux_x86_64
    app_windows_x86_64.exe (id: 456) => https://github.com/yourname/yourrepo/releases/download/2.0/app_windows_x86_64.exe
AutoUpdater at 14:59:11: Selected asset: app_linux_x86_64
AutoUpdater at 14:59:11: Newer release available
AutoUpdater at 14:59:11: Downloading update from: https://github.com/yourname/yourrepo/releases/download/2.0/app_linux_x86_64
AutoUpdater at 14:59:11: Saving to: /tmp/autoupdater_123456/app_linux_x86_64
AutoUpdater at 14:59:28: Latest release downloaded successfully                                     
AutoUpdater at 14:59:28: Creating backup of current executeble at /tmp/autoupdater_123456/app_linux_x86_64.bak
AutoUpdater at 14:59:28: Attempting to replace current executable
AutoUpdater at 14:59:28: Current executable size: 123 bytes
AutoUpdater at 14:59:28: Downloaded file size: 456 bytes
AutoUpdater at 14:59:28: Replacement successful
```

## 🛡️ Safety Features

- Automatic backup of current executable
- Size verification before/after update
- Clean rollback on failure
- Temporary directory cleanup
- Windows-compatible delayed update installation

## 📚 Documentation

### Public Methods

- bool is_update_available()
    ```
    Checks GitHub for newer releases and returns true if an update is available.
    ```

- bool update()
    ```
    Downloads and applies the update. Returns true on success.
    ```

### Private Helpers

- download_update()
    ```
    Handles file download with progress tracking
    ```

- string create_temp_directory()
    ```
    Creates temporary directories. Returns temp path as string
    ```

- parse_github_api_response()
    ```
    Processes GitHub API JSON response
    ```

## 🤝 Contributing

### Implemented a new feature or fixed a bug? Send pull request!
### Please ensure:
- Code follows existing style and is readable
- New features are tested
- Documentation is updated
