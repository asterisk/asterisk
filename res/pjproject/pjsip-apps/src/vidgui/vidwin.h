/* $Id$ */
/*
 * Copyright (C) 2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef VIDWIN_H
#define VIDWIN_H

#include <pjsua.h>
#include <QWidget>
#include <QBoxLayout>

class VidWin : public QWidget
{
    Q_OBJECT

public:
    VidWin(const pjmedia_vid_dev_hwnd *hwnd,
	   QWidget* parent = 0,
	   Qt::WindowFlags f = 0);
    virtual ~VidWin();
    QSize sizeHint() const { return size_hint; }

    void putIntoLayout(QBoxLayout *layout);

protected:
    virtual bool event(QEvent *e);

private:
    pjmedia_vid_dev_hwnd hwnd;
    void *orig_parent;
    QSize size_hint;

    void attach();
    void detach();
    void set_size();
    void get_size();
    void show_sdl(bool visible=true);
};

#endif

