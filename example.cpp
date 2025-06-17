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