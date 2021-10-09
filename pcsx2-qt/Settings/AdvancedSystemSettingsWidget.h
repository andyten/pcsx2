#pragma once

#include <QtWidgets/QWidget>

#include "ui_AdvancedSystemSettingsWidget.h"

class SettingsDialog;

class AdvancedSystemSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit AdvancedSystemSettingsWidget(QWidget* parent, SettingsDialog* dialog);
  ~AdvancedSystemSettingsWidget();

private:
  Ui::AdvancedSystemSettingsWidget m_ui;
};
