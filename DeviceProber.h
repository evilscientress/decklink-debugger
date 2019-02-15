#ifndef __DeviceProber__
#define __DeviceProber__

#include <string>

#include "DeckLinkAPI.h"
#include "CaptureDelegate.h"
#include "util.h"

class DeviceProber
{
public:
	DeviceProber(IDeckLink* deckLink);
	virtual ~DeviceProber();

	virtual std::string GetDeviceName();
	virtual bool        CanAutodetect()  { return m_canAutodetect; }
	virtual bool        CanInput()       { return m_canInput; }

	virtual IDeckLink*         GetIDecklink(void) { return m_deckLink; }
	virtual bool               GetSignalDetected(void);
	virtual bool               IsSubDevice();
	virtual std::string        GetDetectedMode(void);
	virtual BMDPixelFormat     GetPixelFormat(void);
	virtual BMDVideoConnection GetActiveConnection(void);

	virtual void               SelectNextConnection(void);

	virtual IDeckLinkVideoInputFrame* GetLastFrame(void);

private:
	bool                 queryCanAutodetect(void);
	bool                 queryCanInput(void);
	IDeckLinkAttributes* queryAttributesInterface(void);

private:
	IDeckLink*           m_deckLink;
	CaptureDelegate*     m_captureDelegate;
	IDeckLinkAttributes* m_deckLinkAttributes;

	bool                 m_canAutodetect;
	bool                 m_canInput;
};

#endif
