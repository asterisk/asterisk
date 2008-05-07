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
AST_G7231Capability::AST_G7231Capability(int rx_frames, BOOL annexA_)
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

BOOL AST_G7231Capability::OnSendingPDU(H245_AudioCapability & cap,
										unsigned packetSize) const
{
	cap.SetTag(H245_AudioCapability::e_g7231);
	H245_AudioCapability_g7231 & g7231 = cap;
	g7231.m_maxAl_sduAudioFrames = packetSize;
	g7231.m_silenceSuppression = annexA;
	return TRUE;
}

BOOL AST_G7231Capability::OnReceivedPDU(const H245_AudioCapability & cap,
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

BOOL AST_GSM0610Capability::OnSendingPDU(H245_AudioCapability & cap,
										unsigned packetSize) const
{
	cap.SetTag(H245_AudioCapability::e_gsmFullRate);
	H245_GSMAudioCapability & gsm = cap;
	gsm.m_audioUnitSize = packetSize * 33;
	gsm.m_comfortNoise = comfortNoise;
	gsm.m_scrambled = scrambled;
	return TRUE;
}

BOOL AST_GSM0610Capability::OnReceivedPDU(const H245_AudioCapability & cap,
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
