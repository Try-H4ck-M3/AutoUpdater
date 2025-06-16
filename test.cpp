#include "includes/AutoUpdater.cpp"

using namespace std;

int main()
{
    AutoUpdater updater("hufrea", "byedpi", "2025-05-02", "byedpi-17-x86_64.tar.gz", true);

    if (updater.is_update_available())
    {
        cout << "Update available! Updating..." << endl;
        updater.update();
    }

    return 0;
}