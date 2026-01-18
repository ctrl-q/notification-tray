#include "system_tray_file_browser.h"
#include "utils/logging.h"

#include <QDBusConnection>

#include <csignal>
#include <cstdlib>
#include <iostream>

static void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    QCoreApplication::quit();
}

int main(int argc, char* argv[]) {
    // Set up signal handler for clean exit
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Set log level from environment
    const char* log_level_env = std::getenv("LOGLEVEL");
    if (log_level_env) {
        QString log_level = QString(log_level_env).toUpper();
        if (log_level == "DEBUG") {
            Logger::setLogLevel(Logger::LOG_LEVEL_DEBUG);
        } else if (log_level == "INFO") {
            Logger::setLogLevel(Logger::LOG_LEVEL_INFO);
        } else if (log_level == "WARNING") {
            Logger::setLogLevel(Logger::LOG_LEVEL_WARNING);
        } else if (log_level == "ERROR") {
            Logger::setLogLevel(Logger::LOG_LEVEL_ERROR);
        }
    }

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <notification_storage_directory>" << std::endl;
        return 1;
    }

    fs::path root_path = argv[1];

    if (!fs::exists(root_path)) {
        std::cerr << "Error: Directory " << root_path << " does not exist" << std::endl;
        return 1;
    }

    // Initialize D-Bus main loop
    if (!QDBusConnection::sessionBus().isConnected()) {
        std::cerr << "Cannot connect to D-Bus session bus." << std::endl;
        return 1;
    }

    SystemTrayFileBrowser app(argc, argv, root_path);

    return app.exec();
}
