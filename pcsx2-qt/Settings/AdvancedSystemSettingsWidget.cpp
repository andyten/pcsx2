#include "PrecompiledHeader.h"

#include <QtWidgets/QMessageBox>
#include <algorithm>

#include "AdvancedSystemSettingsWidget.h"
#include "EmuThread.h"
#include "QtUtils.h"
#include "SettingWidgetBinder.h"
#include "SettingsDialog.h"

AdvancedSystemSettingsWidget::AdvancedSystemSettingsWidget(QWidget* parent, SettingsDialog* dialog)
  : QWidget(parent)
{
  m_ui.setupUi(this);

  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.eeRecompiler, "EmuCore/CPU/Recompiler", "EnableEE", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.eeCache, "EmuCore/CPU/Recompiler", "EnableEECache", false);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.eeINTCSpinDetection, "EmuCore/Speedhacks", "IntcStat", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.eeWaitLoopDetection, "EmuCore/Speedhacks", "WaitLoop", true);

  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.vu0Recompiler, "EmuCore/CPU/Recompiler", "EnableVU0", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.vu1Recompiler, "EmuCore/CPU/Recompiler", "EnableVU1", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.vuFlagHack, "EmuCore/Speedhacks", "vuFlagHack", true);

  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.iopRecompiler, "EmuCore/CPU/Recompiler", "EnableIOP", true);

  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.gameFixes, "", "EnableGameFixes", true);
  SettingWidgetBinder::BindWidgetToBoolSetting(m_ui.patches, "EmuCore", "EnablePatches", true);

  SettingWidgetBinder::BindWidgetToFloatSetting(m_ui.ntscFrameRate, "EmuCore/GS", "FramerateNTSC", 59.94f);
  SettingWidgetBinder::BindWidgetToFloatSetting(m_ui.palFrameRate, "EmuCore/GS", "FrameratePAL", 50.00f);
}

AdvancedSystemSettingsWidget::~AdvancedSystemSettingsWidget() = default;
