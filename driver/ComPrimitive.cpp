#include "pch.h"
#include "pdu_api.h"
#include "ComPrimitive.h"
#include "Logger.h"
#include "Settings.h"
#include "j2534/j2534_v0404.h"
#include "j2534/shim_loader.h"

#include <string>
#include <sstream>

#define COP_TRACE(event, ...) LOGGER.trace("ComPrimitive.cpp", __FUNCTION__, event, __VA_ARGS__)

UNUM32 ComPrimitive::m_hCoPCtr = 0;
UNUM8 ComPrimitive::m_destAddr = 0;

ComPrimitive::ComPrimitive(UNUM32 CoPType, UNUM32 CoPDataSize, UNUM8* pCoPData, PDU_COP_CTRL_DATA* pCopCtrlData,
                           void* pCoPTag, unsigned long protocolID) :
	m_state(PDU_COPST_IDLE), m_CoPType(CoPType), m_pCoPTag(pCoPTag), m_protocolID(protocolID)
{
	COP_TRACE("ENTER", "CoPType=%u CoPDataSize=%u pCoPData=%p pCopCtrlData=%p pCoPTag=%p protocolID=%lu", CoPType, CoPDataSize, pCoPData, pCopCtrlData, pCoPTag, protocolID);
	if (CoPDataSize > 0 && pCoPData != nullptr)
	{
		m_CoPData.assign(pCoPData, pCoPData + CoPDataSize);
	}
	if (pCopCtrlData != nullptr)
	{
		m_CopCtrlData = *pCopCtrlData;
	}
	else
	{
		m_CopCtrlData = {};
	}

	if (m_hCoPCtr == 0) //hcop 0 is invalid
	{
		++m_hCoPCtr;
	}
	m_hCoP = m_hCoPCtr++;
	COP_TRACE("STATE", "hCoP=%u type=%u state=0x%x dataSize=%zu protocolID=%lu", m_hCoP, m_CoPType, static_cast<unsigned int>(m_state), m_CoPData.size(), m_protocolID);
	COP_TRACE("EXIT", "hCoP=%u", m_hCoP);

}

UNUM32 ComPrimitive::getHandle()
{
	return m_hCoP;
}

UNUM32 ComPrimitive::getType()
{
	return m_CoPType;
}

UNUM32 ComPrimitive::getTime() const
{
	return m_CopCtrlData.Time;
}

void* ComPrimitive::getTag() const
{
	return m_pCoPTag;
}

void ComPrimitive::Execute(PDU_EVENT_ITEM*& pEvt)
{
	COP_TRACE("ENTER", "hCoP=%u state=0x%x eventOut=%p", m_hCoP, static_cast<unsigned int>(m_state), &pEvt);
	if (m_state != PDU_COPST_EXECUTING)
	{
		m_state = PDU_COPST_EXECUTING;
		COP_TRACE("STATE", "hCoP=%u state=0x%x", m_hCoP, static_cast<unsigned int>(m_state));
		GenerateStatusEvent(pEvt);
	}
	COP_TRACE("EXIT", "event=%p", pEvt);
}

T_PDU_STATUS ComPrimitive::GetStatus()
{
	COP_TRACE("STATUS", "hCoP=%u state=0x%x", m_hCoP, static_cast<unsigned int>(m_state));
	return m_state;
}

void ComPrimitive::Cancel(PDU_EVENT_ITEM*& pEvt)
{
	COP_TRACE("ENTER", "hCoP=%u state=0x%x eventOut=%p", m_hCoP, static_cast<unsigned int>(m_state), &pEvt);
	if (m_state != PDU_COPST_CANCELLED)
	{
		m_state = PDU_COPST_CANCELLED;
		COP_TRACE("STATE", "hCoP=%u state=0x%x", m_hCoP, static_cast<unsigned int>(m_state));
		GenerateStatusEvent(pEvt);
	}
	COP_TRACE("EXIT", "event=%p", pEvt);
}

void ComPrimitive::Destroy()
{
	COP_TRACE("ENTER", "hCoP=%u", m_hCoP);
	m_hCoP = 0;
	COP_TRACE("EXIT", "hCoP=%u", m_hCoP);
}

void ComPrimitive::Finish(PDU_EVENT_ITEM*& pEvt, bool forceFinish)
{
	COP_TRACE("ENTER", "hCoP=%u state=0x%x sendCycles=%d receiveCycles=%d force=%s", m_hCoP, static_cast<unsigned int>(m_state), m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles, forceFinish ? "true" : "false");
	if (forceFinish ||
		(m_CopCtrlData.NumSendCycles == 0 && m_CopCtrlData.NumReceiveCycles == 0) ||
		m_CoPType == PDU_COPT_UPDATEPARAM ||
		m_CoPType == PDU_COPT_RESTORE_PARAM ||
		m_CoPType == PDU_COPT_DELAY)
	{
		if (m_state != PDU_COPST_FINISHED)
		{
			m_state = PDU_COPST_FINISHED;
			COP_TRACE("STATE", "hCoP=%u state=0x%x", m_hCoP, static_cast<unsigned int>(m_state));
			GenerateStatusEvent(pEvt);
		}
	COP_TRACE("EXIT", "event=%p state=0x%x", pEvt, static_cast<unsigned int>(m_state));
	}
}

void ComPrimitive::GenerateStatusEvent(PDU_EVENT_ITEM*& pEvt)
{
	COP_TRACE("ENTER", "hCoP=%u state=0x%x", m_hCoP, static_cast<unsigned int>(m_state));
	pEvt = new PDU_EVENT_ITEM;
	pEvt->hCop = m_hCoP;
	pEvt->ItemType = PDU_IT_STATUS;
	pEvt->pCoPTag = m_pCoPTag;
	pEvt->pData = new PDU_STATUS_DATA;
	*(PDU_STATUS_DATA*)(pEvt->pData) = m_state;
	COP_TRACE("EXIT", "event=%p ItemType=%u state=0x%x", pEvt, pEvt->ItemType, static_cast<unsigned int>(m_state));
}

#undef COP_TRACE
