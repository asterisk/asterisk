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
#include "vidwin.h"
#include <QEvent>

#define THIS_FILE	"vidwin.cpp"
#define TRACE_(...)	PJ_LOG(4,(THIS_FILE, __VA_ARGS__))

VidWin::VidWin(const pjmedia_vid_dev_hwnd *hwnd_,
	       QWidget* parent,
	       Qt::WindowFlags f) :
    QWidget(parent, f), orig_parent(NULL),
    size_hint(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)
{
    setAttribute(Qt::WA_NativeWindow);

    /* Make this widget a bit "lighter" */
    setAttribute(Qt::WA_UpdatesDisabled);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_PaintOutsidePaintEvent);
    setUpdatesEnabled(false);

    pj_bzero(&hwnd, sizeof(hwnd));
    if (hwnd_) {
	hwnd = *hwnd_;
    }
}


VidWin::~VidWin()
{
    show_sdl(false);
    detach();
}

void VidWin::putIntoLayout(QBoxLayout *box)
{
    box->addWidget(this, 1);
    show();
    activateWindow();
}

bool VidWin::event(QEvent *e)
{
    switch(e->type()) {

    case QEvent::Resize:
	set_size();
	break;

    case QEvent::ParentChange:
	get_size();
	if (0) {
	    QRect qr = rect();
	    if (qr.width() > size_hint.width())
		size_hint.setWidth(qr.width());
	    if (qr.height() > size_hint.height())
		size_hint.setWidth(qr.height());
	}
	setFixedSize(size_hint);
	attach();
	break;

    case QEvent::Show:
	show_sdl(true);
	// revert to default size hint, make it resizable
	setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
	break;

    case QEvent::Hide:
	show_sdl(false);
	break;

    default:
	break;
    }

    return QWidget::event(e);
}

/* Platform specific code */

#if defined(_WIN32) && !defined(_WIN32_WINCE)

#include <windows.h>

void VidWin::attach()
{
    if (!hwnd.info.win.hwnd) return;

    HWND w = (HWND)hwnd.info.win.hwnd;
    HWND new_parent = (HWND)winId();
    orig_parent = GetParent(w);

    //SetWindowLong(w, GWL_STYLE, WS_CHILD);
    SetParent(w, new_parent);
    TRACE_("%p new parent handle = %p", w, new_parent);
}

void VidWin::detach()
{
    if (!hwnd.info.win.hwnd) return;

    HWND w = (HWND)hwnd.info.win.hwnd;
    SetParent(w, (HWND)orig_parent);
    TRACE_("%p revert parent handle to %p", w, orig_parent);
}

void VidWin::set_size()
{
    if (!hwnd.info.win.hwnd) return;

    HWND w = (HWND)hwnd.info.win.hwnd;
    QRect qr = rect();
    UINT swp_flag = SWP_NOACTIVATE;
    SetWindowPos(w, HWND_TOP, 0, 0, qr.width(), qr.height(), swp_flag);
    TRACE_("%p new size = %dx%d", w, qr.width(), qr.height());
}

void VidWin::get_size()
{
    if (!hwnd.info.win.hwnd) return;

    HWND w = (HWND)hwnd.info.win.hwnd;
    RECT r;
    if (GetWindowRect(w, &r))
	size_hint = QSize(r.right-r.left+1, r.bottom-r.top+1);
    TRACE_("%p size = %dx%d", w, size_hint.width(), size_hint.height());
}

void VidWin::show_sdl(bool visible)
{
    if (!hwnd.info.win.hwnd) return;

    HWND w = (HWND)hwnd.info.win.hwnd;
    ShowWindow(w, visible ? SW_SHOW : SW_HIDE);
}

#elif defined(__APPLE__)

#import<Cocoa/Cocoa.h>

void VidWin::attach()
{
    if (!hwnd.info.cocoa.window) return;

    /* Embed hwnd to widget */
    NSWindow *w = (NSWindow*)hwnd.info.cocoa.window;
    NSWindow *parent = [(NSView*)winId() window];
    orig_parent = [w parentWindow];

    //[w setStyleMask:NSBorderlessWindowMask];

    //Can't use this, as sometime the video window may not get reparented.
    //[w setParentWindow:parent];

    [parent addChildWindow:w ordered:NSWindowAbove];
    TRACE_("%p new parent handle = %p", w, parent);
}


void VidWin::detach()
{
    if (!hwnd.info.cocoa.window) return;

    NSWindow *w = (NSWindow*)hwnd.info.cocoa.window;
    NSWindow *parent = [(NSView*)winId() window];
    [parent removeChildWindow:w]; 
}


void VidWin::set_size()
{
    if (!hwnd.info.cocoa.window) return;

    /* Update position and size */
    NSWindow *w = (NSWindow*)hwnd.info.cocoa.window;
    NSRect r;

    NSView* v = (NSView*)winId();
    r = [v bounds];
    r = [v convertRectToBase:r];
    r.origin = [[v window] convertBaseToScreen:r.origin];

    QRect qr = rect();
    [w setFrame:r display:NO]; 

    TRACE_("%p new size = %dx%d", w, qr.width(), qr.height());
}

void VidWin::get_size()
{
    if (!hwnd.info.cocoa.window) return;

    NSWindow *w = (NSWindow*)hwnd.info.cocoa.window;

    size_hint = QSize(300, 200);

    TRACE_("%p size = %dx%d", 0, size_hint.width(), size_hint.height());
}

void VidWin::show_sdl(bool visible)
{
    if (!hwnd.info.cocoa.window) return;

    NSWindow *w = (NSWindow*)hwnd.info.cocoa.window;

    if (visible) {
        [[w contentView]setHidden:NO];
    } else {
        [[w contentView]setHidden:YES];
    }
}

#elif defined(linux) || defined(__linux)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <QX11Info>
#include <stdio.h>

#define GET_DISPLAY()	QX11Info::display()
//#define GET_DISPLAY()	(Display*)hwnd.info.x11.display

void VidWin::attach()
{
    if (!hwnd.info.x11.window) return;

    /* Embed hwnd to widget */

    // Use Qt X11 display here, using window creator X11 display may cause
    // the window failing to embed to this QWidget.
    //Display *d = (Display*)hwnd.info.x11.display;
    Display *d = GET_DISPLAY();
    Window w = (Window)hwnd.info.x11.window;
    Window parent = (Window)this->winId();
    int err = XReparentWindow(d, w, parent, 0, 0);
    TRACE_("%p new parent handle = %p, err = %d",
	   (void*)w,(void*)parent, err);
}


void VidWin::detach()
{
}


void VidWin::set_size()
{
    if (!hwnd.info.x11.window) return;

    /* Update position and size */
    Display *d = GET_DISPLAY();
    Window w = (Window)hwnd.info.x11.window;
    QRect qr = rect();

    int err = XResizeWindow(d, w, qr.width(), qr.height());
    TRACE_("[%p,%p] new size = %dx%d, err = %d",
	   (void*)d, (void*)w, qr.width(), qr.height(), err);
}

void VidWin::get_size()
{
    if (!hwnd.info.x11.window) return;

    Display *d = GET_DISPLAY();
    Window w = (Window)hwnd.info.x11.window;

    XWindowAttributes attr;
    XGetWindowAttributes(d, w, &attr);
    size_hint = QSize(attr.width, attr.height);
    TRACE_("%p size = %dx%d", w, size_hint.width(), size_hint.height());
}

void VidWin::show_sdl(bool visible)
{
    if (!hwnd.info.x11.window) return;

    Display *d = GET_DISPLAY();
    Window w = (Window)hwnd.info.x11.window;

    if (visible) {
	XMapRaised(d, w);
    } else {
	XUnmapWindow(d, w);
    }

    XFlush(d);
}

#endif

