#include <QApplication>
#include <QMetaType>

#include "data/SampleData.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VitalMonitor"));
    app.setOrganizationName(QStringLiteral("QtMedicalDemo"));

    // 注册自定义类型，使跨线程信号槽（QueuedConnection）可以传递它们。
    qRegisterMetaType<ChannelSample>("ChannelSample");
    qRegisterMetaType<SampleFrame>("SampleFrame");
    qRegisterMetaType<AlarmEvent>("AlarmEvent");
    qRegisterMetaType<QVector<ChannelConfig>>("QVector<ChannelConfig>");

    MainWindow w;
    w.show();

    return app.exec();
}
