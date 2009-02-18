#include <ptlib.h>
#include <h323.h>
#include <h245.h>
#include "ast_h323.h"
#include "caps_h323.h"

#define DEFINE_G711_CAPABILITY(cls, code, capName) \
class cls : public AST_G711Capability { \
public: \
	cls() : AST_G711Capability(240, code) { } \
}; \
H323_REGISTER_CAPABILITY(cls, capName) \

DEFINE_G711_CAPABILITY(AST_G711ALaw64Capability, H323_G711Capability::ALaw, OPAL_G711_ALAW_64K);
DEFINE_G711_CAPABILITY(AST_G711uLaw64Capability, H323_G711Capability::muLaw, OPAL_G711_ULAW_64K);
H323_REGISTER_CAPABILITY(AST_G7231Capability, OPAL_G7231);
H323_REGISTER_CAPABILITY(AST_G729Capability,  OPAL_G729);
H323_REGISTER_CAPABILITY(AST_G729ACapability, OPAL_G729A);
H323_REGISTER_CAPABILITY(AST_GSM0610Capability, OPAL_GSM0610);
H323_REGISTER_CAPABILITY(AST_CiscoG726Capability, CISCO_G726r32);
H323_REGISTER_CAPABILITY(AST_CiscoDtmfCapability, CISCO_DTMF_RELAY);

OPAL_MEDIA_FORMAT_DECLARE(OpalG711ALaw64kFormat,
	OPAL_G711_ALAW_64K,
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::PCMA,
	TRUE,	// Needs jitter
	64000,	// bits/sec
	8,		// bytes/frame
	8,		// 1 millisecond/frame
	OpalMediaFormat::AudioTimeUnits,
	0);
OPAL_MEDIA_FORMAT_DECLARE(OpalG711uLaw64kFormat,
	OPAL_G711_ULAW_64K,
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::PCMU,
	TRUE,	// Needs jitter
	64000,	// bits/sec
	8,		// bytes/frame
	8,		// 1 millisecond/frame
	OpalMediaFormat::AudioTimeUnits,
	0);
OPAL_MEDIA_FORMAT_DECLARE(OpalG729Format,
	OPAL_G729,
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::G729,
	TRUE,	// Needs jitter
	8000,	// bits/sec
	10,		// bytes
	80,		// 10 milliseconds
	OpalMediaFormat::AudioTimeUnits,
	0);
OPAL_MEDIA_FORMAT_DECLARE(OpalG729AFormat,
	OPAL_G729 "A",
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::G729,
	TRUE,	// Needs jitter
	8000,	// bits/sec
	10,		// bytes
	80,		// 10 milliseconds
	OpalMediaFormat::AudioTimeUnits,
	0);
OPAL_MEDIA_FORMAT_DECLARE(OpalG7231_6k3Format,
	OPAL_G7231_6k3,
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::G7231,
	TRUE,	// Needs jitter
	6400,	// bits/sec
	24,		// bytes
	240,	// 30 milliseconds
	OpalMediaFormat::AudioTimeUnits,
	0);
OPAL_MEDIA_FORMAT_DECLARE(OpalG7231A_6k3Format,
	OPAL_G7231A_6k3,
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::G7231,
	TRUE,	// Needs jitter
	6400,	// bits/sec
	24,		// bytes
	240,	// 30 milliseconds
	OpalMediaFormat::AudioTimeUnits,
	0);
OPAL_MEDIA_FORMAT_DECLARE(OpalGSM0610Format,
	OPAL_GSM0610,
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::GSM,
	TRUE,	// Needs jitter
	13200,	// bits/sec
	33,		// bytes
	160,	// 20 milliseconds
	OpalMediaFormat::AudioTimeUnits,
	0);
OPAL_MEDIA_FORMAT_DECLARE(OpalCiscoG726Format,
	CISCO_G726r32,
	OpalMediaFormat::DefaultAudioSessionID,
	RTP_DataFrame::G726,
	TRUE,	// Needs jitter
	32000,	// bits/sec
	4,		// bytes
	8,		// 1 millisecond
	OpalMediaFormat::AudioTimeUnits,
	0);
#if 0
OPAL_MEDIA_FORMAT_DECLARE(OpalCiscoDTMFRelayFormat,
	CISCO_DTMF_RELAY,
	OpalMediaFormat::DefaultAudioSessionID,
	(RTP_DataFrame::PayloadTypes)121, // Choose this for Cisco IOS compatibility
	TRUE,	// Needs jitter
	100,	// bits/sec
	4,		// bytes/frame
	8*150,	// 150 millisecond
	OpalMediaFormat::AudioTimeUnits,
	0);
#endif

/*
 * Capability: G.711
 */
AST_G711Capability::AST_G711Capability(int rx_frames, H323_G711Capability::Mode m, H323_G711Capability::Speed s)
	: H323AudioCapability(rx_frames, 30) // 240ms max, 30ms desired
{
	mode = m;
	speed = s;
}


PObject * AST_G711Capability::Clone() const
{
	return new AST_G711Capability(*this);
}


unsigned AST_G711Capability::GetSubType() const
{
	static const unsigned G711SubType[2][2] = {
		{ H245_AudioCapability::e_g711Alaw64k, H245_AudioCapability::e_g711Alaw56k },
		{ H245_AudioCapability::e_g711Ulaw64k, H245_AudioCapability::e_g711Ulaw56k }
	};
	return G711SubType[mode][speed];
}


PString AST_G711Capability::GetFormatName() const
{
	static const char * const G711Name[2][2] = {
		{ OPAL_G711_ALAW_64K, OPAL_G711_ALAW_56K },
		{ OPAL_G711_ULAW_64K, OPAL_G711_ULAW_56K },
	};
	return G711Name[mode][speed];
}


H323Codec * AST_G711Capability::CreateCodec(H323Codec::Direction direction) const
{
	return NULL;
}


/*
 * Capability: G.723.1
 */
AST_G7231Capability::AST_G7231Capability(int rx_frames, PBoolean annexA_)
	: H323AudioCapability(rx_frames, 4)
{
	annexA = annexA_;
}

PObject::Comparison AST_G7231Capability::Compare(const PObject & obj) const
{
	Comparison result = H323AudioCapability::Compare(obj);
	if (result != EqualTo) {
		return result;
	}
	PINDEX otherAnnexA = ((const AST_G7231Capability &)obj).annexA;
	if (annexA < otherAnnexA) {
		return LessThan;
	}
	if (annexA > otherAnnexA) {
		return GreaterThan;
	}
	return EqualTo;
}

PObject * AST_G7231Capability::Clone() const
{
	return new AST_G7231Capability(*this);
}

PString AST_G7231Capability::GetFormatName() const
{
	return (annexA ? OPAL_G7231 "A" : OPAL_G7231);
}

unsigned AST_G7231Capability::GetSubType() const
{
	return H245_AudioCapability::e_g7231;
}

PBoolean AST_G7231Capability::OnSendingPDU(H245_AudioCapability & cap,
										unsigned packetSize) const
{
	cap.SetTag(H245_AudioCapability::e_g7231);
	H245_AudioCapability_g7231 & g7231 = cap;
	g7231.m_maxAl_sduAudioFrames = packetSize;
	g7231.m_silenceSuppression = annexA;
	return TRUE;
}

PBoolean AST_G7231Capability::OnReceivedPDU(const H245_AudioCapability & cap,
										unsigned & packetSize)
{
	if (cap.GetTag() != H245_AudioCapability::e_g7231) {
		return FALSE;
	}
	const H245_AudioCapability_g7231 & g7231 = cap;
	packetSize = g7231.m_maxAl_sduAudioFrames;
	annexA = g7231.m_silenceSuppression;
	return TRUE;
}

H323Codec * AST_G7231Capability::CreateCodec(H323Codec::Direction direction) const
{
	return NULL;
}

/*
 * Capability: G.729
 */
AST_G729Capability::AST_G729Capability(int rx_frames)
	: H323AudioCapability(rx_frames, 2)
{
}

PObject * AST_G729Capability::Clone() const
{
	return new AST_G729Capability(*this);
}

unsigned AST_G729Capability::GetSubType() const
{
	return H245_AudioCapability::e_g729;
}

PString AST_G729Capability::GetFormatName() const
{
	return OPAL_G729;
}

H323Codec * AST_G729Capability::CreateCodec(H323Codec::Direction direction) const
{
	return NULL;
}

/*
 * Capability: G.729A
 */
AST_G729ACapability::AST_G729ACapability(int rx_frames)
	: H323AudioCapability(rx_frames, 6)
{
}

PObject * AST_G729ACapability::Clone() const
{
	return new AST_G729ACapability(*this);
}

unsigned AST_G729ACapability::GetSubType() const
{
	return H245_AudioCapability::e_g729AnnexA;
}

PString AST_G729ACapability::GetFormatName() const
{
	return OPAL_G729A;
}

H323Codec * AST_G729ACapability::CreateCodec(H323Codec::Direction direction) const
{
	return NULL;
}

/*
 * Capability: GSM full rate
 */
AST_GSM0610Capability::AST_GSM0610Capability(int rx_frames, int comfortNoise_, int scrambled_)
	: H323AudioCapability(rx_frames, 2)
{
	comfortNoise = comfortNoise_;
	scrambled = scrambled_;
}

PObject * AST_GSM0610Capability::Clone() const
{
	return new AST_GSM0610Capability(*this);
}

unsigned AST_GSM0610Capability::GetSubType() const
{
	return H245_AudioCapability::e_gsmFullRate;
}

PBoolean AST_GSM0610Capability::OnSendingPDU(H245_AudioCapability & cap,
										unsigned packetSize) const
{
	cap.SetTag(H245_AudioCapability::e_gsmFullRate);
	H245_GSMAudioCapability & gsm = cap;
	gsm.m_audioUnitSize = packetSize * 33;
	gsm.m_comfortNoise = comfortNoise;
	gsm.m_scrambled = scrambled;
	return TRUE;
}

PBoolean AST_GSM0610Capability::OnReceivedPDU(const H245_AudioCapability & cap,
										unsigned & packetSize)
{
	if (cap.GetTag() != H245_AudioCapability::e_gsmFullRate)
		return FALSE;
	const H245_GSMAudioCapability & gsm = cap;
	packetSize = (gsm.m_audioUnitSize + 32) / 33;
	comfortNoise = gsm.m_comfortNoise;
	scrambled = gsm.m_scrambled;

	return TRUE;
}

PString AST_GSM0610Capability::GetFormatName() const
{
	return OPAL_GSM0610;
}

H323Codec * AST_GSM0610Capability::CreateCodec(H323Codec::Direction direction) const
{
	return NULL;
}

/*
 * Capability: G.726 32 Kbps
 */
AST_CiscoG726Capability::AST_CiscoG726Capability(int rx_frames)
	: H323NonStandardAudioCapability(rx_frames, 240,
		181, 0, 18,
		(const BYTE *)"G726r32", 0)
{
}

PObject *AST_CiscoG726Capability::Clone() const
{
	return new AST_CiscoG726Capability(*this);
}

H323Codec *AST_CiscoG726Capability::CreateCodec(H323Codec::Direction direction) const
{
	return NULL;
}

PString AST_CiscoG726Capability::GetFormatName() const
{
	return PString(CISCO_G726r32);
}

/*
 * Capability: Cisco RTP DTMF Relay
 */
AST_CiscoDtmfCapability::AST_CiscoDtmfCapability()
	: H323NonStandardDataCapability(0, 181, 0, 18, (const BYTE *)"RtpDtmfRelay", 0)
{
	rtpPayloadType = (RTP_DataFrame::PayloadTypes)121;
}

PObject *AST_CiscoDtmfCapability::Clone() const
{
	return new AST_CiscoDtmfCapability(*this);
}

H323Codec *AST_CiscoDtmfCapability::CreateCodec(H323Codec::Direction direction) const
{
	return NULL;
}

PString AST_CiscoDtmfCapability::GetFormatName() const
{
	return PString(CISCO_DTMF_RELAY);
}
