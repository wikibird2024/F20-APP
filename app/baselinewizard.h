#pragma once
#include <QWizard>

class BridgeClient;
class QComboBox;

// Guided baseline wizard (spec §7.2): same steps and words as the
// FILMeasure dialog. Each step page sends its bridge command and lets the
// operator continue only after the bridge said ok.
class BaselineWizard : public QWizard {
    Q_OBJECT
public:
    BaselineWizard(BridgeClient& bridge, QWidget* parent = nullptr);

signals:
    void baselineCommitted();
};
