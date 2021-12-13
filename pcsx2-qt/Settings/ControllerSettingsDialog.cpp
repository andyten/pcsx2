/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2022  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#include "QtHost.h"
#include "Settings/ControllerSettingsDialog.h"
#include "Settings/HotkeySettingsWidget.h"

#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTextEdit>

ControllerSettingsDialog::ControllerSettingsDialog(QWidget* parent /* = nullptr */)
	: QDialog(parent)
{
	m_ui.setupUi(this);

	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	m_hotkey_settings = new HotkeySettingsWidget(m_ui.settingsContainer, this);

	m_ui.settingsContainer->insertWidget(5, m_hotkey_settings);

	m_ui.settingsCategory->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	m_ui.settingsCategory->setCurrentRow(0);
	m_ui.settingsContainer->setCurrentIndex(0);
	connect(m_ui.settingsCategory, &QListWidget::currentRowChanged, this, &ControllerSettingsDialog::onCategoryCurrentRowChanged);
	connect(m_ui.buttonBox, &QDialogButtonBox::rejected, this, &ControllerSettingsDialog::close);
}

ControllerSettingsDialog::~ControllerSettingsDialog() = default;

void ControllerSettingsDialog::setCategory(Category category)
{
	switch (category)
	{
		case Category::GlobalSettings:
			m_ui.settingsContainer->setCurrentIndex(0);
			break;

			// TODO: These will need to take multitap into consideration in the future.
		case Category::FirstControllerSettings:
			m_ui.settingsContainer->setCurrentIndex(1);
			break;

		case Category::FirstMemoryCardSettings:
			m_ui.settingsContainer->setCurrentIndex(3);
			break;

		case Category::HotkeySettings:
			m_ui.settingsContainer->setCurrentIndex(5);
			break;

		default:
			break;
	}
}

void ControllerSettingsDialog::onCategoryCurrentRowChanged(int row)
{
	m_ui.settingsContainer->setCurrentIndex(row);
}

void ControllerSettingsDialog::registerWidgetHelp(QObject* object, QString title, QString recommended_value, QString text)
{
	// construct rich text with formatted description
	QString full_text;
	full_text += "<table width='100%' cellpadding='0' cellspacing='0'><tr><td><strong>";
	full_text += title;
	full_text += "</strong></td><td align='right'><strong>";
	full_text += tr("Recommended Value");
	full_text += ": </strong>";
	full_text += recommended_value;
	full_text += "</td></table><hr>";
	full_text += text;

	m_widget_help_text_map[object] = std::move(full_text);
	object->installEventFilter(this);
}

bool ControllerSettingsDialog::eventFilter(QObject* object, QEvent* event)
{
	if (event->type() == QEvent::Enter)
	{
		auto iter = m_widget_help_text_map.constFind(object);
		if (iter != m_widget_help_text_map.end())
		{
			m_current_help_widget = object;
			m_ui.helpText->setText(iter.value());
		}
	}
	else if (event->type() == QEvent::Leave)
	{
		if (m_current_help_widget)
		{
			m_current_help_widget = nullptr;
			m_ui.helpText->setText(QString());
		}
	}

	return QDialog::eventFilter(object, event);
}
