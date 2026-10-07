#include "historyscreen.h"

#include "storage.h"

#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineSeries>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QValueAxis>

#include <cmath>

namespace {

// One CSV cell of operator text (RFC 4180): quoted when it holds a comma,
// quote or line break. Text starting with = + - @ gets a leading ' so a
// spreadsheet shows it instead of running it as a formula ("CSV injection").
QString csvText(QString text) {
    if (!text.isEmpty() && QStringLiteral("=+-@\t\r").contains(text.front()))
        text.prepend('\'');
    if (text.contains(',') || text.contains('"') || text.contains('\n') || text.contains('\r'))
        text = '"' + text.replace("\"", "\"\"") + '"';
    return text;
}

} // namespace

HistoryScreen::HistoryScreen(Storage& storage, QWidget* parent)
    : QWidget(parent), storage_(storage) {
    auto* layout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel("Recipe:"));
    recipeFilter_ = new QComboBox;
    recipeFilter_->setMinimumWidth(200);
    topRow->addWidget(recipeFilter_);
    auto* refreshButton = new QPushButton("Refresh");
    topRow->addWidget(refreshButton);
    auto* exportButton = new QPushButton("Export CSV...");
    topRow->addWidget(exportButton);
    auto* reanalyzeButton = new QPushButton("Re-analyze spectrum file...");
    topRow->addWidget(reanalyzeButton);
    topRow->addStretch();
    layout->addLayout(topRow);
    statusLabel_ = new QLabel;
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    model_ = new QSqlQueryModel(this);
    table_ = new QTableView;
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 2);

    auto* chart = new QChart;
    chart->setTitle("thickness trend with mean and +-3 sigma");
    trendSeries_ = new QLineSeries;
    trendSeries_->setName("thicknessNm");
    meanSeries_ = new QLineSeries;
    meanSeries_->setName("mean");
    upperSeries_ = new QLineSeries;
    upperSeries_->setName("+3 sigma");
    lowerSeries_ = new QLineSeries;
    lowerSeries_->setName("-3 sigma");
    for (auto* s : {trendSeries_, meanSeries_, upperSeries_, lowerSeries_})
        chart->addSeries(s);
    auto* axisX = new QValueAxis;
    axisX->setTitleText("measurement #(oldest to newest)");
    auto* axisY = new QValueAxis;
    axisY->setTitleText("thickness [nm]");
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    for (auto* s : {trendSeries_, meanSeries_, upperSeries_, lowerSeries_}) {
        s->attachAxis(axisX);
        s->attachAxis(axisY);
    }
    trendView_ = new QChartView(chart);
    trendView_->setMinimumHeight(240);
    layout->addWidget(trendView_, 1);

    connect(refreshButton, &QPushButton::clicked, this, &HistoryScreen::refresh);
    connect(exportButton, &QPushButton::clicked, this, &HistoryScreen::exportCsv);
    connect(recipeFilter_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { updateTrend(); });
    connect(reanalyzeButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "Pick a saved spectrum", QString(),
            "Spectra (*.csv *.fmspe);;All files (*)");
        if (!path.isEmpty())
            emit reanalyzeRequested(path);
    });
}

void HistoryScreen::showStatus(const QString& text, bool isError) {
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(isError ? "color: #c62828;" : "color: #1a7f37;");
}

void HistoryScreen::refresh() {
    // Times are stored in UTC ("...Z"); the operator reads local time.
    // Rows from schema version 1 have local time without "Z" and are shown
    // as they are.
    model_->setQuery(
        "SELECT CASE WHEN time LIKE '%Z' THEN datetime(time, 'localtime')"
        " ELSE time END AS time, recipeName, operatorName, sampleId, layerNumber,"
        " thicknessNm, gof, CASE passed WHEN 1 THEN 'PASS' ELSE 'FAIL' END"
        " AS verdict, reanalyzedFrom"
        " FROM measurements ORDER BY id DESC",
        storage_.database());

    const QString remembered = recipeFilter_->currentText();
    recipeFilter_->blockSignals(true);
    recipeFilter_->clear();
    QSqlQuery recipes("SELECT DISTINCT recipeName FROM measurements",
                      storage_.database());
    while (recipes.next())
        recipeFilter_->addItem(recipes.value(0).toString());
    const int index = recipeFilter_->findText(remembered);
    if (index >= 0)
        recipeFilter_->setCurrentIndex(index);
    recipeFilter_->blockSignals(false);

    updateTrend();
}

void HistoryScreen::updateTrend() {
    trendSeries_->clear();
    meanSeries_->clear();
    upperSeries_->clear();
    lowerSeries_->clear();
    const QString recipe = recipeFilter_->currentText();
    if (recipe.isEmpty())
        return;

    QSqlQuery query(storage_.database());
    query.prepare(
        "SELECT thicknessNm FROM measurements"
        " WHERE recipeName = ? AND layerNumber = 1 ORDER BY id ASC");
    query.addBindValue(recipe);
    query.exec();

    QVector<double> values;
    while (query.next())
        values.append(query.value(0).toDouble());
    if (values.isEmpty())
        return;

    double sum = 0;
    for (double v : values)
        sum += v;
    const double mean = sum / values.size();
    double variance = 0;
    for (double v : values)
        variance += (v - mean) * (v - mean);
    const double sigma = values.size() > 1
                             ? std::sqrt(variance / (values.size() - 1))
                             : 0.0;

    for (int i = 0; i < values.size(); ++i)
        trendSeries_->append(i + 1, values[i]);
    for (const int x : {1, static_cast<int>(values.size())}) {
        meanSeries_->append(x, mean);
        upperSeries_->append(x, mean + 3 * sigma);
        lowerSeries_->append(x, mean - 3 * sigma);
    }

    const auto axes = trendView_->chart()->axes();
    if (axes.size() == 2) {
        static_cast<QValueAxis*>(axes[0])->setRange(0, values.size() + 1);
        static_cast<QValueAxis*>(axes[1])
            ->setRange(mean - 4 * sigma - 1, mean + 4 * sigma + 1);
    }
}

void HistoryScreen::exportCsv() {
    const QString path = QFileDialog::getSaveFileName(this, "Export CSV",
                                                      "results.csv",
                                                      "CSV (*.csv)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        showStatus("Cannot write " + path + ": " + file.errorString(), true);
        return;
    }
    QTextStream out(&file);
    out << "time,recipe,operator,sample,layer,thicknessNm,gof,verdict\n";
    QSqlQuery query(
        "SELECT time, recipeName, operatorName, sampleId, layerNumber,"
        " thicknessNm, gof, passed FROM measurements ORDER BY id",
        storage_.database());
    int rows = 0;
    while (query.next()) {
        for (int c = 0; c < 4; ++c) // operator-typed text
            out << csvText(query.value(c).toString()) << ',';
        for (int c = 4; c < 7; ++c) // numbers
            out << query.value(c).toString() << ',';
        out << (query.value(7).toInt() ? "PASS" : "FAIL") << '\n';
        ++rows;
    }
    out.flush();
    if (query.lastError().isValid()) {
        showStatus("Export incomplete - database error: " + query.lastError().text(), true);
    } else if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError) {
        showStatus("Export incomplete - cannot write " + path + ": " + file.errorString(), true);
    } else {
        showStatus(QString("Exported %1 rows to %2").arg(rows).arg(path), false);
    }
}
