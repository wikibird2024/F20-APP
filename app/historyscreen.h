#pragma once
#include <QWidget>

class Storage;
class QTableView;
class QSqlQueryModel;
class QComboBox;
class QPushButton;
class QLineSeries;
class QChartView;

// History / SPC screen (spec §7.3): past results newest first, a trend
// chart per recipe with mean and +-3 sigma, CSV export.
class HistoryScreen : public QWidget {
    Q_OBJECT
public:
    explicit HistoryScreen(Storage& storage, QWidget* parent = nullptr);

public slots:
    void refresh();

signals:
    void reanalyzeRequested(const QString& spectrumFilePath);

private:
    void exportCsv();
    void updateTrend();

    Storage& storage_;
    QTableView* table_;
    QSqlQueryModel* model_;
    QComboBox* recipeFilter_;
    QLineSeries* trendSeries_;
    QLineSeries* meanSeries_;
    QLineSeries* upperSeries_;
    QLineSeries* lowerSeries_;
    QChartView* trendView_;
};
