/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * KDE Console monitor -- Header file
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <kapp.h>
#include <ktmainwindow.h>
#include <qpushbutton.h>
#include <kmenubar.h>
#include <qpopupmenu.h>
#include <qlistbox.h>
#include <qlayout.h>
#include <qframe.h>

class KAsteriskConsole : public KTMainWindow
{
	Q_OBJECT
public:
	KAsteriskConsole();
	void closeEvent(QCloseEvent *);
	QListBox *verbose;
public slots:
	void slotExit();
private:
	void KAsteriskConsole::verboser(char *stuff, int opos, int replacelast, int complete);
	QPushButton *btnExit;
	KMenuBar *menu;
	QPopupMenu *file, *help;
};
