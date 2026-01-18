#include <QCoreApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    // Qt requires a QCoreApplication for certain features
    QCoreApplication app(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
