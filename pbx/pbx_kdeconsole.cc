/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * KDE Console monitor -- Class implmementation
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include "pbx_kdeconsole.moc"

KAsteriskConsole::KAsteriskConsole() : KTMainWindow()
{
	QVBoxLayout *box;
	QFrame *f;
	
	f = new QFrame(this);
	
	setGeometry(100,100,600,400);
	/* Menus */
	file = new QPopupMenu();
	file->insertItem("&Exit", this, SLOT(slotExit()));
	
	help = kapp->getHelpMenu(TRUE, "KDE Asterisk Console\nby Mark Spencer");
	
	setCaption("Asterisk Console");
	
	/* Box */
	box = new QVBoxLayout(f, 20, 5);
	
	/* Menu bar creation */
	menu = new KMenuBar(this);
	menu->insertItem("&File", file);
	menu->insertItem("&Help", help);
	/* Verbose stuff */
	verbose = new QListBox(f, "verbose");
	/* Exit button */
	btnExit = new QPushButton("Exit", f, "exit");
	btnExit->show();
	connect(btnExit,  SIGNAL(clicked()), this, SLOT(slotExit()));
	
	box->addWidget(verbose, 1);
	box->addWidget(btnExit, 0);
	setView(f, TRUE);
	statusBar()->message("Ready", 2000);
}

void KAsteriskConsole::slotExit()
{
	close();
}

void KAsteriskConsole::closeEvent(QCloseEvent *)
{
	kapp->quit();
}

