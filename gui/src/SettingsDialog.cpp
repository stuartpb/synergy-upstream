/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2008 Volker Lanz (vl@fidra.de)
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "SettingsDialog.h"

#include <QtCore>
#include <QtGui>

#include "AppConfig.h"

SettingsDialog::SettingsDialog(QWidget* parent, AppConfig& config) :
	QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
	Ui::SettingsDialogBase(),
	m_AppConfig(config)
{
	setupUi(this);

	m_pCheckBoxAutoConnect->setChecked(appConfig().autoConnect());
	m_pLineEditSynergyc->setText(appConfig().synergyc());
	m_pLineEditSynergys->setText(appConfig().synergys());
	m_pLineEditScreenName->setText(appConfig().screenName());
	m_pSpinBoxPort->setValue(appConfig().port());
	m_pLineEditInterface->setText(appConfig().interface());
	m_pComboLogLevel->setCurrentIndex(appConfig().logLevel());
}

QString SettingsDialog::browseForSynergyc(QWidget* parent, const QString& programDir, const QString& synergycName)
{
	return QFileDialog::getOpenFileName(parent, tr("Browse for synergyc executable"),  programDir, synergycName);
}

QString SettingsDialog::browseForSynergys(QWidget* parent, const QString& programDir, const QString& synergysName)
{
	return QFileDialog::getOpenFileName(parent, tr("Browse for synergys executable"),  programDir, synergysName);
}
bool SettingsDialog::on_m_pButtonBrowseSynergys_clicked()
{
	QString fileName = browseForSynergys(this, appConfig().synergyProgramDir(), appConfig().synergysName());

	if (!fileName.isEmpty())
	{
		m_pLineEditSynergys->setText(fileName);
		return true;
	}

	return false;
}

bool SettingsDialog::on_m_pButtonBrowseSynergyc_clicked()
{
	QString fileName = browseForSynergyc(this, appConfig().synergyProgramDir(), appConfig().synergycName());

	if (!fileName.isEmpty())
	{
		m_pLineEditSynergyc->setText(fileName);
		return true;
	}

	return false;
}

void SettingsDialog::accept()
{
	appConfig().setAutoConnect(m_pCheckBoxAutoConnect->isChecked());
	appConfig().setSynergyc(m_pLineEditSynergyc->text());
	appConfig().setSynergys(m_pLineEditSynergys->text());
	appConfig().setScreenName(m_pLineEditScreenName->text());
	appConfig().setPort(m_pSpinBoxPort->value());
	appConfig().setInterface(m_pLineEditInterface->text());
	appConfig().setLogLevel(m_pComboLogLevel->currentIndex());

	QDialog::accept();
}

