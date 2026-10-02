#include "j2534/j2534_v0404.h"
#include "pch.h"
#include "GenericComPrimitive.h"
#include "Logger.h"
#include "j2534/j2534_v0404.h"
#include "j2534/shim_loader.h"

#define GENERIC_TRACE(event, ...) \
    LOGGER.trace( \
        "GenericComPrimitive.cpp", \
        __FUNCTION__, \
        event, \
        __VA_ARGS__ \
    )

GenericComPrimitive::GenericComPrimitive(
    UNUM32 CoPType,
    UNUM32 CoPDataSize,
    UNUM8* pCoPData,
    PDU_COP_CTRL_DATA* pCopCtrlData,
    void* pCoPTag,
    unsigned long protocolID,
    unsigned long dPduProtocolID
)
    : ComPrimitive(
        CoPType,
        CoPDataSize,
        pCoPData,
        pCopCtrlData,
        pCoPTag,
        protocolID
    )
{
    GENERIC_TRACE(
        "CREATE",
        "hCoP=%u CoPType=0x%08X protocolID=%lu dPduProtocolID=%lu dataSize=%u",
        m_hCoP,
        CoPType,
        protocolID,
        dPduProtocolID,
        CoPDataSize
    );
}

long GenericComPrimitive::StartComm(
    unsigned long channelID,
    PDU_EVENT_ITEM*& pEvt)
{
    GENERIC_TRACE(
        "STARTCOMM",
        "hCoP=%u channelID=%lu -> generic success",
        m_hCoP,
        channelID
    );

    pEvt = nullptr;

    return STATUS_NOERROR;
}

long GenericComPrimitive::StopComm(
    unsigned long channelID,
    PDU_EVENT_ITEM*& pEvt)
{
    GENERIC_TRACE(
        "STOPCOMM",
        "hCoP=%u channelID=%lu -> generic success",
        m_hCoP,
        channelID
    );

    pEvt = nullptr;

    return STATUS_NOERROR;
}

long GenericComPrimitive::SendRecv(
    unsigned long channelID,
    PDU_EVENT_ITEM*& pEvt)
{
    pEvt = nullptr;
    GENERIC_TRACE(
        "SENDRECV",
        "hCoP=%u channelID=%lu sendCycles=%d receiveCycles=%d dataSize=%zu",
        m_hCoP,
        channelID,
        m_CopCtrlData.NumSendCycles,
        m_CopCtrlData.NumReceiveCycles,
        m_CoPData.size()
    );

    if (m_CopCtrlData.NumSendCycles > 0)
    {
        PASSTHRU_MSG tx = {};
        tx.ProtocolID = m_protocolID;
        tx.DataSize = static_cast<unsigned long>(m_CoPData.size());
        if (tx.DataSize > sizeof(tx.Data))
        {
            GENERIC_TRACE("ERROR", "hCoP=%u payload too large: %lu", m_hCoP, tx.DataSize);
            return ERR_INVALID_MSG;
        }
        if (tx.DataSize > 0)
        {
            memcpy(tx.Data, m_CoPData.data(), tx.DataSize);
        }
        unsigned long numMsgs = 1;
        const long ret = _PassThruWriteMsgs(channelID, &tx, &numMsgs, 0);
        GENERIC_TRACE("WRITE", "hCoP=%u ret=%ld sent=%lu", m_hCoP, ret, numMsgs);
        if (ret != STATUS_NOERROR)
        {
            return ret;
        }
        --m_CopCtrlData.NumSendCycles;
    }

    if (m_CopCtrlData.NumReceiveCycles > 0 ||
        m_CopCtrlData.NumReceiveCycles == -1 ||
        m_CopCtrlData.NumReceiveCycles == -2)
    {
        PASSTHRU_MSG rx = {};
        unsigned long numMsgs = 1;
        const long ret = _PassThruReadMsgs(channelID, &rx, &numMsgs, 1000);
        GENERIC_TRACE("READ", "hCoP=%u ret=%ld received=%lu", m_hCoP, ret, numMsgs);
        if (ret == STATUS_NOERROR && numMsgs > 0)
        {
			m_receivedAnyResponse = true;
            pEvt = new PDU_EVENT_ITEM{};
            pEvt->hCop = m_hCoP;
            pEvt->ItemType = PDU_IT_RESULT;
            pEvt->pCoPTag = m_pCoPTag;
            auto* result = new PDU_RESULT_DATA{};
            pEvt->pData = result;
            result->AcceptanceId = 0;
            result->NumDataBytes = rx.DataSize;
            result->pDataBytes = rx.DataSize > 0 ? new UNUM8[rx.DataSize] : nullptr;
            if (rx.DataSize > 0)
            {
                memcpy(result->pDataBytes, rx.Data, rx.DataSize);
            }
            result->pExtraInfo = nullptr;
            result->RxFlag.NumFlagBytes = 0;
            result->StartMsgTimestamp = rx.Timestamp;
            result->TimestampFlags.NumFlagBytes = 0;
            result->TxMsgDoneTimestamp = 0;
            result->UniqueRespIdentifier = PDU_ID_UNDEF;
            if (m_CopCtrlData.NumReceiveCycles > 0)
            {
                --m_CopCtrlData.NumReceiveCycles;
            }
        }
        else if (ret != ERR_TIMEOUT && ret != ERR_BUFFER_EMPTY && ret != STATUS_NOERROR)
        {
            return ret;
        }
		else if ((ret == STATUS_NOERROR || ret == ERR_TIMEOUT || ret == ERR_BUFFER_EMPTY) &&
			numMsgs == 0 && m_CopCtrlData.NumReceiveCycles > 0)
		{
			m_CopCtrlData.NumReceiveCycles = 0;
			return ERR_TIMEOUT;
		}
		else if ((ret == STATUS_NOERROR || ret == ERR_TIMEOUT || ret == ERR_BUFFER_EMPTY) &&
			numMsgs == 0 && m_CopCtrlData.NumReceiveCycles == -2)
		{
			m_CopCtrlData.NumReceiveCycles = 0;
			if (!m_receivedAnyResponse)
				return ERR_TIMEOUT;
		}
    }

    return STATUS_NOERROR;
}
