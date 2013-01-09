#ifndef COMPAT_H323_H
#define COMPAT_H323_H

#include "ast_ptlib.h"

#if VERSION(OPENH323_MAJOR,OPENH323_MINOR,OPENH323_BUILD) < VERSION(1,17,3)
/**
 *  Workaround for broken (less than 1.17.3) OpenH323 stack to be able to
 *  make TCP connections from specific address
 */
class MyH323TransportTCP : public H323TransportTCP
{
	PCLASSINFO(MyH323TransportTCP, H323TransportTCP);

public:
	MyH323TransportTCP(
		H323EndPoint & endpoint,    ///<  H323 End Point object
		PIPSocket::Address binding = PIPSocket::GetDefaultIpAny(), ///<  Local interface to use
		PBoolean listen = FALSE         ///<  Flag for need to wait for remote to connect
	);
	/**Connect to the remote party.
	 */
	virtual PBoolean Connect();
};
#else
#define MyH323TransportTCP H323TransportTCP
#endif /* <VERSION(1,17,3) */

class MyH323TransportUDP: public H323TransportUDP
{
	PCLASSINFO(MyH323TransportUDP, H323TransportUDP);

public:
	MyH323TransportUDP(H323EndPoint &endpoint,
		PIPSocket::Address binding = PIPSocket::GetDefaultIpAny(),
		WORD localPort = 0,
		WORD remotePort = 0): H323TransportUDP(endpoint, binding, localPort, remotePort)
	{
	}
	virtual PBoolean DiscoverGatekeeper(H323Gatekeeper &,
		H323RasPDU &,
		const H323TransportAddress &);
protected:
	PDECLARE_NOTIFIER(PThread, MyH323TransportUDP, DiscoverMain);
	H323Gatekeeper *discoverGatekeeper;
	H323RasPDU *discoverPDU;
	const H323TransportAddress *discoverAddress;
	PBoolean discoverResult;
	PBoolean discoverReady;
	PMutex discoverMutex;
};

template <class _Abstract_T, typename _Key_T = PString>
class MyPFactory: public PFactory<_Abstract_T, _Key_T>
{
public:
	template <class _Concrete_T> class Worker: public PFactory<_Abstract_T, _Key_T>::WorkerBase
	{
	public:
		Worker(const _Key_T &_key, bool singleton = false)
			:PFactory<_Abstract_T, _Key_T>::WorkerBase(singleton), key(_key)
		{
			PFactory<_Abstract_T, _Key_T>::Register(key, this);
		}
		~Worker()
		{
			PFactory<_Abstract_T, _Key_T>::Unregister(key);
		}
	protected:
		virtual _Abstract_T *Create(const _Key_T &) const { return new _Concrete_T; }

	private:
		PString key;
    };
};

#ifdef H323_REGISTER_CAPABILITY
#undef H323_REGISTER_CAPABILITY
#endif
#define H323_REGISTER_CAPABILITY(cls, capName) static MyPFactory<H323Capability>::Worker<cls> cls##Factory(capName, true)

#ifdef OPAL_MEDIA_FORMAT_DECLARE
#undef OPAL_MEDIA_FORMAT_DECLARE
#endif

#define OPAL_MEDIA_FORMAT_DECLARE(classname, _fullName, _defaultSessionID, _rtpPayloadType, _needsJitter,_bandwidth, _frameSize, _frameTime, _timeUnits, _timeStamp) \
class classname : public OpalMediaFormat \
{ \
  public: \
    classname() \
      : OpalMediaFormat(_fullName, _defaultSessionID, _rtpPayloadType, _needsJitter, _bandwidth, \
        _frameSize, _frameTime, _timeUnits, _timeStamp){} \
}; \
static MyPFactory<OpalMediaFormat>::Worker<classname> classname##Factory(_fullName, true)

#endif /* !defined AST_H323_H */
