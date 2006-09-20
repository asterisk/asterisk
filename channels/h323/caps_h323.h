#ifndef __AST_H323CAPS_H
#define __AST_H323CAPS_H

/**This class describes the G.711 codec capability.
 */
class AST_G711Capability : public H323AudioCapability
{
	PCLASSINFO(AST_G711Capability, H323AudioCapability);

public:
	AST_G711Capability(int rx_frames = 125, H323_G711Capability::Mode _mode = H323_G711Capability::muLaw, H323_G711Capability::Speed _speed = H323_G711Capability::At64k);
	virtual PObject *Clone() const;
	virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;
	virtual unsigned GetSubType() const;
	virtual PString GetFormatName() const;

protected:
	H323_G711Capability::Mode mode;
	H323_G711Capability::Speed speed;
};

/**This class describes the G.723.1 codec capability.
 */
class AST_G7231Capability : public H323AudioCapability
{
	PCLASSINFO(AST_G7231Capability, H323AudioCapability);

public:
	AST_G7231Capability(int rx_frames = 7, BOOL annexA = TRUE);
	Comparison Compare(const PObject & obj) const;
	virtual PObject * Clone() const;
	virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;
	virtual unsigned GetSubType() const;
	virtual PString GetFormatName() const;
	virtual BOOL OnSendingPDU(H245_AudioCapability & pdu, unsigned packetSize) const;
	virtual BOOL OnReceivedPDU(const H245_AudioCapability & pdu, unsigned & packetSize);

protected:
	BOOL annexA;
};

/**This class describes the (fake) G729 codec capability.
 */
class AST_G729Capability : public H323AudioCapability
{
	PCLASSINFO(AST_G729Capability, H323AudioCapability);

public:
	AST_G729Capability(int rx_frames = 24);
	/* Create a copy of the object. */
	virtual PObject * Clone() const;

	/* Create the codec instance, allocating resources as required. */
	virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;

	/* Get the sub-type of the capability. This is a code dependent on the
	   main type of the capability.

	   This returns one of the four possible combinations of mode and speed
	   using the enum values of the protocol ASN H245_AudioCapability class. */
	virtual unsigned GetSubType() const;

	/* Get the name of the media data format this class represents. */
	virtual PString GetFormatName() const;
};

/* This class describes the VoiceAge G729A codec capability. */
class AST_G729ACapability : public H323AudioCapability
{
	PCLASSINFO(AST_G729ACapability, H323AudioCapability);

public:
	/* Create a new G.729A capability. */
	AST_G729ACapability(int rx_frames = 24);

	/* Create a copy of the object. */
	virtual PObject * Clone() const;
	/* Create the codec instance, allocating resources as required. */
	virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;

	/* Get the sub-type of the capability. This is a code dependent on the
	   main type of the capability.

	   This returns one of the four possible combinations of mode and speed
	   using the enum values of the protocol ASN H245_AudioCapability class. */
	virtual unsigned GetSubType() const;

	/* Get the name of the media data format this class represents. */
	virtual PString GetFormatName() const;
};

/* This class describes the GSM-06.10 codec capability. */
class AST_GSM0610Capability : public H323AudioCapability
{
	PCLASSINFO(AST_GSM0610Capability, H323AudioCapability);

public:
	/* Create a new GSM capability. */
	AST_GSM0610Capability(int rx_frames = 24, int comfortNoise = 0, int scrambled = 0);

	/* Create a copy of the object. */
	virtual PObject * Clone() const;

	/* Create the codec instance, allocating resources as required. */
	virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;

	/* Get the sub-type of the capability. This is a code dependent on the
	   main type of the capability.

	   This returns one of the four possible combinations of mode and speed
	   using the enum values of the protocol ASN H245_AudioCapability class. */
	virtual unsigned GetSubType() const;

	/* Get the name of the media data format this class represents. */
	virtual PString GetFormatName() const;

	BOOL OnSendingPDU(H245_AudioCapability & pdu, unsigned packetSize) const;
	BOOL OnReceivedPDU(const H245_AudioCapability & pdu, unsigned & packetSize);

protected:
	int comfortNoise;
	int scrambled;
};
#endif /* __AST_H323CAPS_H */
