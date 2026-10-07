#include "baselinewizard.h"

#include "bridgeclient.h"
#include "f20/errors.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWizardPage>

namespace
{

// One wizard page = one bridge command behind one big button.
// Next stays disabled until the command succeeded.
class CommandPage : public QWizardPage
{
  public:
    CommandPage(BridgeClient              &bridge,
                const QString             &title,
                const QString             &instruction,
                const QString             &buttonText,
                std::function<f20::json()> params,
                const QString             &command,
                std::function<void()>      onSuccess = {})
        : bridge_(bridge), command_(command), params_(std::move(params)), onSuccess_(std::move(onSuccess))
    {
        setTitle(title);
        auto *layout = new QVBoxLayout(this);
        auto *label = new QLabel(instruction);
        label->setWordWrap(true);
        layout->addWidget(label);
        button_ = new QPushButton(buttonText);
        button_->setMinimumHeight(42);
        layout->addWidget(button_);
        status_ = new QLabel;
        status_->setWordWrap(true);
        layout->addWidget(status_);

        connect(button_, &QPushButton::clicked, this, [this] {
            button_->setEnabled(false);
            status_->setText("...");
            // Context `this`: the operator may close the wizard while the
            // step runs; the page is then deleted and the reply dropped.
            bridge_.send(command_, params_ ? params_() : f20::json::object(), this, [this](const f20::Reply &reply) {
                button_->setEnabled(true);
                if (reply.ok) {
                    done_ = true;
                    if (onSuccess_)
                        onSuccess_();
                    status_->setText("OK");
                    status_->setStyleSheet("color: #1a7f37;");
                    emit completeChanged();
                } else {
                    const auto code = f20::errorCodeFromString(reply.errorCode);
                    const auto msg = code ? f20::operatorMessage(*code, reply.errorMessage) : reply.errorMessage;
                    status_->setText(QString::fromStdString(msg));
                    status_->setStyleSheet("color: #c62828;");
                }
            });
        });
    }

    bool isComplete() const override
    {
        return done_;
    }

  private:
    BridgeClient              &bridge_;
    QString                    command_;
    std::function<f20::json()> params_;
    std::function<void()>      onSuccess_;
    QPushButton               *button_;
    QLabel                    *status_;
    bool                       done_ = false;
};

} // namespace

BaselineWizard::BaselineWizard(BridgeClient &bridge, QWidget *parent) : QWizard(parent)
{
    setWindowTitle("Baseline");

    // Page 1: reference material (spec §5.4 baselineSetRefMat).
    auto *refCombo = new QComboBox;
    refCombo->addItems({"Si", "SiO2 (thermal oxide)"});
    auto *refPage = new CommandPage(
        bridge,
        "Reference material",
        "Choose the reference standard you will place on the stage, then set it.",
        "SET REFERENCE MATERIAL",
        [refCombo] { return f20::json{{"name", refCombo->currentText().toStdString()}}; },
        "baselineSetRefMat",
        [this, refCombo] { referenceMaterial_ = refCombo->currentText(); });
    static_cast<QVBoxLayout *>(refPage->layout())->insertWidget(1, refCombo);
    addPage(refPage);

    addPage(new CommandPage(bridge, "Step 1 - sample", "Place the SAMPLE on the stage.", "MEASURE SAMPLE", {}, "baselineStep1"));
    addPage(new CommandPage(bridge,
                            "Step 2 - reference",
                            "Place the REFERENCE wafer on the stage, at the same height as the sample.",
                            "MEASURE REFERENCE",
                            {},
                            "baselineStep2"));
    addPage(new CommandPage(bridge,
                            "Step 3 - background",
                            "REMOVE the reference from the stage (dark reading).",
                            "MEASURE BACKGROUND",
                            {},
                            "baselineStep3"));

    addPage(new CommandPage(
        bridge, "Commit", "Save this baseline. Measuring is allowed afterwards.", "COMMIT", {}, "baselineCommit", [this] {
            committedAtUtc_ = QDateTime::currentDateTimeUtc();
        }));
}
