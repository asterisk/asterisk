/* $Id$ */
/* 
 * Copyright (C) 2011-2011 Teluu Inc. (http://www.teluu.com)
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
#include "vidgui.h"
#include "vidwin.h"

#if defined(PJ_WIN32)
#   define SDL_MAIN_HANDLED
#endif

#include <SDL.h>
#include <assert.h>
#include <QMessageBox>

#define LOG_FILE		"vidgui.log"
#define THIS_FILE		"vidgui.cpp"

///////////////////////////////////////////////////////////////////////////
//
// SETTINGS
//

//
// These configure SIP registration
//
#define USE_REGISTRATION	0
#define SIP_DOMAIN		"pjsip.org"
#define SIP_USERNAME		"vidgui"
#define SIP_PASSWORD		"secret"
#define SIP_PORT		5080
#define SIP_TCP			1

//
// NAT helper settings
//
#define USE_ICE			1
#define USE_STUN		0
#define STUN_SRV		"stun.pjsip.org"

//
// Devices settings
//
#define DEFAULT_CAP_DEV		PJMEDIA_VID_DEFAULT_CAPTURE_DEV
//#define DEFAULT_CAP_DEV		1
#define DEFAULT_REND_DEV	PJMEDIA_VID_DEFAULT_RENDER_DEV


//
// End of Settings
///////////////////////////////////////////////////////////////////////////


MainWin *MainWin::theInstance_;

MainWin::MainWin(QWidget *parent)
: QWidget(parent), accountId_(-1), currentCall_(-1),
  preview_on(false), video_(NULL), video_prev_(NULL)
{
    theInstance_ = this;

    initLayout();
    emit signalCallReleased();
}

MainWin::~MainWin()
{
    quit();
    theInstance_ = NULL;
}

MainWin *MainWin::instance()
{
    return theInstance_;
}

void MainWin::initLayout()
{
    //statusBar_ = new QStatusBar(this);

    /* main layout */
    QHBoxLayout *hbox_main = new QHBoxLayout;
    //QVBoxLayout *vbox_left = new QVBoxLayout;
    vbox_left = new QVBoxLayout;
    QVBoxLayout *vbox_right = new QVBoxLayout;
    hbox_main->addLayout(vbox_left);
    hbox_main->addLayout(vbox_right);

    /* Left pane */
    QHBoxLayout *hbox_url = new QHBoxLayout;
    hbox_url->addWidget(new QLabel(tr("Url:")));
    hbox_url->addWidget(url_=new QLineEdit(tr("sip:")), 1);
    vbox_left->addLayout(hbox_url);

    /* Right pane */
    vbox_right->addWidget((localUri_ = new QLabel));
    vbox_right->addWidget((vidEnabled_ = new QCheckBox(tr("Enable &video"))));
    vbox_right->addWidget((previewButton_=new QPushButton(tr("Start &Preview"))));
    vbox_right->addWidget((callButton_=new QPushButton(tr("Call"))));
    vbox_right->addWidget((hangupButton_=new QPushButton(tr("Hangup"))));
    vbox_right->addWidget((quitButton_=new QPushButton(tr("Quit"))));

#if PJMEDIA_HAS_VIDEO
    vidEnabled_->setCheckState(Qt::Checked);
#else
    vidEnabled_->setCheckState(Qt::Unchecked);
    vidEnabled_->setEnabled(false);
#endif

    /* Outest layout */
    QVBoxLayout *vbox_outest = new QVBoxLayout;
    vbox_outest->addLayout(hbox_main);
    vbox_outest->addWidget((statusBar_ = new QLabel));

    setLayout(vbox_outest);

    connect(previewButton_, SIGNAL(clicked()), this, SLOT(preview()));
    connect(callButton_, SIGNAL(clicked()), this, SLOT(call()));
    connect(hangupButton_, SIGNAL(clicked()), this, SLOT(hangup()));
    connect(quitButton_, SIGNAL(clicked()), this, SLOT(quit()));
    //connect(this, SIGNAL(close()), this, SLOT(quit()));
    connect(vidEnabled_, SIGNAL(stateChanged(int)), this, SLOT(onVidEnabledChanged(int)));

    // UI updates must be done in the UI thread!
    connect(this, SIGNAL(signalNewCall(int, bool)),
	    this, SLOT(onNewCall(int, bool)));
    connect(this, SIGNAL(signalCallReleased()),
	    this, SLOT(onCallReleased()));
    connect(this, SIGNAL(signalInitVideoWindow()),
	    this, SLOT(initVideoWindow()));
    connect(this, SIGNAL(signalShowStatus(const QString&)),
	    this, SLOT(doShowStatus(const QString&)));
}

void MainWin::quit()
{
    delete video_prev_;
    video_prev_ = NULL;
    delete video_;
    video_ = NULL;

    pjsua_destroy();
    qApp->quit();
}

void MainWin::showStatus(const char *msg)
{
    PJ_LOG(3,(THIS_FILE, "%s", msg));

    QString msg_ = QString::fromUtf8(msg);
    emit signalShowStatus(msg_);
}

void MainWin::doShowStatus(const QString& msg)
{
    //statusBar_->showMessage(msg);
    statusBar_->setText(msg);
}

void MainWin::showError(const char *title, pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];
    char errline[120];

    pj_strerror(status, errmsg, sizeof(errmsg));
    snprintf(errline, sizeof(errline), "%s error: %s", title, errmsg);
    showStatus(errline);
}

void MainWin::onVidEnabledChanged(int state)
{
    pjsua_call_setting call_setting;

    if (currentCall_ == -1)
	return;

    pjsua_call_setting_default(&call_setting);
    call_setting.vid_cnt = (state == Qt::Checked);

    pjsua_call_reinvite2(currentCall_, &call_setting, NULL);
}

void MainWin::onNewCall(int cid, bool incoming)
{
    pjsua_call_info ci;

    pj_assert(currentCall_ == -1);
    currentCall_ = cid;

    pjsua_call_get_info(cid, &ci);
    url_->setText(ci.remote_info.ptr);
    url_->setEnabled(false);
    hangupButton_->setEnabled(true);

    if (incoming) {
	callButton_->setText(tr("Answer"));
	callButton_->setEnabled(true);
    } else {
	callButton_->setEnabled(false);
    }

    //video_->setText(ci.remote_contact.ptr);
    //video_->setWindowTitle(ci.remote_contact.ptr);
}

void MainWin::onCallReleased()
{
    url_->setEnabled(true);
    callButton_->setEnabled(true);
    callButton_->setText(tr("Call"));
    hangupButton_->setEnabled(false);
    currentCall_ = -1;

    delete video_;
    video_ = NULL;
}

void MainWin::preview()
{
    if (preview_on) {
	delete video_prev_;
	video_prev_ = NULL;

	pjsua_vid_preview_stop(DEFAULT_CAP_DEV);

	showStatus("Preview stopped");
	previewButton_->setText(tr("Start &Preview"));
    } else {
	pjsua_vid_win_id wid;
	pjsua_vid_win_info wi;
	pjsua_vid_preview_param pre_param;
	pj_status_t status;

	pjsua_vid_preview_param_default(&pre_param);
	pre_param.rend_id = DEFAULT_REND_DEV;
	pre_param.show = PJ_FALSE;

	status = pjsua_vid_preview_start(DEFAULT_CAP_DEV, &pre_param);
	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    pj_strerror(status, errmsg, sizeof(errmsg));
	    QMessageBox::critical(0, "Error creating preview", errmsg);
	    return;
	}
	wid = pjsua_vid_preview_get_win(DEFAULT_CAP_DEV);
	pjsua_vid_win_get_info(wid, &wi);

	video_prev_ = new VidWin(&wi.hwnd);
        video_prev_->putIntoLayout(vbox_left);
	//Using this will cause SDL window to display blank
	//screen sometimes, probably because it's using different
	//X11 Display
	//status = pjsua_vid_win_set_show(wid, PJ_TRUE);
	//This is handled by VidWin now
	//video_prev_->show_sdl();
	showStatus("Preview started");

	previewButton_->setText(tr("Stop &Preview"));
    }
    preview_on = !preview_on;
}


void MainWin::call()
{
    if (callButton_->text() == "Answer") {
	pjsua_call_setting call_setting;

	pj_assert(currentCall_ != -1);

	pjsua_call_setting_default(&call_setting);
	call_setting.vid_cnt = (vidEnabled_->checkState()==Qt::Checked);

	pjsua_call_answer2(currentCall_, &call_setting, 200, NULL, NULL);
	callButton_->setEnabled(false);
    } else {
	pj_status_t status;
	QString dst = url_->text();
	char uri[256];
	pjsua_call_setting call_setting;

	pj_ansi_strncpy(uri, dst.toAscii().data(), sizeof(uri));
	pj_str_t uri2 = pj_str((char*)uri);

	pj_assert(currentCall_ == -1);

	pjsua_call_setting_default(&call_setting);
	call_setting.vid_cnt = (vidEnabled_->checkState()==Qt::Checked);

	status = pjsua_call_make_call(accountId_, &uri2, &call_setting,
				      NULL, NULL, &currentCall_);
	if (status != PJ_SUCCESS) {
	    showError("make call", status);
	    return;
	}
    }
}

void MainWin::hangup()
{
    pj_assert(currentCall_ != -1);
    //pjsua_call_hangup(currentCall_, PJSIP_SC_BUSY_HERE, NULL, NULL);
    pjsua_call_hangup_all();
    emit signalCallReleased();
}


void MainWin::initVideoWindow()
{
    pjsua_call_info ci;
    unsigned i;

    if (currentCall_ == -1)
	return;

    delete video_;
    video_ = NULL;

    pjsua_call_get_info(currentCall_, &ci);
    for (i = 0; i < ci.media_cnt; ++i) {
	if ((ci.media[i].type == PJMEDIA_TYPE_VIDEO) &&
	    (ci.media[i].dir & PJMEDIA_DIR_DECODING))
	{
	    pjsua_vid_win_info wi;
	    pjsua_vid_win_get_info(ci.media[i].stream.vid.win_in, &wi);

	    video_= new VidWin(&wi.hwnd);
            video_->putIntoLayout(vbox_left);

	    break;
	}
    }
}

void MainWin::on_reg_state(pjsua_acc_id acc_id)
{
    pjsua_acc_info info;

    pjsua_acc_get_info(acc_id, &info);

    char reg_status[80];
    char status[120];

    if (!info.has_registration) {
	pj_ansi_snprintf(reg_status, sizeof(reg_status), "%.*s",
			 (int)info.status_text.slen,
			 info.status_text.ptr);

    } else {
	pj_ansi_snprintf(reg_status, sizeof(reg_status),
			 "%d/%.*s (expires=%d)",
			 info.status,
			 (int)info.status_text.slen,
			 info.status_text.ptr,
			 info.expires);

    }

    snprintf(status, sizeof(status),
	     "%.*s: %s\n",
	     (int)info.acc_uri.slen, info.acc_uri.ptr,
	     reg_status);
    showStatus(status);
}

void MainWin::on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
    pjsua_call_info ci;

    PJ_UNUSED_ARG(e);

    pjsua_call_get_info(call_id, &ci);

    if (currentCall_ == -1 && ci.state < PJSIP_INV_STATE_DISCONNECTED &&
	ci.role == PJSIP_ROLE_UAC)
    {
	emit signalNewCall(call_id, false);
    }

    char status[80];
    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
	snprintf(status, sizeof(status), "Call is %s (%s)",
	         ci.state_text.ptr,
	         ci.last_status_text.ptr);
	showStatus(status);
	emit signalCallReleased();
    } else {
	snprintf(status, sizeof(status), "Call is %s", pjsip_inv_state_name(ci.state));
	showStatus(status);
    }
}

void MainWin::on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
                               pjsip_rx_data *rdata)
{
    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(rdata);

    if (currentCall_ != -1) {
	pjsua_call_answer(call_id, PJSIP_SC_BUSY_HERE, NULL, NULL);
	return;
    }

    emit signalNewCall(call_id, true);

    pjsua_call_info ci;
    char status[80];

    pjsua_call_get_info(call_id, &ci);
    snprintf(status, sizeof(status), "Incoming call from %.*s",
             (int)ci.remote_info.slen, ci.remote_info.ptr);
    showStatus(status);
}

void MainWin::on_call_media_state(pjsua_call_id call_id)
{
    pjsua_call_info ci;

    pjsua_call_get_info(call_id, &ci);

    for (unsigned i=0; i<ci.media_cnt; ++i) {
	if (ci.media[i].type == PJMEDIA_TYPE_AUDIO) {
	    switch (ci.media[i].status) {
	    case PJSUA_CALL_MEDIA_ACTIVE:
		pjsua_conf_connect(ci.media[i].stream.aud.conf_slot, 0);
		pjsua_conf_connect(0, ci.media[i].stream.aud.conf_slot);
		break;
	    default:
		break;
	    }
	} else if (ci.media[i].type == PJMEDIA_TYPE_VIDEO) {
	    emit signalInitVideoWindow();
	}
    }
}

//
// pjsua callbacks
//
static void on_reg_state(pjsua_acc_id acc_id)
{
    MainWin::instance()->on_reg_state(acc_id);
}

static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
    MainWin::instance()->on_call_state(call_id, e);
}

static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
                             pjsip_rx_data *rdata)
{
    MainWin::instance()->on_incoming_call(acc_id, call_id, rdata);
}

static void on_call_media_state(pjsua_call_id call_id)
{
    MainWin::instance()->on_call_media_state(call_id);
}

//
// initStack()
//
bool MainWin::initStack()
{
    pj_status_t status;

    //showStatus("Creating stack..");
    status = pjsua_create();
    if (status != PJ_SUCCESS) {
	showError("pjsua_create", status);
	return false;
    }

    showStatus("Initializing stack..");

    pjsua_config ua_cfg;
    pjsua_config_default(&ua_cfg);
    pjsua_callback ua_cb;
    pj_bzero(&ua_cb, sizeof(ua_cb));
    ua_cfg.cb.on_reg_state = &::on_reg_state;
    ua_cfg.cb.on_call_state = &::on_call_state;
    ua_cfg.cb.on_incoming_call = &::on_incoming_call;
    ua_cfg.cb.on_call_media_state = &::on_call_media_state;
#if USE_STUN
    ua_cfg.stun_srv_cnt = 1;
    ua_cfg.stun_srv[0] = pj_str((char*)STUN_SRV);
#endif

    pjsua_logging_config log_cfg;
    pjsua_logging_config_default(&log_cfg);
    log_cfg.log_filename = pj_str((char*)LOG_FILE);

    pjsua_media_config med_cfg;
    pjsua_media_config_default(&med_cfg);
    med_cfg.enable_ice = USE_ICE;

    status = pjsua_init(&ua_cfg, &log_cfg, &med_cfg);
    if (status != PJ_SUCCESS) {
	showError("pjsua_init", status);
	goto on_error;
    }

    //
    // Create UDP and TCP transports
    //
    pjsua_transport_config udp_cfg;
    pjsua_transport_id udp_id;
    pjsua_transport_config_default(&udp_cfg);
    udp_cfg.port = SIP_PORT;

    status = pjsua_transport_create(PJSIP_TRANSPORT_UDP,
                                    &udp_cfg, &udp_id);
    if (status != PJ_SUCCESS) {
	showError("UDP transport creation", status);
	goto on_error;
    }

    pjsua_transport_info udp_info;
    status = pjsua_transport_get_info(udp_id, &udp_info);
    if (status != PJ_SUCCESS) {
	showError("UDP transport info", status);
	goto on_error;
    }

#if SIP_TCP
    pjsua_transport_config tcp_cfg;
    pjsua_transport_config_default(&tcp_cfg);
    tcp_cfg.port = 0;

    status = pjsua_transport_create(PJSIP_TRANSPORT_TCP,
                                    &tcp_cfg, NULL);
    if (status != PJ_SUCCESS) {
	showError("TCP transport creation", status);
	goto on_error;
    }
#endif

    //
    // Create account
    //
    pjsua_acc_config acc_cfg;
    pjsua_acc_config_default(&acc_cfg);
#if USE_REGISTRATION
    acc_cfg.id = pj_str( (char*)"<sip:" SIP_USERNAME "@" SIP_DOMAIN ">");
    acc_cfg.reg_uri = pj_str((char*) ("sip:" SIP_DOMAIN));
    acc_cfg.cred_count = 1;
    acc_cfg.cred_info[0].realm = pj_str((char*)"*");
    acc_cfg.cred_info[0].scheme = pj_str((char*)"digest");
    acc_cfg.cred_info[0].username = pj_str((char*)SIP_USERNAME);
    acc_cfg.cred_info[0].data = pj_str((char*)SIP_PASSWORD);

# if SIP_TCP
    acc_cfg.proxy[acc_cfg.proxy_cnt++] = pj_str((char*) "<sip:" SIP_DOMAIN ";transport=tcp>");
# endif

#else
    char sip_id[80];
    snprintf(sip_id, sizeof(sip_id),
	     "sip:%s@%.*s:%u", SIP_USERNAME,
	     (int)udp_info.local_name.host.slen,
	     udp_info.local_name.host.ptr,
	     udp_info.local_name.port);
    acc_cfg.id = pj_str(sip_id);
#endif

    acc_cfg.vid_cap_dev = DEFAULT_CAP_DEV;
    acc_cfg.vid_rend_dev = DEFAULT_REND_DEV;
    acc_cfg.vid_in_auto_show = PJ_TRUE;
    acc_cfg.vid_out_auto_transmit = PJ_TRUE;

    status = pjsua_acc_add(&acc_cfg, PJ_TRUE, &accountId_);
    if (status != PJ_SUCCESS) {
	showError("Account creation", status);
	goto on_error;
    }

    localUri_->setText(acc_cfg.id.ptr);

    //
    // Start pjsua!
    //
    showStatus("Starting stack..");
    status = pjsua_start();
    if (status != PJ_SUCCESS) {
	showError("pjsua_start", status);
	goto on_error;
    }

    showStatus("Ready");

    return true;

on_error:
    pjsua_destroy();
    return false;
}

/*
 * A simple registrar, invoked by default_mod_on_rx_request()
 */
static void simple_registrar(pjsip_rx_data *rdata)
{
    pjsip_tx_data *tdata;
    const pjsip_expires_hdr *exp;
    const pjsip_hdr *h;
    unsigned cnt = 0;
    pjsip_generic_string_hdr *srv;
    pj_status_t status;

    status = pjsip_endpt_create_response(pjsua_get_pjsip_endpt(),
				 rdata, 200, NULL, &tdata);
    if (status != PJ_SUCCESS)
	return;

    exp = (pjsip_expires_hdr*)
	  pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL);

    h = rdata->msg_info.msg->hdr.next;
    while (h != &rdata->msg_info.msg->hdr) {
	if (h->type == PJSIP_H_CONTACT) {
	    const pjsip_contact_hdr *c = (const pjsip_contact_hdr*)h;
	    int e = c->expires;

	    if (e < 0) {
		if (exp)
		    e = exp->ivalue;
		else
		    e = 3600;
	    }

	    if (e > 0) {
		pjsip_contact_hdr *nc = (pjsip_contact_hdr*)
					pjsip_hdr_clone(tdata->pool, h);
		nc->expires = e;
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)nc);
		++cnt;
	    }
	}
	h = h->next;
    }

    srv = pjsip_generic_string_hdr_create(tdata->pool, NULL, NULL);
    srv->name = pj_str((char*)"Server");
    srv->hvalue = pj_str((char*)"pjsua simple registrar");
    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)srv);

    pjsip_endpt_send_response2(pjsua_get_pjsip_endpt(),
                               rdata, tdata, NULL, NULL);
}

/* Notification on incoming request */
static pj_bool_t default_mod_on_rx_request(pjsip_rx_data *rdata)
{
    /* Simple registrar */
    if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method,
                         &pjsip_register_method) == 0)
    {
	simple_registrar(rdata);
	return PJ_TRUE;
    }

    return PJ_FALSE;
}

/* The module instance. */
static pjsip_module mod_default_handler =
{
    NULL, NULL,				/* prev, next.		*/
    { (char*)"mod-default-handler", 19 },	/* Name.		*/
    -1,					/* Id			*/
    PJSIP_MOD_PRIORITY_APPLICATION+99,	/* Priority	        */
    NULL,				/* load()		*/
    NULL,				/* start()		*/
    NULL,				/* stop()		*/
    NULL,				/* unload()		*/
    &default_mod_on_rx_request,		/* on_rx_request()	*/
    NULL,				/* on_rx_response()	*/
    NULL,				/* on_tx_request.	*/
    NULL,				/* on_tx_response()	*/
    NULL,				/* on_tsx_state()	*/

};

int main(int argc, char *argv[])
{
    /* At least on Linux, we have to initialize SDL video subsystem prior to
     * creating/initializing QApplication, otherwise we'll segfault miserably
     * in SDL_CreateWindow(). Here's a stack trace if you're interested:

	Thread [7] (Suspended: Signal 'SIGSEGV' received. Description: Segmentation fault.)
	13 XCreateIC()
	12 SetupWindowData()
	11 X11_CreateWindow()
	10 SDL_CreateWindow()
	..
     */
    if ( SDL_InitSubSystem(SDL_INIT_VIDEO) < 0 ) {
        printf("Unable to init SDL: %s\n", SDL_GetError());
        return 1;
    }

    QApplication app(argc, argv);

    MainWin win;
    win.show();

    if (!win.initStack()) {
	win.quit();
	return 1;
    }

    /* We want to be registrar too! */
    if (pjsua_get_pjsip_endpt()) {
	pjsip_endpt_register_module(pjsua_get_pjsip_endpt(),
				    &mod_default_handler);
    }

    return app.exec();
}

