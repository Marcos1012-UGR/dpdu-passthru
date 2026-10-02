#include "pch.h"
#include "pdu_api.h"
#include "ISO14230ComPrimitive.h"
#include "Logger.h"
#include "Settings.h"
#include "j2534/j2534_v0404.h"
#include "j2534/shim_loader.h"

#include <string>
#include <sstream>

#define ISO_TRACE(event, ...) LOGGER.trace("ISO14230ComPrimitive.cpp", __FUNCTION__, event, __VA_ARGS__)

constexpr int POLL_TIMEOUT_MS = 10;
constexpr int TIMEOUT_MS = 1000;

const std::vector<UNUM8> MSG_TESTER_PRESENT_41 = { 0x80, 0x41, 0xf1, 0x01, 0x3e, 0xf1 };

long ISO14230ComPrimitive::StartComm(unsigned long channelID, PDU_EVENT_ITEM*& pEvt)
{
	long ret = STATUS_NOERROR;
	ISO_TRACE("ENTER", "channelID=%lu hCoP=%u protocolID=%lu dataSize=%zu sendCycles=%d receiveCycles=%d eventOut=%p", channelID, m_hCoP, m_protocolID, m_CoPData.size(), m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles, &pEvt);

	if (m_CopCtrlData.NumReceiveCycles == 0 || m_CopCtrlData.NumSendCycles == 0)
	{
		ISO_TRACE("RESULT", "no-op because sendCycles=%d receiveCycles=%d", m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles);
		ISO_TRACE("EXIT", "ret=%ld", ret);
		return ret;
	}
	if (m_CoPData.size() < 2 || m_CoPData.size() > sizeof(PASSTHRU_MSG{}.Data))
		return ERR_INVALID_MSG;

	unsigned long dataSize = m_CoPData.size();
	PASSTHRU_MSG txMsg = {};
	txMsg.ProtocolID = m_protocolID;
	txMsg.DataSize = dataSize;
	txMsg.ExtraDataIndex = dataSize;
	PASSTHRU_MSG rxMsg = {};
	ISO_TRACE("TX", "ProtocolID=%lu DataSize=%lu ExtraDataIndex=%lu", txMsg.ProtocolID, txMsg.DataSize, txMsg.ExtraDataIndex);

	memcpy(txMsg.Data, &m_CoPData[0], dataSize);

	ISO_TRACE("BEFORE", "_PassThruIoctl ChannelID=%lu Ioctl=FAST_INIT input=%p output=%p", channelID, FAST_INIT, &txMsg, &rxMsg);
	ret = _PassThruIoctl(channelID, FAST_INIT, &txMsg, &rxMsg);
	if (ret == STATUS_NOERROR)
		ISO_TRACE("AFTER", "_PassThruIoctl ret=%ld output DataSize=%lu RxStatus=%lu ProtocolID=%lu", ret, rxMsg.DataSize, rxMsg.RxStatus, rxMsg.ProtocolID);
	else
		ISO_TRACE("AFTER", "_PassThruIoctl ret=%ld output fields unavailable", ret);
	if (ret == STATUS_NOERROR)
	{
		if (rxMsg.DataSize == 0)
			return ERR_TIMEOUT;
		--m_CopCtrlData.NumSendCycles;
		--m_CopCtrlData.NumReceiveCycles;

		m_destAddr = m_CoPData[1];

		ISO_TRACE("RX", "ProtocolID=%lu RxStatus=%lu Timestamp=%lu DataSize=%lu ExtraDataIndex=%lu", rxMsg.ProtocolID, rxMsg.RxStatus, rxMsg.Timestamp, rxMsg.DataSize, rxMsg.ExtraDataIndex);

		pEvt = new PDU_EVENT_ITEM;
		pEvt->hCop = m_hCoP;
		pEvt->ItemType = PDU_IT_RESULT;
		pEvt->pCoPTag = m_pCoPTag;
		pEvt->pData = new PDU_RESULT_DATA;

		PDU_RESULT_DATA* pRes = (PDU_RESULT_DATA*)(pEvt->pData);
		pRes->AcceptanceId = 1;
		pRes->NumDataBytes = rxMsg.DataSize;
		pRes->pDataBytes = new UNUM8[rxMsg.DataSize];
		pRes->pExtraInfo = nullptr;
		pRes->RxFlag.NumFlagBytes = 0;
		pRes->StartMsgTimestamp = 0;
		pRes->TimestampFlags.NumFlagBytes = 0;
		pRes->TxMsgDoneTimestamp = 0;
		pRes->UniqueRespIdentifier = PDU_ID_UNDEF;

		memcpy(pRes->pDataBytes, rxMsg.Data, pRes->NumDataBytes);

		ISO_TRACE("STATE", "diagnostic session destination=0x%x sendCycles=%d receiveCycles=%d", m_destAddr, m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles);
	}

	ISO_TRACE("EXIT", "ret=%ld event=%p", ret, pEvt);
	return ret;
}

long ISO14230ComPrimitive::StopComm(unsigned long channelID, PDU_EVENT_ITEM*& pEvt)
{
	long ret = STATUS_NOERROR;
	ISO_TRACE("ENTER", "channelID=%lu hCoP=%u eventOut=%p", channelID, m_hCoP, &pEvt);
	ISO_TRACE("EXIT", "ret=%ld", ret);
	return ret;
}

void checksum(std::vector<UNUM8>& data, UNUM32 dataSize)
{
	UNUM8 csum = 0;
	for (UNUM8 i = 0; i < dataSize; ++i)
	{
		csum += data[i];
	}

	data[dataSize] = csum;
}

long ISO14230ComPrimitive::SendRecv(unsigned long channelID, PDU_EVENT_ITEM*& pEvt)
{
	long ret = STATUS_NOERROR;
	ISO_TRACE("ENTER", "channelID=%lu hCoP=%u protocolID=%lu dataSize=%zu sendCycles=%d receiveCycles=%d eventOut=%p", channelID, m_hCoP, m_protocolID, m_CoPData.size(), m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles, &pEvt);
	if (m_CopCtrlData.NumSendCycles > 0 && (m_CoPData.size() < 2 || m_CoPData.size() > sizeof(PASSTHRU_MSG{}.Data)))
		return ERR_INVALID_MSG;

	if (m_CopCtrlData.NumSendCycles > 0)
	{
		if (TesterPresentWorkaround(pEvt))
		{
			--m_CopCtrlData.NumSendCycles;
			if (m_CopCtrlData.NumReceiveCycles != -1)
			{
				--m_CopCtrlData.NumReceiveCycles;
			}

			ISO_TRACE("EXIT", "ret=%ld testerPresentHandled=true event=%p", ret, pEvt);
			return ret;
		}

		if (CheckDestinationAddress(channelID) != STATUS_NOERROR)
		{
			ISO_TRACE("EXIT", "ret=%ld destinationCheckFailed=true", ret);
			return ret;
		}

		unsigned long numMsgs = 1;

		if (m_CoPData.empty() || m_CoPData.size() > sizeof(PASSTHRU_MSG{}.Data))
			return ERR_INVALID_MSG;
		unsigned long dataSize = m_CoPData.size();
		PASSTHRU_MSG txMsg = {};
		txMsg.ProtocolID = m_protocolID;
		txMsg.DataSize = dataSize;
		txMsg.ExtraDataIndex = dataSize;
		ISO_TRACE("TX", "ProtocolID=%lu DataSize=%lu ExtraDataIndex=%lu requestedMessages=%lu Timeout=%d", txMsg.ProtocolID, txMsg.DataSize, txMsg.ExtraDataIndex, numMsgs, TIMEOUT_MS);

		memcpy(txMsg.Data, &m_CoPData[0], dataSize);

		ISO_TRACE("BEFORE", "_PassThruWriteMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &txMsg, &numMsgs, numMsgs, TIMEOUT_MS);
		ret = _PassThruWriteMsgs(channelID, &txMsg, &numMsgs, TIMEOUT_MS);
		ISO_TRACE("AFTER", "_PassThruWriteMsgs ret=%ld returnedMessages=%lu", ret, numMsgs);

		if (ret == STATUS_NOERROR)
		{
			--m_CopCtrlData.NumSendCycles;
		}
		else
		{
			ISO_TRACE("RESULT", "_PassThruWriteMsgs failed ret=%ld", ret);
		}
	}

	if (m_CopCtrlData.NumReceiveCycles > 0 || m_CopCtrlData.NumReceiveCycles == -1 || m_CopCtrlData.NumReceiveCycles == -2)
	{
		PASSTHRU_MSG rxMsg = { 0 };
		unsigned long numMsgs = 1;
		ISO_TRACE("BEFORE", "_PassThruReadMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &rxMsg, &numMsgs, numMsgs, POLL_TIMEOUT_MS);
		ret = _PassThruReadMsgs(channelID, &rxMsg, &numMsgs, POLL_TIMEOUT_MS);
		ISO_TRACE("AFTER", "_PassThruReadMsgs ret=%ld returnedMessages=%lu RxStatus=%lu DataSize=%lu", ret, numMsgs, rxMsg.RxStatus, rxMsg.DataSize);
		if (ret == STATUS_NOERROR && rxMsg.RxStatus == START_OF_MESSAGE)
		{
			memset(&rxMsg, 0, sizeof(rxMsg));
			numMsgs = 1;
			ISO_TRACE("BEFORE", "_PassThruReadMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &rxMsg, &numMsgs, numMsgs, TIMEOUT_MS);
			ret = _PassThruReadMsgs(channelID, &rxMsg, &numMsgs, TIMEOUT_MS);
			ISO_TRACE("AFTER", "_PassThruReadMsgs ret=%ld returnedMessages=%lu RxStatus=%lu Timestamp=%lu DataSize=%lu ExtraDataIndex=%lu", ret, numMsgs, rxMsg.RxStatus, rxMsg.Timestamp, rxMsg.DataSize, rxMsg.ExtraDataIndex);
			if (ret == STATUS_NOERROR && numMsgs > 0)
			{
				ISO_TRACE("RX", "ProtocolID=%lu RxStatus=%lu Timestamp=%lu DataSize=%lu ExtraDataIndex=%lu", rxMsg.ProtocolID, rxMsg.RxStatus, rxMsg.Timestamp, rxMsg.DataSize, rxMsg.ExtraDataIndex);

				if (m_CopCtrlData.NumReceiveCycles != -1)
				{
					--m_CopCtrlData.NumReceiveCycles;
				}

				pEvt = new PDU_EVENT_ITEM;
				pEvt->hCop = m_hCoP;
				pEvt->ItemType = PDU_IT_RESULT;
				pEvt->pCoPTag = m_pCoPTag;
				pEvt->pData = new PDU_RESULT_DATA;

				PDU_RESULT_DATA* pRes = (PDU_RESULT_DATA*)(pEvt->pData);
				pRes->AcceptanceId = 1;
				pRes->NumDataBytes = rxMsg.DataSize;
				pRes->pDataBytes = new UNUM8[rxMsg.DataSize];
				pRes->pExtraInfo = nullptr;
				pRes->RxFlag.NumFlagBytes = 0;
				pRes->StartMsgTimestamp = 0;
				pRes->TimestampFlags.NumFlagBytes = 0;
				pRes->TxMsgDoneTimestamp = 0;
				pRes->UniqueRespIdentifier = PDU_ID_UNDEF;

				memcpy(pRes->pDataBytes, rxMsg.Data, rxMsg.DataSize);
			}
			else if (ret == STATUS_NOERROR)
			{
				m_CopCtrlData.NumReceiveCycles = 0;
				ret = ERR_TIMEOUT;
			}
			else
			{
				ISO_TRACE("RESULT", "_PassThruReadMsgs failed ret=%ld", ret);
			}
		}
		else if (ret == ERR_TIMEOUT || ret == ERR_BUFFER_EMPTY)
		{
			ISO_TRACE("RESULT", "waiting for start of message timed out ret=%ld", ret);
			if (m_CopCtrlData.NumReceiveCycles != -1)
			{
				m_CopCtrlData.NumReceiveCycles = 0;
				ret = ERR_TIMEOUT;
			}
			else
				ret = STATUS_NOERROR;
		}
		else if (ret == STATUS_NOERROR && (numMsgs == 0 || rxMsg.RxStatus != START_OF_MESSAGE) && m_CopCtrlData.NumReceiveCycles != -1)
		{
			m_CopCtrlData.NumReceiveCycles = 0;
			ret = ERR_TIMEOUT;
		}
	}

	ISO_TRACE("EXIT", "ret=%ld event=%p sendCycles=%d receiveCycles=%d", ret, pEvt, m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles);
	return ret;
}

long ISO14230ComPrimitive::CheckDestinationAddress(unsigned long channelID)
{
	long ret = STATUS_NOERROR;
	ISO_TRACE("ENTER", "channelID=%lu hCoP=%u AutoRestartComm=%s destination=0x%x", channelID, m_hCoP, Settings::AutoRestartComm ? "true" : "false", m_destAddr);

	if (Settings::AutoRestartComm)
	{
		UNUM8 format = m_CoPData[0];

		// Ignore if not physical (10xxxxxx) or functional (11xxxxxx) addressing
		if ((format & 0xC0) != 0x80 && (format & 0xC0) != 0xC0)
		{
			ISO_TRACE("RESULT", "AutoRestartComm ignored for format=0x%x", format);
			ISO_TRACE("EXIT", "ret=%ld", ret);
			return ret; 
		}

		if (m_CoPData[1] != m_destAddr)
		{
			ISO_TRACE("ACTION", "destination mismatch session=0x%x message=0x%x; restarting communication", m_destAddr, m_CoPData[1]);

			std::vector<UNUM8> data = { 0x81, 0x00, 0xf1, 0x81, 0x00 };
			data[1] = m_CoPData[1];
			checksum(data, data.size() - 1);

			PDU_COP_CTRL_DATA ctrlData;
			ctrlData.NumReceiveCycles = 1;
			ctrlData.NumSendCycles = 1;

			auto cop = ISO14230ComPrimitive(PDU_COPT_STARTCOMM, data.size(), data.data(), &ctrlData, nullptr, m_protocolID);

			PDU_EVENT_ITEM* pEvt = nullptr;
			ISO_TRACE("BEFORE", "temporary ComPrimitive::StartComm channelID=%lu target=0x%x", channelID, data[1]);
			ret = cop.StartComm(channelID, pEvt);
			ISO_TRACE("AFTER", "temporary ComPrimitive::StartComm ret=%ld event=%p", ret, pEvt);
			if (ret == STATUS_NOERROR)
			{
				PDU_EVENT_ITEM* pIt = (PDU_EVENT_ITEM*)pEvt;
				PDU_RESULT_DATA* pData = (PDU_RESULT_DATA*)pIt->pData;
				delete[] pData->pDataBytes;
				pData->pDataBytes = nullptr;
				delete pIt->pData;
				pIt->pData = nullptr;
				delete pIt;
				pIt = nullptr;
			}
		}
	}

	ISO_TRACE("EXIT", "ret=%ld", ret);
	return ret;
}

bool ISO14230ComPrimitive::TesterPresentWorkaround(PDU_EVENT_ITEM*& pEvt)
{
	bool ret = false;
	ISO_TRACE("ENTER", "hCoP=%u DisableTesterpresent=%s FixTesterpresentDestination=%s dataSize=%zu", m_hCoP, Settings::DisableTesterpresent ? "true" : "false", Settings::FixTesterpresentDestination ? "true" : "false", m_CoPData.size());

	if (Settings::DisableTesterpresent)
	{
		if (m_CoPData == MSG_TESTER_PRESENT_41)
		{
			pEvt = new PDU_EVENT_ITEM;
			pEvt->hCop = m_hCoP;
			pEvt->ItemType = PDU_IT_RESULT;
			pEvt->pCoPTag = m_pCoPTag;
			pEvt->pData = new PDU_RESULT_DATA;

			PDU_RESULT_DATA* pRes = (PDU_RESULT_DATA*)(pEvt->pData);
			pRes->AcceptanceId = 1;
			pRes->NumDataBytes = 5;
			pRes->pDataBytes = new UNUM8[5]{ 0x81, 0xf1, 0x41, 0x7e, 0x31 };
			pRes->pExtraInfo = nullptr;
			pRes->RxFlag.NumFlagBytes = 0;
			pRes->StartMsgTimestamp = 0;
			pRes->TimestampFlags.NumFlagBytes = 0;
			pRes->TxMsgDoneTimestamp = 0;
			pRes->UniqueRespIdentifier = PDU_ID_UNDEF;

			ISO_TRACE("ACTION", "simulating TesterPresent; message suppressed; event=%p", pEvt);

			ret = true;
		}
	}
	else if (Settings::FixTesterpresentDestination)
	{
		if (m_CoPData == MSG_TESTER_PRESENT_41)
		{
			m_CoPData[1] = m_destAddr;
			checksum(m_CoPData, m_CoPData.size() - 1);

			ISO_TRACE("ACTION", "TesterPresent destination fixed to 0x%x", m_destAddr);

			ret = false;
		}
	}

	ISO_TRACE("EXIT", "handled=%s event=%p", ret ? "true" : "false", pEvt);
	return ret;
}
