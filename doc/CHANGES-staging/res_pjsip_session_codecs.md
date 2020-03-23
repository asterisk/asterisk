Subject: res_pjsip_session

When placing an outgoing call to a PJSIP endpoint the intent
of any requested formats will now be respected. If only an audio
format is requested (such as ulaw) but the underlying endpoint
does not support the format the resulting SDP will still only
contain an audio stream, and not any additional streams such as
video.
