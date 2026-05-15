#include <QCoreApplication>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "utils/settings.h"

int main(int argc, char** argv) {
    // Qt requires a QCoreApplication for certain features
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    Settings::resetConfigCache();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
