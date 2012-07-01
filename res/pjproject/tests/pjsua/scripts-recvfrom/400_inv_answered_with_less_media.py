# $Id$
import inc_sip as sip
import inc_sdp as sdp

# Offer with 2 media lines answered with only 1 media line

pjsua = "--null-audio sip:127.0.0.1:$PORT --id=sip:1000@localhost --extra-audio --use-srtp=0"

sdp = \
"""
v=0
o=- 0 0 IN IP4 127.0.0.1
s=pjmedia
c=IN IP4 127.0.0.1
t=0 0
m=audio 4000 RTP/AVP 0 101
a=rtpmap:0 PCMU/8000
a=sendrecv
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-15
"""

req = sip.RecvfromTransaction("Receiving 2 media lines, answer with 1 media line", 200,
				include=["m=audio \d+ RTP/AVP", "m=audio \d+ RTP/AVP"], 
				exclude=[],
                                resp_hdr=["Content-type: application/sdp"],
                                resp_body=sdp,
			  	)

recvfrom_cfg = sip.RecvfromCfg("Receiving answer with less media lines",
			       pjsua, [req])

