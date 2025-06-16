#include "includes/AutoUpdater.cpp"

using namespace std;

int main()
{
    string repo_name = "byedpi";
    string repo_author = "hufrea";
    string current_release_date = "2025-05-02";
    string asset_to_download = "byedpi-17-x86_64.tar.gz";
    bool verbose = true;

    AutoUpdater updater(repo_author, repo_name, current_release_date, asset_to_download, verbose);

    if (updater.is_update_available())
    {
        cout << "Update available! Updating..." << endl;
        updater.update();
    }
    else
    {
        cout << "Latest version already installed." << endl;
    }

    return 0;
}