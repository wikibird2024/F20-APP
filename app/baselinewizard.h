#pragma once
#include <QDateTime>
#include <QWizard>

#include <optional>

class BridgeClient;
class QComboBox;

// Guided baseline wizard (spec §7.2): same steps and words as the
// FILMeasure dialog. Each step page sends its bridge command and lets the
// operator continue only after the bridge said ok.
class BaselineWizard : public QWizard {
    Q_OBJECT
public:
    BaselineWizard(BridgeClient& bridge, QWidget* parent = nullptr);

    // When baselineCommit succeeded; empty if the operator never got that
    // far. The baseline age counts from here, not from closing the dialog.
    std::optional<QDateTime> committedAtUtc() const { return committedAtUtc_; }
    QString referenceMaterial() const { return referenceMaterial_; }

private:
    std::optional<QDateTime> committedAtUtc_;
    QString referenceMaterial_;
};
