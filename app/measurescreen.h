#pragma once
#include "f20/results.h"

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
class QSpinBox;
class QLineSeries;
class QChartView;

// Measure screen (spec §7.1): recipe, operator/sample, MEASURE, spectrum
// chart, result panel with a large PASS/FAIL verdict.
class MeasureScreen : public QWidget {
    Q_OBJECT
public:
    explicit MeasureScreen(QWidget* parent = nullptr);

    void setRecipes(const QStringList& recipes);
    QString currentRecipe() const;
    QString operatorName() const;
    QString sampleId() const;

    bool autoCycleEnabled() const;
    int autoCycleSeconds() const;

    // Enable/disable MEASURE with the reason shown (spec §6.1).
    void setMeasureAllowed(bool allowed, const QString& reasonWhenBlocked);
    // Unticks "auto cycle" (fault: no point retrying every N seconds).
    void stopAutoCycle();
    void showResult(const f20::MeasureResult& result);
    void showSpectrum(const f20::Spectrum& spectrum);
    void showError(const QString& operatorMessage);

signals:
    void measureRequested();
    void recipeChanged(const QString& name);
    void autoCycleToggled(bool enabled, int seconds);

private:
    QComboBox* recipeBox_;
    QLineEdit* operatorEdit_;
    QLineEdit* sampleEdit_;
    QPushButton* measureButton_;
    QLabel* blockedReason_;
    QString shownPermissionText_;
    QLabel* thicknessLabel_;
    QLabel* gofLabel_;
    QLabel* verdictLabel_;
    QCheckBox* autoCycleBox_;
    QSpinBox* autoCycleSeconds_;
    QLineSeries* measuredSeries_;
    QChartView* chartView_;
};
