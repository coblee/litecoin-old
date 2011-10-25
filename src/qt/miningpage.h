#ifndef MININGPAGE_H
#define MININGPAGE_H

#include <QWidget>

#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QStringList>
#include <QMap>
#include <QSettings>

// Log types
#define STARTED 0
#define SHARE_SUCCESS 1
#define SHARE_FAIL 2
#define ERROR 3
#define LONGPOLL 4
#define FATAL_ERROR 5

namespace Ui {
    class MiningPage;
}

class MiningPage : public QWidget
{
    Q_OBJECT

public:
    explicit MiningPage(QWidget *parent = 0);
    ~MiningPage();

    bool minerActive;

    QProcess *minerProcess;

    QMap<int, double> threadSpeed;

    QTimer *readTimer;

    int acceptedShares;
    int rejectedShares;

    int roundAcceptedShares;
    int roundRejectedShares;

    int initThreads;

public slots:
    void startPressed();

    void startMining();
    void stopMining();

    void updateSpeed();

    void checkSettings();
    void saveSettings();

    void reportToList(QString, int, QString);

    void minerStarted();

    void minerError(QProcess::ProcessError);
    void minerFinished(int, QProcess::ExitStatus);

    void readProcessOutput();

    QString getTime(QString);

private:
    Ui::MiningPage *ui;
};

#endif // MININGPAGE_H
