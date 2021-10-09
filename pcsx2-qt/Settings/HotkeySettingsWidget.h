#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QMap>
#include <array>
#include <vector>

class QTabWidget;
class QGridLayout;

class SettingsDialog;

class HotkeySettingsWidget : public QWidget
{
  Q_OBJECT

public:
  HotkeySettingsWidget(QWidget* parent, SettingsDialog* dialog);
  ~HotkeySettingsWidget();

private:
  void createUi();
  void createButtons();

  QTabWidget* m_tab_widget;

  struct Category
  {
    QWidget* container;
    QGridLayout* layout;
  };
  QMap<QString, Category> m_categories;
};
