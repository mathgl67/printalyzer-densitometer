#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QtCore/QDebug>
#include <QtCore/QThread>
#include <QtCore/QMimeData>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtGui/QImage>
#include <QtGui/QValidator>
#include <QtGui/QStandardItemModel>
#include <QtGui/QClipboard>

#include "connectdialog.h"
#include "densinterface.h"
#include "remotecontroldialog.h"
#include "gaincalibrationdialog.h"
#include "slopecalibrationdialog.h"
#include "logwindow.h"
#include "settingsexporter.h"
#include "settingsimportdialog.h"
#include "floatitemdelegate.h"
#include "util.h"

namespace
{
static const int MEAS_TABLE_ROWS = 10;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , statusLabel_(new QLabel)
    , serialPort_(new QSerialPort(this))
    , densInterface_(new DensInterface(this))
    , logWindow_(new LogWindow(this))
{
    // Setup initial state of menu items
    ui->setupUi(this);
    ui->actionConnect->setEnabled(true);
    ui->actionDisconnect->setEnabled(false);
    ui->actionConfigure->setEnabled(true);
    ui->actionExit->setEnabled(true);

    ui->actionImportSettings->setEnabled(false);
    ui->actionExportSettings->setEnabled(false);

    ui->refreshSensorsPushButton->setEnabled(false);
    ui->screenshotButton->setEnabled(false);

    ui->statusBar->addWidget(statusLabel_);

    ui->zeroIndicatorLabel->setPixmap(QPixmap());

    // Hide this menu item until we figure out what to use it for
    ui->actionConfigure->setVisible(false);

    // Setup menu shortcuts
    ui->actionCut->setShortcut(QKeySequence::Cut);
    ui->actionCopy->setShortcut(QKeySequence::Copy);
    ui->actionPaste->setShortcut(QKeySequence::Paste);
    ui->actionDelete->setShortcut(QKeySequence::Delete);
    ui->actionExit->setShortcut(QKeySequence::Quit);

    // Calibration (measurement light) field validation
    ui->reflLightLineEdit->setValidator(util::createIntValidator(1, 128, this));
    ui->tranLightLineEdit->setValidator(util::createIntValidator(1, 128, this));
    connect(ui->reflLightLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalLightTextChanged);
    connect(ui->tranLightLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalLightTextChanged);

    // Calibration (gain) field validation
    ui->med0LineEdit->setValidator(util::createFloatValidator(22.0, 27.0, 6, this));
    ui->med1LineEdit->setValidator(util::createFloatValidator(22.0, 27.0, 6, this));
    ui->high0LineEdit->setValidator(util::createFloatValidator(360.0, 440.0, 6, this));
    ui->high1LineEdit->setValidator(util::createFloatValidator(360.0, 440.0, 6, this));
    ui->max0LineEdit->setValidator(util::createFloatValidator(8500.0, 9900.0, 6, this));
    ui->max1LineEdit->setValidator(util::createFloatValidator(9100.0, 10700.0, 6, this));
    connect(ui->med0LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalGainTextChanged);
    connect(ui->med1LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalGainTextChanged);
    connect(ui->high0LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalGainTextChanged);
    connect(ui->high1LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalGainTextChanged);
    connect(ui->max0LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalGainTextChanged);
    connect(ui->max1LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalGainTextChanged);

    // Calibration (slope) field validation
    ui->b0LineEdit->setValidator(util::createFloatValidator(-100.0, 100.0, 6, this));
    ui->b1LineEdit->setValidator(util::createFloatValidator(-100.0, 100.0, 6, this));
    ui->b2LineEdit->setValidator(util::createFloatValidator(-100.0, 100.0, 6, this));
    connect(ui->b0LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalSlopeTextChanged);
    connect(ui->b1LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalSlopeTextChanged);
    connect(ui->b2LineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalSlopeTextChanged);

    // Calibration (reflection density) field validation
    ui->reflLoDensityLineEdit->setValidator(util::createFloatValidator(0.0, 2.5, 2, this));
    ui->reflLoReadingLineEdit->setValidator(util::createFloatValidator(0.0, 500.0, 6, this));
    ui->reflHiDensityLineEdit->setValidator(util::createFloatValidator(0.0, 2.5, 2, this));
    ui->reflHiReadingLineEdit->setValidator(util::createFloatValidator(0.0, 500.0, 6, this));
    connect(ui->reflLoDensityLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalReflectionTextChanged);
    connect(ui->reflLoReadingLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalReflectionTextChanged);
    connect(ui->reflHiDensityLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalReflectionTextChanged);
    connect(ui->reflHiReadingLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalReflectionTextChanged);

    // Calibration (transmission density) field validation
    ui->tranLoReadingLineEdit->setValidator(util::createFloatValidator(0.0, 500.0, 6, this));
    ui->tranHiDensityLineEdit->setValidator(util::createFloatValidator(0.0, 5.0, 2, this));
    ui->tranHiReadingLineEdit->setValidator(util::createFloatValidator(0.0, 500.0, 6, this));
    connect(ui->tranLoReadingLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalTransmissionTextChanged);
    connect(ui->tranHiDensityLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalTransmissionTextChanged);
    connect(ui->tranHiReadingLineEdit, &QLineEdit::textChanged, this, &MainWindow::onCalTransmissionTextChanged);

    // Top-level UI signals
    connect(ui->menuEdit, &QMenu::aboutToShow, this, &MainWindow::onMenuEditAboutToShow);
    connect(ui->actionConnect, &QAction::triggered, this, &MainWindow::openConnection);
    connect(ui->actionDisconnect, &QAction::triggered, this, &MainWindow::closeConnection);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);
    //connect(ui->actionConfigure, &QAction::triggered, settings_, &SettingsDialog::show);
    connect(ui->actionCut, &QAction::triggered, this, &MainWindow::onActionCut);
    connect(ui->actionCopy, &QAction::triggered, this, &MainWindow::onActionCopy);
    connect(ui->actionPaste, &QAction::triggered, this, &MainWindow::onActionPaste);
    connect(ui->actionDelete, &QAction::triggered, this, &MainWindow::onActionDelete);
    connect(ui->actionImportSettings, &QAction::triggered, this, &MainWindow::onImportSettings);
    connect(ui->actionExportSettings, &QAction::triggered, this, &MainWindow::onExportSettings);
    connect(ui->actionLogger, &QAction::triggered, this, &MainWindow::onLogger);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::about);

    // Log window UI signals
    connect(logWindow_, &LogWindow::opened, this, &MainWindow::onLoggerOpened);
    connect(logWindow_, &LogWindow::closed, this, &MainWindow::onLoggerClosed);

    // Measurement UI signals
    connect(ui->addReadingPushButton, &QPushButton::clicked, this, &MainWindow::onAddReadingClicked);
    connect(ui->copyTablePushButton, &QPushButton::clicked, this, &MainWindow::onCopyTableClicked);
    connect(ui->clearTablePushButton, &QPushButton::clicked, this, &MainWindow::onClearTableClicked);

    // Diagnostics UI signals
    connect(ui->refreshSensorsPushButton, &QPushButton::clicked, densInterface_, &DensInterface::sendGetSystemInternalSensors);
    connect(ui->screenshotButton, &QPushButton::clicked, densInterface_, &DensInterface::sendGetDiagDisplayScreenshot);
    connect(ui->remotePushButton, &QPushButton::clicked, this, &MainWindow::onRemoteControl);

    // Calibration UI signals
    connect(ui->calGetAllPushButton, &QPushButton::clicked, this, &MainWindow::onCalGetAllValues);
    connect(ui->lightGetPushButton, &QPushButton::clicked, densInterface_, &DensInterface::sendGetCalLight);
    connect(ui->lightSetPushButton, &QPushButton::clicked, this, &MainWindow::onCalLightSetClicked);
    connect(ui->gainCalPushButton, &QPushButton::clicked, this, &MainWindow::onCalGainCalClicked);
    connect(ui->gainGetPushButton, &QPushButton::clicked, densInterface_, &DensInterface::sendGetCalGain);
    connect(ui->gainSetPushButton, &QPushButton::clicked, this, &MainWindow::onCalGainSetClicked);
    connect(ui->slopeGetPushButton, &QPushButton::clicked, densInterface_, &DensInterface::sendGetCalSlope);
    connect(ui->slopeSetPushButton, &QPushButton::clicked, this, &MainWindow::onCalSlopeSetClicked);
    connect(ui->reflGetPushButton, &QPushButton::clicked, densInterface_, &DensInterface::sendGetCalReflection);
    connect(ui->reflSetPushButton, &QPushButton::clicked, this, &MainWindow::onCalReflectionSetClicked);
    connect(ui->tranGetPushButton, &QPushButton::clicked, densInterface_, &DensInterface::sendGetCalTransmission);
    connect(ui->tranSetPushButton, &QPushButton::clicked, this, &MainWindow::onCalTransmissionSetClicked);
    connect(ui->slopeCalPushButton, &QPushButton::clicked, this, &MainWindow::onSlopeCalibrationTool);

    // Densitometer interface update signals
    connect(densInterface_, &DensInterface::connectionOpened, this, &MainWindow::onConnectionOpened);
    connect(densInterface_, &DensInterface::connectionClosed, this, &MainWindow::onConnectionClosed);
    connect(densInterface_, &DensInterface::connectionError, this, &MainWindow::onConnectionError);
    connect(densInterface_, &DensInterface::densityReading, this, &MainWindow::onDensityReading);
    connect(densInterface_, &DensInterface::systemVersionResponse, this, &MainWindow::onSystemVersionResponse);
    connect(densInterface_, &DensInterface::systemBuildResponse, this, &MainWindow::onSystemBuildResponse);
    connect(densInterface_, &DensInterface::systemDeviceResponse, this, &MainWindow::onSystemDeviceResponse);
    connect(densInterface_, &DensInterface::systemUniqueId, this, &MainWindow::onSystemUniqueId);
    connect(densInterface_, &DensInterface::systemInternalSensors, this, &MainWindow::onSystemInternalSensors);
    connect(densInterface_, &DensInterface::diagDisplayScreenshot, this, &MainWindow::onDiagDisplayScreenshot);
    connect(densInterface_, &DensInterface::diagLogLine, logWindow_, &LogWindow::appendLogLine);
    connect(densInterface_, &DensInterface::calLightResponse, this, &MainWindow::onCalLightResponse);
    connect(densInterface_, &DensInterface::calGainResponse, this, &MainWindow::onCalGainResponse);
    connect(densInterface_, &DensInterface::calSlopeResponse, this, &MainWindow::onCalSlopeResponse);
    connect(densInterface_, &DensInterface::calReflectionResponse, this, &MainWindow::onCalReflectionResponse);
    connect(densInterface_, &DensInterface::calTransmissionResponse, this, &MainWindow::onCalTransmissionResponse);

    // Loop back the set-complete signals to refresh their associated values
    connect(densInterface_, &DensInterface::calLightSetComplete, densInterface_, &DensInterface::sendGetCalLight);
    connect(densInterface_, &DensInterface::calGainSetComplete, densInterface_, &DensInterface::sendGetCalGain);
    connect(densInterface_, &DensInterface::calSlopeSetComplete, densInterface_, &DensInterface::sendGetCalSlope);
    connect(densInterface_, &DensInterface::calReflectionSetComplete, densInterface_, &DensInterface::sendGetCalReflection);
    connect(densInterface_, &DensInterface::calTransmissionSetComplete, densInterface_, &DensInterface::sendGetCalTransmission);

    // Setup the measurement model
    measModel_ = new QStandardItemModel(MEAS_TABLE_ROWS, 2, this);
    measModel_->setHorizontalHeaderLabels(QStringList() << tr("Mode") << tr("Measurement") << tr("Offset"));
    ui->measTableView->setModel(measModel_);
    ui->measTableView->setItemDelegateForColumn(1, new FloatItemDelegate(0.0, 5.0, 2));
    ui->measTableView->setItemDelegateForColumn(2, new FloatItemDelegate(0.0, 5.0, 2));
    ui->measTableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->measTableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->measTableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    // Set the initial state of table items
    for (int row = 0; row < measModel_->rowCount(); row++) {
        // Non-editable mode item
        QStandardItem *item = new QStandardItem();
        item->setSelectable(false);
        item->setEditable(false);
        measModel_->setItem(row, 0, item);

        // Non-editable offset item
        item = new QStandardItem();
        item->setSelectable(false);
        item->setEditable(false);
        measModel_->setItem(row, 2, item);
    }

    QModelIndex index = measModel_->index(0, 1);
    ui->measTableView->setCurrentIndex(index);
    ui->measTableView->selectionModel()->clearSelection();

    ui->autoAddPushButton->setChecked(true);
    ui->addReadingPushButton->setEnabled(false);

    // Initialize all fields with blank values
    onSystemVersionResponse();
    onSystemBuildResponse();
    onSystemDeviceResponse();
    onSystemUniqueId();
    onSystemInternalSensors();

    refreshButtonState();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::connectToPort(const QString &portName)
{
    if (!portName.isEmpty()) {
        openConnectionToPort(portName);
    }
}

void MainWindow::openConnection()
{
    qDebug() << "Open connection";
    ConnectDialog *dialog = new ConnectDialog(this);
    connect(dialog, &QDialog::finished, this, &MainWindow::onOpenConnectionDialogFinished);
    dialog->setModal(true);
    dialog->show();
}

void MainWindow::onOpenConnectionDialogFinished(int result)
{
    ConnectDialog *dialog = dynamic_cast<ConnectDialog *>(sender());
    dialog->deleteLater();

    if (result == QDialog::Accepted) {
        const QString portName = dialog->portName();
        openConnectionToPort(portName);
    }
}

void MainWindow::openConnectionToPort(const QString &portName)
{
    qDebug() << "Connecting to:" << portName;
    serialPort_->setPortName(portName);
    serialPort_->setBaudRate(QSerialPort::Baud115200);
    serialPort_->setDataBits(QSerialPort::Data8);
    serialPort_->setParity(QSerialPort::NoParity);
    serialPort_->setStopBits(QSerialPort::OneStop);
    serialPort_->setFlowControl(QSerialPort::NoFlowControl);
    if (serialPort_->open(QIODevice::ReadWrite)) {
        serialPort_->setDataTerminalReady(true);
        if (densInterface_->connectToDevice(serialPort_)) {
            ui->actionConnect->setEnabled(false);
            ui->actionDisconnect->setEnabled(true);
            statusLabel_->setText(tr("Connected to %1").arg(portName));
        } else {
            serialPort_->close();
            statusLabel_->setText(tr("Unrecognized device"));
            QMessageBox::critical(this, tr("Error"), tr("Unrecognized device"));
        }
    } else {
        statusLabel_->setText(tr("Open error"));
        QMessageBox::critical(this, tr("Error"), serialPort_->errorString());
    }
}

void MainWindow::closeConnection()
{
    qDebug() << "Close connection";
    densInterface_->disconnectFromDevice();
    if (serialPort_->isOpen()) {
        serialPort_->close();
    }
    refreshButtonState();
    ui->actionConnect->setEnabled(true);
    ui->actionDisconnect->setEnabled(false);
}

void MainWindow::onImportSettings()
{
    QFileDialog fileDialog(this, tr("Load Device Settings"), QString(), tr("Settings Files (*.pds)"));
    fileDialog.setDefaultSuffix(".pds");
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    if (fileDialog.exec() && !fileDialog.selectedFiles().isEmpty()) {
        QString filename = fileDialog.selectedFiles().constFirst();
        if (!filename.isEmpty()) {
            SettingsImportDialog importDialog;
            if (!importDialog.loadFile(filename)) {
                QMessageBox::warning(this, tr("Error"), tr("Unable to read settings file"));
                return;
            }
            if (importDialog.exec() == QDialog::Accepted) {
                QMessageBox messageBox;
                messageBox.setWindowTitle(tr("Send to Device"));
                messageBox.setText(tr("Replace the current device settings with the selected values?"));
                messageBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
                messageBox.setDefaultButton(QMessageBox::Cancel);

                if (messageBox.exec() == QMessageBox::Ok) {
                    importDialog.sendSelectedSettings(densInterface_);
                    onCalGetAllValues();
                }
            }
        }
    }
}

void MainWindow::onExportSettings()
{
    SettingsExporter *exporter = new SettingsExporter(densInterface_, this);
    connect(exporter, &SettingsExporter::exportReady, this, [this, exporter]() {
        QFileDialog fileDialog(this, tr("Save Device Settings"), QString(), tr("Settings File (*.pds)"));
        fileDialog.setDefaultSuffix(".pds");
        fileDialog.setAcceptMode(QFileDialog::AcceptSave);
        if (fileDialog.exec() && !fileDialog.selectedFiles().isEmpty()) {
            QString filename = fileDialog.selectedFiles().constFirst();
            if (!filename.isEmpty()) {
                exporter->saveExport(filename);
            }
        }
        exporter->deleteLater();
    });
    connect(exporter, &SettingsExporter::exportFailed, this, [exporter]() {
        exporter->deleteLater();
    });
    exporter->prepareExport();
}

void MainWindow::onLogger(bool checked)
{
    if (checked) {
        logWindow_->show();
    } else {
        logWindow_->hide();
    }
}

void MainWindow::onLoggerOpened()
{
    qDebug() << "Log window opened";
    ui->actionLogger->setChecked(true);
    if (densInterface_->connected()) {
        densInterface_->sendSetDiagLoggingModeUsb();
    }
}

void MainWindow::onLoggerClosed()
{
    qDebug() << "Log window closed";
    ui->actionLogger->setChecked(false);
    if (densInterface_->connected()) {
        densInterface_->sendSetDiagLoggingModeDebug();
    }
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About"),
                       tr("<b>%1 v%2</b><br>"
                          "<br>"
                          "Copyright 2022 Dektronics, Inc. All rights reserved.")
                       .arg(QApplication::applicationName(),
                            QApplication::applicationVersion()));
}

void MainWindow::refreshButtonState()
{
    const bool connected = densInterface_->connected();
    if (connected) {
        ui->actionImportSettings->setEnabled(true);
        ui->actionExportSettings->setEnabled(true);
        ui->refreshSensorsPushButton->setEnabled(true);
        ui->screenshotButton->setEnabled(true);
        ui->remotePushButton->setEnabled(true);
        ui->calGetAllPushButton->setEnabled(true);
        ui->lightGetPushButton->setEnabled(true);
        ui->gainCalPushButton->setEnabled(true);
        ui->gainGetPushButton->setEnabled(true);
        ui->slopeGetPushButton->setEnabled(true);
        ui->reflGetPushButton->setEnabled(true);
        ui->tranGetPushButton->setEnabled(true);

        // Populate read-only edit fields that are only set
        // via the protocol for consistency of the data formats
        if (ui->low0LineEdit->text().isEmpty()) {
            ui->low0LineEdit->setText("1");
        }
        if (ui->low1LineEdit->text().isEmpty()) {
            ui->low1LineEdit->setText("1");
        }
        if (ui->tranLoDensityLineEdit->text().isEmpty()) {
            ui->tranLoDensityLineEdit->setText("0.00");
        }

        ui->low0LineEdit->setEnabled(true);
        ui->low1LineEdit->setEnabled(true);
        ui->med0LineEdit->setEnabled(true);
        ui->med1LineEdit->setEnabled(true);
        ui->high0LineEdit->setEnabled(true);
        ui->high1LineEdit->setEnabled(true);
        ui->max0LineEdit->setEnabled(true);
        ui->max1LineEdit->setEnabled(true);

    } else {
        ui->actionImportSettings->setEnabled(false);
        ui->actionExportSettings->setEnabled(false);
        ui->refreshSensorsPushButton->setEnabled(false);
        ui->screenshotButton->setEnabled(false);
        ui->remotePushButton->setEnabled(false);
        ui->calGetAllPushButton->setEnabled(false);
        ui->lightGetPushButton->setEnabled(false);
        ui->gainCalPushButton->setEnabled(false);
        ui->gainGetPushButton->setEnabled(false);
        ui->slopeGetPushButton->setEnabled(false);
        ui->reflGetPushButton->setEnabled(false);
        ui->tranGetPushButton->setEnabled(false);
    }

    // Make calibration values editable only if connected
    ui->reflLightLineEdit->setReadOnly(!connected);
    ui->tranLightLineEdit->setReadOnly(!connected);

    ui->med0LineEdit->setReadOnly(!connected);
    ui->med1LineEdit->setReadOnly(!connected);
    ui->high0LineEdit->setReadOnly(!connected);
    ui->high1LineEdit->setReadOnly(!connected);
    ui->max0LineEdit->setReadOnly(!connected);
    ui->max1LineEdit->setReadOnly(!connected);

    ui->b0LineEdit->setReadOnly(!connected);
    ui->b1LineEdit->setReadOnly(!connected);
    ui->b2LineEdit->setReadOnly(!connected);

    ui->reflLoDensityLineEdit->setReadOnly(!connected);
    ui->reflLoReadingLineEdit->setReadOnly(!connected);
    ui->reflHiDensityLineEdit->setReadOnly(!connected);
    ui->reflHiReadingLineEdit->setReadOnly(!connected);

    ui->tranLoReadingLineEdit->setReadOnly(!connected);
    ui->tranHiDensityLineEdit->setReadOnly(!connected);
    ui->tranHiReadingLineEdit->setReadOnly(!connected);

    onCalLightTextChanged();
    onCalGainTextChanged();
    onCalSlopeTextChanged();
    onCalReflectionTextChanged();
    onCalTransmissionTextChanged();
}

void MainWindow::onMenuEditAboutToShow()
{
    bool hasCut = false;
    bool hasCopy = false;
    bool hasPaste = false;
    bool hasDelete = false;

    if (ui->tabWidget->currentWidget() == ui->tabMeasurement) {
        const QWidget *focusWidget = ui->tabMeasurement->focusWidget();
        if (focusWidget) {
            if (ui->readingValueLineEdit == focusWidget && ui->readingValueLineEdit->hasSelectedText()) {
                hasCopy = true;
            } else if (ui->measTableView == focusWidget || ui->measTableView->isAncestorOf(focusWidget)) {
                if (!ui->measTableView->selectionModel()->selectedRows(1).isEmpty()) {
                    // It is possible to have a table selection, and have focus
                    // or highlight elsewhere on the tab. However, making the table
                    // still handle edit actions under this situation has a lot of
                    // corner cases that would need to be handled. Probably easier
                    // to leave it alone for now.
                    hasCut = true;
                    hasCopy = true;
                    hasPaste = true;
                    hasDelete = true;
                }
            }
        }
    } else if (ui->tabWidget->currentWidget() == ui->tabCalibration) {
        const QWidget *focusWidget = ui->tabCalibration->focusWidget();
        if (focusWidget) {
            const QLineEdit *lineEdit = qobject_cast<const QLineEdit *>(focusWidget);
            if (lineEdit) {
                if (lineEdit->hasSelectedText()) {
                    hasCopy = true;
                    if (!lineEdit->isReadOnly()) {
                        hasCut = true;
                        hasPaste = true;
                        hasDelete = true;
                    }
                } else {
                    if (!lineEdit->isReadOnly()) {
                        hasPaste = true;
                    }
                }
            }
        }
    }

    // Only let paste stay enabled if the clipboard has content
    if (hasPaste) {
        const QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();
        if (!mimeData->hasText()) {
            hasPaste = false;
        }
    }

    ui->actionCut->setEnabled(hasCut);
    ui->actionCopy->setEnabled(hasCopy);
    ui->actionPaste->setEnabled(hasPaste);
    ui->actionDelete->setEnabled(hasDelete);
}

void MainWindow::onConnectionOpened()
{
    qDebug() << "Connection opened";

    // Clear the calibration page since values could have changed
    ui->reflLightLineEdit->clear();
    ui->tranLightLineEdit->clear();

    ui->low0LineEdit->clear();
    ui->low1LineEdit->clear();
    ui->med0LineEdit->clear();
    ui->med1LineEdit->clear();
    ui->high0LineEdit->clear();
    ui->high1LineEdit->clear();
    ui->max0LineEdit->clear();
    ui->max1LineEdit->clear();

    ui->b0LineEdit->clear();
    ui->b1LineEdit->clear();
    ui->b2LineEdit->clear();

    ui->reflLoDensityLineEdit->clear();
    ui->reflLoReadingLineEdit->clear();
    ui->reflHiDensityLineEdit->clear();
    ui->reflHiReadingLineEdit->clear();

    ui->tranLoDensityLineEdit->clear();
    ui->tranLoReadingLineEdit->clear();
    ui->tranHiDensityLineEdit->clear();
    ui->tranHiReadingLineEdit->clear();

    densInterface_->sendSetMeasurementFormat(DensInterface::FormatExtended);
    densInterface_->sendSetAllowUncalibratedMeasurements(true);
    densInterface_->sendGetSystemBuild();
    densInterface_->sendGetSystemDeviceInfo();
    densInterface_->sendGetSystemUID();
    densInterface_->sendGetSystemInternalSensors();
    refreshButtonState();

    if (logWindow_->isVisible()) {
        densInterface_->sendSetDiagLoggingModeUsb();
    }
}

void MainWindow::onConnectionClosed()
{
    qDebug() << "Connection closed";
    refreshButtonState();
    ui->actionConnect->setEnabled(true);
    ui->actionDisconnect->setEnabled(false);

    if (densInterface_->deviceUnrecognized()) {
        statusLabel_->setText(tr("Unrecognized device"));
        QMessageBox::critical(this, tr("Error"), tr("Unrecognized device"));
    } else {
        statusLabel_->setText(tr("Disconnected"));
    }

    if (remoteDialog_) {
        remoteDialog_->close();
    }
}

void MainWindow::onConnectionError()
{
    closeConnection();
}

void MainWindow::onDensityReading(DensInterface::DensityType type, float dValue, float dZero, float rawValue, float corrValue)
{
    Q_UNUSED(rawValue)
    Q_UNUSED(corrValue)

    // Update main tab contents
    if (type == DensInterface::DensityReflection) {
        ui->readingTypeLogoLabel->setPixmap(QPixmap(QString::fromUtf8(":/images/reflection-icon.png")));
        ui->readingTypeNameLabel->setText(tr("Reflection"));
    } else {
        ui->readingTypeLogoLabel->setPixmap(QPixmap(QString::fromUtf8(":/images/transmission-icon.png")));
        ui->readingTypeNameLabel->setText(tr("Transmission"));
    }

    if (!qIsNaN(dZero)) {
        ui->zeroIndicatorLabel->setPixmap(QPixmap(QString::fromUtf8(":/images/zero-set-indicator.png")));
        float displayZero = dZero;
        if (qAbs(displayZero) < 0.01F) {
            displayZero = 0.0F;
        }
        ui->zeroIndicatorLabel->setToolTip(QString("%1D").arg(displayZero, 4, 'f', 2));
    } else {
        ui->zeroIndicatorLabel->setPixmap(QPixmap());
        ui->zeroIndicatorLabel->setToolTip(QString());
    }

    // Clean up the display value
    float displayValue;
    if (!qIsNaN(dZero)) {
        displayValue = dValue - dZero;
    } else {
        displayValue = dValue;
    }
    if (qAbs(displayValue) < 0.01F) {
        displayValue = 0.0F;
    }
    ui->readingValueLineEdit->setText(QString("%1D").arg(displayValue, 4, 'f', 2));

    // Save values so they can be referenced later
    lastReadingType_ = type;
    lastReadingDensity_ = displayValue;
    lastReadingOffset_ = dZero;
    ui->addReadingPushButton->setEnabled(true);

    // Update the measurement tab table view, if the tab is focused
    if (ui->tabWidget->currentWidget() == ui->tabMeasurement) {
        if (ui->autoAddPushButton->isChecked()) {
            onAddReadingClicked();
        }
    }

    // Update calibration tab fields, if focused
    if (type == DensInterface::DensityReflection) {
        if (ui->reflLoReadingLineEdit->hasFocus()) {
            ui->reflLoReadingLineEdit->setText(QString::number(corrValue, 'f'));
        } else if (ui->reflHiReadingLineEdit->hasFocus()) {
            ui->reflHiReadingLineEdit->setText(QString::number(corrValue, 'f'));
        }
    } else {
        if (ui->tranLoReadingLineEdit->hasFocus()) {
            ui->tranLoReadingLineEdit->setText(QString::number(corrValue, 'f'));
        } else if (ui->tranHiReadingLineEdit->hasFocus()) {
            ui->tranHiReadingLineEdit->setText(QString::number(corrValue, 'f'));
        }
    }
}

void MainWindow::onActionCut()
{
    QWidget *focusWidget = ui->tabWidget->currentWidget()->focusWidget();
    if (focusWidget) {
        // Handle the common case for a line edit widget
        QLineEdit *lineEdit = qobject_cast<QLineEdit *>(focusWidget);
        if (lineEdit && !lineEdit->isReadOnly()) {
            lineEdit->cut();
            return;
        }

        // Handle the case for a measurement table selection
        if (ui->tabWidget->currentWidget() == ui->tabMeasurement
                && focusWidget == ui->measTableView
                && !ui->measTableView->selectionModel()->selectedRows(1).isEmpty()) {
            measTableCut();
        }
    }
}

void MainWindow::onActionCopy()
{
    const QWidget *focusWidget = ui->tabWidget->currentWidget()->focusWidget();
    if (focusWidget) {
        // Handle the common case for a line edit widget
        const QLineEdit *lineEdit = qobject_cast<const QLineEdit *>(focusWidget);
        if (lineEdit) {
            lineEdit->copy();
            return;
        }

        // Handle the case for a measurement table selection
        if (ui->tabWidget->currentWidget() == ui->tabMeasurement
                && focusWidget == ui->measTableView
                && !ui->measTableView->selectionModel()->selectedRows(1).isEmpty()) {
            measTableCopy();
        }
    }
}

void MainWindow::onActionPaste()
{
    QWidget *focusWidget = ui->tabWidget->currentWidget()->focusWidget();
    if (focusWidget) {
        // Handle the common case for a line edit widget
        QLineEdit *lineEdit = qobject_cast<QLineEdit *>(focusWidget);
        if (lineEdit && !lineEdit->isReadOnly()) {
            lineEdit->paste();
            return;
        }

        // Handle the case for a measurement table selection
        if (ui->tabWidget->currentWidget() == ui->tabMeasurement
                && focusWidget == ui->measTableView
                && !ui->measTableView->selectionModel()->selectedRows(1).isEmpty()) {
            measTablePaste();
        }
    }
}

void MainWindow::onActionDelete()
{
    QWidget *focusWidget = ui->tabWidget->currentWidget()->focusWidget();
    if (focusWidget) {
        // Handle the common case for a line edit widget
        QLineEdit *lineEdit = qobject_cast<QLineEdit *>(focusWidget);
        if (lineEdit && !lineEdit->isReadOnly()) {
            lineEdit->del();
            return;
        }

        // Handle the case for a measurement table selection
        if (ui->tabWidget->currentWidget() == ui->tabMeasurement
                && focusWidget == ui->measTableView
                && !ui->measTableView->selectionModel()->selectedRows(1).isEmpty()) {
            measTableDelete();
        }
    }
}

void MainWindow::measTableAddReading(DensInterface::DensityType type, float density, float offset)
{

    QString numStr = QString("%1").arg(density, 4, 'f', 2);
    QString typeStr;
    QIcon typeIcon;
    QString offsetStr;

    if (type == DensInterface::DensityReflection) {
        typeIcon = QIcon(QString::fromUtf8(":/images/reflection-icon.png"));
        typeStr = QLatin1String("R");
    } else if (type == DensInterface::DensityTransmission) {
        typeIcon = QIcon(QString::fromUtf8(":/images/transmission-icon.png"));
        typeStr = QLatin1String("T");
    }

    if (!qIsNaN(offset)) {
        offsetStr = QString("%1").arg(offset, 4, 'f', 2);
    }

    int row = -1;
    QModelIndexList selected = ui->measTableView->selectionModel()->selectedIndexes();
    selected.append(ui->measTableView->selectionModel()->currentIndex());
    for (const QModelIndex &index : qAsConst(selected)) {
        if (row == -1 || index.row() < row) {
            row = index.row();
        }
    }
    ui->measTableView->selectionModel()->clearSelection();

    if (row >= 0) {
        QStandardItem *typeItem = new QStandardItem(typeIcon, typeStr);
        typeItem->setSelectable(false);
        typeItem->setEditable(false);
        measModel_->setItem(row, 0, typeItem);

        QStandardItem *measItem = new QStandardItem(numStr);
        measModel_->setItem(row, 1, measItem);

        QStandardItem *offsetItem = new QStandardItem(offsetStr);
        offsetItem->setSelectable(false);
        offsetItem->setEditable(false);
        measModel_->setItem(row, 2, offsetItem);

        if (row < measModel_->rowCount() - 1) {
            QModelIndex index = measModel_->index(row + 1, 1);
            ui->measTableView->setCurrentIndex(index);
        } else {
            measModel_->insertRow(row + 1);
            QModelIndex index = measModel_->index(row + 1, 1);
            ui->measTableView->setCurrentIndex(index);
        }
        ui->measTableView->scrollTo(ui->measTableView->currentIndex());
    }
}

void MainWindow::measTableCut()
{
    measTableCopy();
    measTableDelete();
}

void MainWindow::measTableCopy()
{
    QModelIndexList selected = ui->measTableView->selectionModel()->selectedRows(1);
    std::sort(selected.begin(), selected.end());
    measTableCopyList(selected, true);
}

void MainWindow::measTableCopyList(const QModelIndexList &indexList, bool includeEmpty)
{
    QVector<QString> numList;

    // Collect the list of populated measurement items in the table
    for (const QModelIndex &index : qAsConst(indexList)) {
        const QStandardItem *item = measModel_->itemFromIndex(index);
        if (item && item->column() == 1 && (includeEmpty || !item->text().isEmpty())) {
            numList.append(item->text());
        }
    }

    // Get the copy orientation
    bool horizCopy;
    if (ui->copyDirButtonGroup->checkedButton() == ui->horizCopyRadioButton) {
        horizCopy = true;
    } else {
        horizCopy = false;
    }

    // Build the string to put in the clipboard
    QString copiedText;
    for (const auto &numElement : numList) {
        if (!copiedText.isEmpty()) {
            if (horizCopy) {
                copiedText.append(QLatin1String("\t"));
            } else {
#if defined(Q_OS_WIN)
                copiedText.append(QLatin1String("\r\n"));
#else
                copiedText.append(QLatin1String("\n"));
#endif
            }
        }
        copiedText.append(numElement);
    }

    // Move to the clipboard
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(copiedText, QClipboard::Clipboard);

    if (clipboard->supportsSelection()) {
        clipboard->setText(copiedText, QClipboard::Selection);
    }

#if defined(Q_OS_UNIX)
    QThread::msleep(1);
#endif
}

void MainWindow::measTablePaste()
{
    // Capture and split the text to be pasted
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    QList<float> numList;
    if (mimeData->hasText()) {
        const QString text = mimeData->text();
        const QStringList elements = text.split(QRegExp("\n|\r\n|\r|\t|[,;]\\s*|\\s+"), Qt::SkipEmptyParts);
        for (const QString& element : elements) {
            bool ok;
            float num = element.toFloat(&ok);
            if (ok) {
                numList.append(num);
            }
        }
    }

    // Add the pasted readings
    for (float num : numList) {
        measTableAddReading(DensInterface::DensityUnknown, num, qSNaN());
    }
}

void MainWindow::measTableDelete()
{
    QModelIndexList selected = ui->measTableView->selectionModel()->selectedRows(1);

    for (const QModelIndex &index : qAsConst(selected)) {
        QStandardItem *item = measModel_->item(index.row(), 0);
        if (item) {
            item->setText(QString());
            item->setIcon(QIcon());
        }

        item = measModel_->item(index.row(), 1);
        if (item) { item->setText(QString()); }

        item = measModel_->item(index.row(), 2);
        if (item) { item->setText(QString()); }
    }
}

void MainWindow::onAddReadingClicked()
{
    if (lastReadingType_ == DensInterface::DensityUnknown
            || qIsNaN(lastReadingDensity_)) {
        return;
    }

    measTableAddReading(lastReadingType_, lastReadingDensity_, lastReadingOffset_);
}

void MainWindow::onCopyTableClicked()
{
    // Build a list of all the items in the measurement column
    QModelIndexList indexList;
    for (int row = 0; row < measModel_->rowCount(); row++) {
        indexList.append(measModel_->index(row, 1));
    }

    // Call the common function for copying data from the list
    measTableCopyList(indexList, false);
}

void MainWindow::onClearTableClicked()
{
    if (measModel_->rowCount() > MEAS_TABLE_ROWS) {
        measModel_->removeRows(MEAS_TABLE_ROWS, measModel_->rowCount() - 10);
    }

    for (int row = 0; row < measModel_->rowCount(); row++) {
        QStandardItem *item = measModel_->item(row, 0);
        if (item) {
            item->setText(QString());
            item->setIcon(QIcon());
        }

        item = measModel_->item(row, 1);
        if (item) { item->setText(QString()); }

        item = measModel_->item(row, 2);
        if (item) { item->setText(QString()); }
    }

    QModelIndex index = measModel_->index(0, 1);
    ui->measTableView->setCurrentIndex(index);
    ui->measTableView->selectionModel()->clearSelection();
    ui->measTableView->scrollToTop();
}

void MainWindow::onCalGetAllValues()
{
    densInterface_->sendGetCalLight();
    densInterface_->sendGetCalGain();
    densInterface_->sendGetCalSlope();
    densInterface_->sendGetCalReflection();
    densInterface_->sendGetCalTransmission();
}

void MainWindow::onCalLightSetClicked()
{
    DensCalLight calLight;
    bool ok;

    calLight.setReflectionValue(ui->reflLightLineEdit->text().toInt(&ok));
    if (!ok) { return; }

    calLight.setTransmissionValue(ui->tranLightLineEdit->text().toInt(&ok));
    if (!ok) { return; }

    if (!calLight.isValid()) { return; }

    densInterface_->sendSetCalLight(calLight);
}

void MainWindow::onCalGainCalClicked()
{
    if (remoteDialog_) {
        qWarning() << "Cannot start gain galibration with remote control dialog open";
        return;
    }
    ui->gainCalPushButton->setEnabled(false);

    QMessageBox messageBox;
    messageBox.setWindowTitle(tr("Sensor Gain Calibration"));
    messageBox.setText(tr("Hold the device firmly closed with no film in the optical path."));
    messageBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    messageBox.setDefaultButton(QMessageBox::Ok);

    if (messageBox.exec() == QMessageBox::Ok) {
        GainCalibrationDialog dialog(densInterface_, this);
        dialog.exec();
        if (dialog.success()) {
            densInterface_->sendGetCalLight();
            densInterface_->sendGetCalGain();
        }
    }

    ui->gainCalPushButton->setEnabled(true);
}

void MainWindow::onCalGainSetClicked()
{
    DensCalGain calSlope;
    bool ok;

    calSlope.setLow0(1.0F);
    calSlope.setLow1(1.0F);

    calSlope.setMed0(ui->med0LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calSlope.setMed1(ui->med1LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calSlope.setHigh0(ui->high0LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calSlope.setHigh1(ui->high1LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calSlope.setMax0(ui->max0LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calSlope.setMax1(ui->max1LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    densInterface_->sendSetCalGain(calSlope);
}

void MainWindow::onCalSlopeSetClicked()
{
    DensCalSlope calSlope;
    bool ok;

    calSlope.setB0(ui->b0LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calSlope.setB1(ui->b1LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calSlope.setB2(ui->b2LineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    densInterface_->sendSetCalSlope(calSlope);
}

void MainWindow::onCalReflectionSetClicked()
{
    DensCalTarget calTarget;
    bool ok;

    calTarget.setLoDensity(ui->reflLoDensityLineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calTarget.setLoReading(ui->reflLoReadingLineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calTarget.setHiDensity(ui->reflHiDensityLineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calTarget.setHiReading(ui->reflHiReadingLineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    densInterface_->sendSetCalReflection(calTarget);
}

void MainWindow::onCalTransmissionSetClicked()
{
    DensCalTarget calTarget;
    bool ok;

    calTarget.setLoDensity(0.0F);

    calTarget.setLoReading(ui->tranLoReadingLineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calTarget.setHiDensity(ui->tranHiDensityLineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    calTarget.setHiReading(ui->tranHiReadingLineEdit->text().toFloat(&ok));
    if (!ok) { return; }

    densInterface_->sendSetCalTransmission(calTarget);
}

void MainWindow::onCalLightTextChanged()
{
    if (densInterface_->connected()
            && ui->reflLightLineEdit->hasAcceptableInput()
            && ui->tranLightLineEdit->hasAcceptableInput()) {
        ui->lightSetPushButton->setEnabled(true);
    } else {
        ui->lightSetPushButton->setEnabled(false);
    }

    const DensCalLight calLight = densInterface_->calLight();
    updateLineEditDirtyState(ui->reflLightLineEdit, calLight.reflectionValue());
    updateLineEditDirtyState(ui->tranLightLineEdit, calLight.transmissionValue());
}

void MainWindow::onCalGainTextChanged()
{
    if (densInterface_->connected()
            && !ui->low0LineEdit->text().isEmpty()
            && !ui->low1LineEdit->text().isEmpty()
            && ui->med0LineEdit->hasAcceptableInput()
            && ui->med1LineEdit->hasAcceptableInput()
            && ui->high0LineEdit->hasAcceptableInput()
            && ui->high1LineEdit->hasAcceptableInput()
            && ui->max0LineEdit->hasAcceptableInput()
            && ui->max1LineEdit->hasAcceptableInput()) {
        ui->gainSetPushButton->setEnabled(true);
    } else {
        ui->gainSetPushButton->setEnabled(false);
    }

    const DensCalGain calGain = densInterface_->calGain();
    updateLineEditDirtyState(ui->med0LineEdit, calGain.med0(), 6);
    updateLineEditDirtyState(ui->med1LineEdit, calGain.med1(), 6);
    updateLineEditDirtyState(ui->high0LineEdit, calGain.high0(), 6);
    updateLineEditDirtyState(ui->high1LineEdit, calGain.high1(), 6);
    updateLineEditDirtyState(ui->max0LineEdit, calGain.max0(), 6);
    updateLineEditDirtyState(ui->max1LineEdit, calGain.max1(), 6);
}

void MainWindow::onCalSlopeTextChanged()
{
    if (densInterface_->connected()
            && ui->b0LineEdit->hasAcceptableInput()
            && ui->b1LineEdit->hasAcceptableInput()
            && ui->b2LineEdit->hasAcceptableInput()) {
        ui->slopeSetPushButton->setEnabled(true);
    } else {
        ui->slopeSetPushButton->setEnabled(false);
    }

    const DensCalSlope calSlope = densInterface_->calSlope();
    updateLineEditDirtyState(ui->b0LineEdit, calSlope.b0(), 6);
    updateLineEditDirtyState(ui->b1LineEdit, calSlope.b1(), 6);
    updateLineEditDirtyState(ui->b2LineEdit, calSlope.b2(), 6);
}

void MainWindow::onCalReflectionTextChanged()
{
    if (densInterface_->connected()
            && ui->reflLoDensityLineEdit->hasAcceptableInput()
            && ui->reflLoReadingLineEdit->hasAcceptableInput()
            && ui->reflHiDensityLineEdit->hasAcceptableInput()
            && ui->reflHiReadingLineEdit->hasAcceptableInput()) {
        ui->reflSetPushButton->setEnabled(true);
    } else {
        ui->reflSetPushButton->setEnabled(false);
    }

    const DensCalTarget calTarget = densInterface_->calReflection();
    updateLineEditDirtyState(ui->reflLoDensityLineEdit, calTarget.loDensity(), 2);
    updateLineEditDirtyState(ui->reflLoReadingLineEdit, calTarget.loReading(), 6);
    updateLineEditDirtyState(ui->reflHiDensityLineEdit, calTarget.hiDensity(), 2);
    updateLineEditDirtyState(ui->reflHiReadingLineEdit, calTarget.hiReading(), 6);
}

void MainWindow::onCalTransmissionTextChanged()
{
    if (densInterface_->connected()
            && !ui->tranLoDensityLineEdit->text().isEmpty()
            && ui->tranLoReadingLineEdit->hasAcceptableInput()
            && ui->tranHiDensityLineEdit->hasAcceptableInput()
            && ui->tranHiReadingLineEdit->hasAcceptableInput()) {
        ui->tranSetPushButton->setEnabled(true);
    } else {
        ui->tranSetPushButton->setEnabled(false);
    }

    const DensCalTarget calTarget = densInterface_->calTransmission();
    updateLineEditDirtyState(ui->tranLoReadingLineEdit, calTarget.loReading(), 6);
    updateLineEditDirtyState(ui->tranHiDensityLineEdit, calTarget.hiDensity(), 2);
    updateLineEditDirtyState(ui->tranHiReadingLineEdit, calTarget.hiReading(), 6);
}

void MainWindow::updateLineEditDirtyState(QLineEdit *lineEdit, int value)
{
    if (!lineEdit) { return; }

    if (lineEdit->text().isNull() || lineEdit->text().isEmpty()
            || lineEdit->text() == QString::number(value)) {
        lineEdit->setStyleSheet(styleSheet());
    } else {
        lineEdit->setStyleSheet("QLineEdit { background-color: lightgoldenrodyellow; }");
    }
}

void MainWindow::updateLineEditDirtyState(QLineEdit *lineEdit, float value, int prec)
{
    if (!lineEdit) { return; }

    if (lineEdit->text().isNull() || lineEdit->text().isEmpty()
            || lineEdit->text() == QString::number(value, 'f', prec)) {
        lineEdit->setStyleSheet(styleSheet());
    } else {
        lineEdit->setStyleSheet("QLineEdit { background-color: lightgoldenrodyellow; }");
    }
}

void MainWindow::onSystemVersionResponse()
{
    if (densInterface_->projectName().isEmpty()) {
        ui->nameLabel->setText("Printalyzer Densitometer");
    } else {
        ui->nameLabel->setText(QString("<b>%1</b>").arg(densInterface_->projectName()));
    }
    ui->versionLabel->setText(tr("Version: %1").arg(densInterface_->version()));
}

void MainWindow::onSystemBuildResponse()
{
    ui->buildDateLabel->setText(tr("Date: %1").arg(densInterface_->buildDate().toString("yyyy-MM-dd hh:mm")));
    ui->buildDescribeLabel->setText(tr("Commit: %1").arg(densInterface_->buildDescribe()));
    if (densInterface_->buildChecksum() == 0) {
        ui->checksumLabel->setText(tr("Checksum: %1").arg(""));
    } else {
        ui->checksumLabel->setText(tr("Checksum: %1").arg(densInterface_->buildChecksum(), 0, 16));
    }
}

void MainWindow::onSystemDeviceResponse()
{
    ui->halVersionLabel->setText(tr("HAL Version: %1").arg(densInterface_->halVersion()));
    ui->mcuDevIdLabel->setText(tr("MCU Device ID: %1").arg(densInterface_->mcuDeviceId()));
    ui->mcuRevIdLabel->setText(tr("MCU Revision ID: %1").arg(densInterface_->mcuRevisionId()));
    ui->mcuSysClockLabel->setText(tr("MCU SysClock: %1").arg(densInterface_->mcuSysClock()));
}

void MainWindow::onSystemUniqueId()
{
    ui->uniqueIdLabel->setText(tr("UID: %1").arg(densInterface_->uniqueId()));
}

void MainWindow::onSystemInternalSensors()
{
    ui->mcuVddaLabel->setText(tr("Vdda: %1").arg(densInterface_->mcuVdda()));
    ui->mcuTempLabel->setText(tr("Temperature: %1").arg(densInterface_->mcuTemp()));
}

void MainWindow::onDiagDisplayScreenshot(const QByteArray &data)
{
    qDebug() << "Got screenshot:" << data.size();
    QImage image = QImage::fromData(data, "XBM");
    if (!image.isNull()) {
        image = image.mirrored(true, true);
        image.invertPixels();

        QString fileName = QFileDialog::getSaveFileName(this, tr("Save Screenshot"),
                                   "screenshot.png",
                                   tr("Images (*.png *.jpg)"));
        if (!fileName.isEmpty()) {
            if (image.save(fileName)) {
                qDebug() << "Saved screenshot to:" << fileName;
            } else {
                qDebug() << "Error saving screenshot to:" << fileName;
            }
        }
    }
}

void MainWindow::onCalLightResponse()
{
    const DensCalLight calLight = densInterface_->calLight();

    ui->reflLightLineEdit->setText(QString::number(calLight.reflectionValue()));
    ui->tranLightLineEdit->setText(QString::number(calLight.transmissionValue()));

    onCalLightTextChanged();
}

void MainWindow::onCalGainResponse()
{
    const DensCalGain calGain = densInterface_->calGain();

    ui->low0LineEdit->setText(QString::number(calGain.low0(), 'f'));
    ui->low1LineEdit->setText(QString::number(calGain.low1(), 'f'));

    ui->med0LineEdit->setText(QString::number(calGain.med0(), 'f'));
    ui->med1LineEdit->setText(QString::number(calGain.med1(), 'f'));

    ui->high0LineEdit->setText(QString::number(calGain.high0(), 'f'));
    ui->high1LineEdit->setText(QString::number(calGain.high1(), 'f'));

    ui->max0LineEdit->setText(QString::number(calGain.max0(), 'f'));
    ui->max1LineEdit->setText(QString::number(calGain.max1(), 'f'));

    onCalGainTextChanged();
}

void MainWindow::onCalSlopeResponse()
{
    const DensCalSlope calSlope = densInterface_->calSlope();

    ui->b0LineEdit->setText(QString::number(calSlope.b0(), 'f'));
    ui->b1LineEdit->setText(QString::number(calSlope.b1(), 'f'));
    ui->b2LineEdit->setText(QString::number(calSlope.b2(), 'f'));

    onCalSlopeTextChanged();
}

void MainWindow::onCalReflectionResponse()
{
    const DensCalTarget calReflection = densInterface_->calReflection();

    ui->reflLoDensityLineEdit->setText(QString::number(calReflection.loDensity(), 'f', 2));
    ui->reflLoReadingLineEdit->setText(QString::number(calReflection.loReading(), 'f', 6));
    ui->reflHiDensityLineEdit->setText(QString::number(calReflection.hiDensity(), 'f', 2));
    ui->reflHiReadingLineEdit->setText(QString::number(calReflection.hiReading(), 'f', 6));

    onCalReflectionTextChanged();
}

void MainWindow::onCalTransmissionResponse()
{
    const DensCalTarget calTransmission = densInterface_->calTransmission();

    ui->tranLoDensityLineEdit->setText(QString::number(calTransmission.loDensity(), 'f', 2));
    ui->tranLoReadingLineEdit->setText(QString::number(calTransmission.loReading(), 'f', 6));
    ui->tranHiDensityLineEdit->setText(QString::number(calTransmission.hiDensity(), 'f', 2));
    ui->tranHiReadingLineEdit->setText(QString::number(calTransmission.hiReading(), 'f', 6));

    onCalTransmissionTextChanged();
}

void MainWindow::onRemoteControl()
{
    if (!densInterface_->connected()) {
        return;
    }
    if (remoteDialog_) {
        remoteDialog_->setFocus();
        return;
    }
    remoteDialog_ = new RemoteControlDialog(densInterface_, this);
    connect(remoteDialog_, &QDialog::finished, this, &MainWindow::onRemoteControlFinished);
    remoteDialog_->show();
}

void MainWindow::onRemoteControlFinished()
{
    remoteDialog_->deleteLater();
    remoteDialog_ = nullptr;
}

void MainWindow::onSlopeCalibrationTool()
{
    SlopeCalibrationDialog *dialog = new SlopeCalibrationDialog(densInterface_, this);
    connect(dialog, &QDialog::finished, this, &MainWindow::onSlopeCalibrationToolFinished);
    dialog->show();
}

void MainWindow::onSlopeCalibrationToolFinished(int result)
{
    SlopeCalibrationDialog *dialog = dynamic_cast<SlopeCalibrationDialog *>(sender());
    dialog->deleteLater();

    if (result == QDialog::Accepted) {
        auto result = dialog->calValues();
        ui->b0LineEdit->setText(QString::number(std::get<0>(result), 'f'));
        ui->b1LineEdit->setText(QString::number(std::get<1>(result), 'f'));
        ui->b2LineEdit->setText(QString::number(std::get<2>(result), 'f'));
    }
}
