#pragma once

#include <QtWidgets/QWidget>

#include "ui_GameFixSettingsWidget.h"

class SettingsDialog;

class GameFixSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit GameFixSettingsWidget(QWidget* parent, SettingsDialog* dialog);
  ~GameFixSettingsWidget();

private:
  Ui::GameFixSettingsWidget m_ui;
};
