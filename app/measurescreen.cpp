#include "measurescreen.h"

#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QPushButton>
#include <QSpinBox>
#include <QValueAxis>

MeasureScreen::MeasureScreen(QWidget* parent) : QWidget(parent) {
    auto* layout = new QGridLayout(this);

    recipeBox_ = new QComboBox;
    operatorEdit_ = new QLineEdit;
    operatorEdit_->setPlaceholderText("operator");
    sampleEdit_ = new QLineEdit;
    sampleEdit_->setPlaceholderText("sample id");

    measureButton_ = new QPushButton("MEASURE");
    measureButton_->setMinimumHeight(48);
    blockedReason_ = new QLabel;
    blockedReason_->setStyleSheet("color: #b05000;");

    thicknessLabel_ = new QLabel("- nm");
    thicknessLabel_->setStyleSheet("font-size: 20px; font-weight: bold;");
    gofLabel_ = new QLabel("GOF -");
    verdictLabel_ = new QLabel("-");
    verdictLabel_->setAlignment(Qt::AlignCenter);
    verdictLabel_->setMinimumHeight(64);
    verdictLabel_->setStyleSheet("font-size: 28px; font-weight: bold;");

    autoCycleBox_ = new QCheckBox("auto cycle every");
    autoCycleSeconds_ = new QSpinBox;
    autoCycleSeconds_->setRange(2, 3600);
    autoCycleSeconds_->setValue(10);
    autoCycleSeconds_->setSuffix(" s");

    // Spectrum chart: measured in blue, like FILMeasure (spec §7.1).
    auto* chart = new QChart;
    measuredSeries_ = new QLineSeries;
    measuredSeries_->setName("measured");
    measuredSeries_->setColor(QColor(30, 90, 200));
    chart->addSeries(measuredSeries_);
    auto* axisX = new QValueAxis;
    axisX->setTitleText("wavelength [nm]");
    axisX->setRange(400, 1000);
    auto* axisY = new QValueAxis;
    axisY->setTitleText("reflectance");
    axisY->setRange(0.0, 0.45);
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    measuredSeries_->attachAxis(axisX);
    measuredSeries_->attachAxis(axisY);
    chartView_ = new QChartView(chart);
    chartView_->setMinimumHeight(300);

    int row = 0;
    layout->addWidget(new QLabel("Recipe:"), row, 0);
    layout->addWidget(recipeBox_, row, 1);
    layout->addWidget(blockedReason_, row, 2, 1, 2);
    ++row;
    layout->addWidget(chartView_, row, 0, 1, 3);
    auto* resultPanel = new QVBoxLayout;
    resultPanel->addWidget(new QLabel("Layer 1"));
    resultPanel->addWidget(thicknessLabel_);
    resultPanel->addWidget(gofLabel_);
    resultPanel->addWidget(verdictLabel_);
    resultPanel->addStretch();
    layout->addLayout(resultPanel, row, 3);
    ++row;
    layout->addWidget(operatorEdit_, row, 0);
    layout->addWidget(sampleEdit_, row, 1);
    layout->addWidget(autoCycleBox_, row, 2);
    layout->addWidget(autoCycleSeconds_, row, 3);
    ++row;
    layout->addWidget(measureButton_, row, 0, 1, 4);

    connect(measureButton_, &QPushButton::clicked, this,
            &MeasureScreen::measureRequested);
    connect(recipeBox_, &QComboBox::currentTextChanged, this,
            &MeasureScreen::recipeChanged);
    auto emitAutoCycle = [this] {
        emit autoCycleToggled(autoCycleBox_->isChecked(),
                              autoCycleSeconds_->value());
    };
    connect(autoCycleBox_, &QCheckBox::toggled, this, emitAutoCycle);
    connect(autoCycleSeconds_, &QSpinBox::valueChanged, this, emitAutoCycle);
}

void MeasureScreen::setRecipes(const QStringList& recipes) {
    recipeBox_->clear();
    recipeBox_->addItems(recipes);
}

QString MeasureScreen::currentRecipe() const { return recipeBox_->currentText(); }
QString MeasureScreen::operatorName() const { return operatorEdit_->text(); }
QString MeasureScreen::sampleId() const { return sampleEdit_->text(); }
bool MeasureScreen::autoCycleEnabled() const { return autoCycleBox_->isChecked(); }
int MeasureScreen::autoCycleSeconds() const { return autoCycleSeconds_->value(); }

void MeasureScreen::setMeasureAllowed(bool allowed, const QString& reason) {
    measureButton_->setEnabled(allowed);
    blockedReason_->setText(allowed ? QString() : reason);
}

void MeasureScreen::showResult(const f20::MeasureResult& result) {
    if (!result.layers.empty())
        thicknessLabel_->setText(
            QString::number(result.layers.front().thicknessNm, 'f', 1) + " nm");
    gofLabel_->setText("GOF " + QString::number(result.gof, 'f', 3));
    verdictLabel_->setText(result.passed ? "PASS" : "FAIL");
    verdictLabel_->setStyleSheet(
        QString("font-size: 28px; font-weight: bold; color: white;"
                "background: %1; border-radius: 6px;")
            .arg(result.passed ? "#1a7f37" : "#c62828"));
}

void MeasureScreen::showSpectrum(const f20::Spectrum& spectrum) {
    QList<QPointF> points;
    points.reserve(static_cast<int>(spectrum.wavelengthNm.size()));
    for (std::size_t i = 0; i < spectrum.wavelengthNm.size(); ++i)
        points.append({spectrum.wavelengthNm[i], spectrum.reflectance[i]});
    measuredSeries_->replace(points);
}

void MeasureScreen::showError(const QString& operatorMessage) {
    verdictLabel_->setText("ERROR");
    verdictLabel_->setStyleSheet(
        "font-size: 22px; font-weight: bold; color: white;"
        "background: #b05000; border-radius: 6px;");
    blockedReason_->setText(operatorMessage);
}
