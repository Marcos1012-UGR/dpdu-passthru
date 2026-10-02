#pragma once

#include "ComPrimitive.h"
#include "j2534/j2534_v0404.h"

class GenericComPrimitive : public ComPrimitive
{
public:

    GenericComPrimitive(
        UNUM32 CoPType,
        UNUM32 CoPDataSize,
        UNUM8* pCoPData,
        PDU_COP_CTRL_DATA* pCopCtrlData,
        void* pCoPTag,
        unsigned long protocolID,
        unsigned long dPduProtocolID
    );

    long StartComm(
        unsigned long channelID,
        PDU_EVENT_ITEM*& pEvt
    ) override;

    long StopComm(
        unsigned long channelID,
        PDU_EVENT_ITEM*& pEvt
    ) override;

    long SendRecv(
        unsigned long channelID,
        PDU_EVENT_ITEM*& pEvt
    ) override;

private:
	bool m_receivedAnyResponse = false;
};
