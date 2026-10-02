#include "pch.h"
#include "pdu_api.h"
#include "KW82ComPrimitive.h"
#include "Logger.h"
#include "Settings.h"
#include "j2534/j2534_v0404.h"
#include "j2534/shim_loader.h"

#include <string>
#include <sstream>

#define KW_TRACE(event, ...) LOGGER.trace("KW82ComPrimitive.cpp", __FUNCTION__, event, __VA_ARGS__)

constexpr int POLL_TIMEOUT_MS = 10;
constexpr int TIMEOUT_MS = 1000;

long KW82ComPrimitive::StartComm(unsigned long channelID, PDU_EVENT_ITEM*& pEvt)
{
	long ret = STATUS_NOERROR;
	KW_TRACE("ENTER", "channelID=%lu hCoP=%u protocolID=%lu dataSize=%zu sendCycles=%d receiveCycles=%d eventOut=%p", channelID, m_hCoP, m_protocolID, m_CoPData.size(), m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles, &pEvt);

	if (m_CopCtrlData.NumReceiveCycles == 0 || m_CopCtrlData.NumSendCycles == 0)
	{
		KW_TRACE("RESULT", "no-op because sendCycles=%d receiveCycles=%d", m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles);
		KW_TRACE("EXIT", "ret=%ld", ret);
		return ret;
	}

	KW_TRACE("ACTION", "starting FIVE_BAUD_INIT ECUAddress=0x64");

	_SBYTE_ARRAY input;
	_SBYTE_ARRAY output;
	UINT8 ecuAddress = 0x64;
	UINT8 keyword[2] = { 0, 0 };

	input.NumOfBytes = 1;
	input.BytePtr = &ecuAddress;

	output.NumOfBytes = 2;
	output.BytePtr = keyword;

	KW_TRACE("BEFORE", "_PassThruIoctl ChannelID=%lu Ioctl=FIVE_BAUD_INIT input=%p NumOfBytes=%lu BytePtr=%p output=%p outputBytes=%lu outputPtr=%p", channelID, FIVE_BAUD_INIT, &input, input.NumOfBytes, input.BytePtr, &output, output.NumOfBytes, output.BytePtr);
	ret = _PassThruIoctl(channelID, FIVE_BAUD_INIT, &input, &output);
	KW_TRACE("AFTER", "_PassThruIoctl ret=%ld keyword0=0x%02X keyword1=0x%02X", ret, keyword[0], keyword[1]);
	if (ret == STATUS_NOERROR)
	{
		KW_TRACE("STATE", "FIVE_BAUD_INIT succeeded keywords=0x%02X,0x%02X", keyword[0], keyword[1]);

		PASSTHRU_MSG rxMsg = { 0 };
		unsigned long numMsgs = 1;
		KW_TRACE("BEFORE", "_PassThruReadMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &rxMsg, &numMsgs, numMsgs, TIMEOUT_MS);
		ret = _PassThruReadMsgs(channelID, &rxMsg, &numMsgs, TIMEOUT_MS);
		KW_TRACE("AFTER", "_PassThruReadMsgs ret=%ld returnedMessages=%lu RxStatus=%lu DataSize=%lu", ret, numMsgs, rxMsg.RxStatus, rxMsg.DataSize);
		if (ret == STATUS_NOERROR && rxMsg.RxStatus == START_OF_MESSAGE)
		{
			memset(&rxMsg, 0, sizeof(rxMsg));
			numMsgs = 1;
			KW_TRACE("BEFORE", "_PassThruReadMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &rxMsg, &numMsgs, numMsgs, TIMEOUT_MS);
			ret = _PassThruReadMsgs(channelID, &rxMsg, &numMsgs, TIMEOUT_MS);
			KW_TRACE("AFTER", "_PassThruReadMsgs ret=%ld returnedMessages=%lu RxStatus=%lu Timestamp=%lu DataSize=%lu ExtraDataIndex=%lu", ret, numMsgs, rxMsg.RxStatus, rxMsg.Timestamp, rxMsg.DataSize, rxMsg.ExtraDataIndex);
			if (ret == STATUS_NOERROR && numMsgs > 0)
			{
				--m_CopCtrlData.NumSendCycles;
				--m_CopCtrlData.NumReceiveCycles;

				KW_TRACE("RX", "ProtocolID=%lu RxStatus=%lu Timestamp=%lu DataSize=%lu ExtraDataIndex=%lu", rxMsg.ProtocolID, rxMsg.RxStatus, rxMsg.Timestamp, rxMsg.DataSize, rxMsg.ExtraDataIndex);

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
			}
			else if (ret == STATUS_NOERROR)
			{
				ret = ERR_TIMEOUT;
			}
		}
		else if (ret == STATUS_NOERROR)
		{
			ret = ERR_TIMEOUT;
		}
		else
		{
			KW_TRACE("RESULT", "_PassThruReadMsgs failed ret=%ld", ret);
		}
		
	}

	KW_TRACE("EXIT", "ret=%ld event=%p sendCycles=%d receiveCycles=%d", ret, pEvt, m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles);
	return ret;
}

long KW82ComPrimitive::StopComm(unsigned long channelID, PDU_EVENT_ITEM*& pEvt)
{
	long ret = STATUS_NOERROR;
	KW_TRACE("ENTER", "channelID=%lu hCoP=%u", channelID, m_hCoP);

	KW_TRACE("ACTION", "terminating session using five writes");
	unsigned long dataSize = 4;
	PASSTHRU_MSG txMsg = { m_protocolID, 0, 0, 0, dataSize, dataSize };
	txMsg.Data[0] = 0x02;
	txMsg.Data[1] = 0xB2;
	txMsg.Data[2] = 0x00;
	txMsg.Data[3] = 0xB4;

	//spam "end session" a few times
	for (int i = 0; i < 5; ++i)
	{
		unsigned long numMsgs = 1;
		KW_TRACE("BEFORE", "_PassThruWriteMsgs iteration=%d ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d DataSize=%lu", i, channelID, &txMsg, &numMsgs, numMsgs, TIMEOUT_MS, txMsg.DataSize);
		ret = _PassThruWriteMsgs(channelID, &txMsg, &numMsgs, TIMEOUT_MS);
		KW_TRACE("AFTER", "_PassThruWriteMsgs iteration=%d ret=%ld returnedMessages=%lu", i, ret, numMsgs);
		if (ret != STATUS_NOERROR)
		{
			KW_TRACE("RESULT", "_PassThruWriteMsgs failed iteration=%d ret=%ld", i, ret);
		}
	}

	KW_TRACE("EXIT", "ret=%ld", ret);
	return ret;
}

long KW82ComPrimitive::SendRecv(unsigned long channelID, PDU_EVENT_ITEM*& pEvt)
{
	long ret = STATUS_NOERROR;
	KW_TRACE("ENTER", "channelID=%lu hCoP=%u protocolID=%lu sendCycles=%d receiveCycles=%d", channelID, m_hCoP, m_protocolID, m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles);

	if (m_CopCtrlData.NumReceiveCycles > 0 || m_CopCtrlData.NumReceiveCycles == -1 || m_CopCtrlData.NumReceiveCycles == -2)
	{
		PASSTHRU_MSG rxMsg = { 0 };
		unsigned long numMsgs = 1;
		KW_TRACE("BEFORE", "_PassThruReadMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &rxMsg, &numMsgs, numMsgs, POLL_TIMEOUT_MS);
		ret = _PassThruReadMsgs(channelID, &rxMsg, &numMsgs, POLL_TIMEOUT_MS);
		KW_TRACE("AFTER", "_PassThruReadMsgs ret=%ld returnedMessages=%lu RxStatus=%lu DataSize=%lu", ret, numMsgs, rxMsg.RxStatus, rxMsg.DataSize);
		if (ret == STATUS_NOERROR && rxMsg.RxStatus == START_OF_MESSAGE)
		{
			memset(&rxMsg, 0, sizeof(rxMsg));
			numMsgs = 1;
			KW_TRACE("BEFORE", "_PassThruReadMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &rxMsg, &numMsgs, numMsgs, TIMEOUT_MS);
			ret = _PassThruReadMsgs(channelID, &rxMsg, &numMsgs, TIMEOUT_MS);
			KW_TRACE("AFTER", "_PassThruReadMsgs ret=%ld returnedMessages=%lu RxStatus=%lu Timestamp=%lu DataSize=%lu ExtraDataIndex=%lu", ret, numMsgs, rxMsg.RxStatus, rxMsg.Timestamp, rxMsg.DataSize, rxMsg.ExtraDataIndex);
			if (ret == STATUS_NOERROR && numMsgs > 0)
			{
				KW_TRACE("RX", "ProtocolID=%lu RxStatus=%lu Timestamp=%lu DataSize=%lu ExtraDataIndex=%lu", rxMsg.ProtocolID, rxMsg.RxStatus, rxMsg.Timestamp, rxMsg.DataSize, rxMsg.ExtraDataIndex);

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
			else
			{
				KW_TRACE("RESULT", "_PassThruReadMsgs failed ret=%ld", ret);
			}
		}
		else if (ret == ERR_TIMEOUT || ret == ERR_BUFFER_EMPTY)
		{
			KW_TRACE("RESULT", "waiting for start of message timed out ret=%ld", ret);
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

	if (m_CopCtrlData.NumSendCycles > 0)
	{
		if (m_CoPData.empty() || m_CoPData.size() > sizeof(PASSTHRU_MSG{}.Data))
			return ERR_INVALID_MSG;
		unsigned long numMsgs = 1;

		unsigned long dataSize = m_CoPData.size();
		PASSTHRU_MSG txMsg = { m_protocolID, 0, 0, 0, dataSize, dataSize };
		KW_TRACE("TX", "ProtocolID=%lu DataSize=%lu ExtraDataIndex=%lu requestedMessages=%lu Timeout=%d", txMsg.ProtocolID, txMsg.DataSize, txMsg.ExtraDataIndex, numMsgs, TIMEOUT_MS);

		memcpy(txMsg.Data, &m_CoPData[0], dataSize);

		KW_TRACE("BEFORE", "_PassThruWriteMsgs ChannelID=%lu pMsg=%p pNumMsgs=%p requested=%lu Timeout=%d", channelID, &txMsg, &numMsgs, numMsgs, TIMEOUT_MS);
		ret = _PassThruWriteMsgs(channelID, &txMsg, &numMsgs, TIMEOUT_MS);
		KW_TRACE("AFTER", "_PassThruWriteMsgs ret=%ld returnedMessages=%lu", ret, numMsgs);

		if (ret == STATUS_NOERROR)
		{
			--m_CopCtrlData.NumSendCycles;
		}
		else
		{
			KW_TRACE("RESULT", "_PassThruWriteMsgs failed ret=%ld", ret);
		}
	}

	KW_TRACE("EXIT", "ret=%ld event=%p sendCycles=%d receiveCycles=%d", ret, pEvt, m_CopCtrlData.NumSendCycles, m_CopCtrlData.NumReceiveCycles);
	return STATUS_NOERROR;
}

#undef KW_TRACE
