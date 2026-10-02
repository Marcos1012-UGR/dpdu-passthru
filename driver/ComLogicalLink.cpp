#include "pch.h"
#include "ComLogicalLink.h"
#include "j2534/j2534_v0404.h"
#include "j2534/shim_loader.h"
#include "Logger.h"
#include "pdu_api.h"

#include "ComPrimitive.h"
#include "ISO14230ComPrimitive.h"
#include "KW82ComPrimitive.h"
#include "GenericComPrimitive.h"

#include <thread>
#include <algorithm>
#include <string>
#include <sstream>
#include <chrono>

#define CLL_TRACE(event, ...) LOGGER.trace("ComLogicalLink.cpp", __FUNCTION__, event, __VA_ARGS__)

ComLogicalLink::ComLogicalLink(
	UNUM32 hMod,
	UNUM32 hCLL,
	unsigned long deviceID,
	enum Protocol protocol,
	UNUM32 dPduProtocolId,
	UNUM32 busTypeId,
	UNUM32 resourceId,
	const std::vector<UNUM32>& dlcPins,
	const std::vector<UNUM8>& createFlags
) :
	m_eventCallbackFnc(nullptr),
	m_hMod(hMod),
	m_hCLL(hCLL),
	m_status(PDU_CLLST_OFFLINE),
	m_lastErrorCode(PDU_ERR_EVT_NOERROR),
	m_lastErrorCoP(PDU_HANDLE_UNDEF),
	m_lastErrorTimestamp(0),
	m_lastExtraErrorInfo(0),
	m_protocol(protocol),
	m_dPduProtocolId(dPduProtocolId),
	m_busTypeId(busTypeId),
	m_resourceId(resourceId),
	m_protocolID(0),
	m_deviceID(deviceID),
	m_channelID(0),
	m_dlcPins(dlcPins),
	m_createFlags(createFlags),
	m_running(false)
{
	CLL_TRACE("ENTER", "hMod=%u hCLL=%u deviceID=%lu protocol=%u", hMod, hCLL, deviceID, static_cast<unsigned int>(protocol));
	switch (protocol)
	{
	case CLL_ISO14230:
		m_protocolID = ISO14230;
		break;

	case CLL_KW82:
		m_protocolID = ISO9141;
		break;
	case CLL_ISO9141:
		m_protocolID = ISO9141;
		break;

	case CLL_CAN:
		m_protocolID = CAN;
		break;

	case CLL_ISO15765:
		m_protocolID = ISO15765;
		break;
	case CLL_SWCAN:
		m_protocolID = SW_CAN_PS;
		break;
	case CLL_J1850_VPW:
		m_protocolID = J1850VPW;
		break;
	case CLL_J1850_PWM:
		m_protocolID = J1850PWM;
		break;
	case CLL_UART:
		m_protocolID = GM_UART_PS;
		break;

		// No direct J2534 protocol mapping.
		// The D-PDU protocol ID is retained separately.
	case CLL_J1939:
	case CLL_UNKNOWN:
	default:
		m_protocolID = 0;
		break;
	}
	CLL_TRACE(
		"RESOURCE",
		"hCLL=%u DpduProtocolId=%u BusTypeId=%u ResourceId=%u",
		m_hCLL,
		m_dPduProtocolId,
		m_busTypeId,
		m_resourceId
	);

	CLL_TRACE(
		"RESOURCE",
		"mappedProtocol=%u J2534ProtocolID=%lu",
		static_cast<unsigned int>(m_protocol),
		m_protocolID
	);

	CLL_TRACE(
		"RESOURCE",
		"DLC pin count=%zu createFlagBytes=%zu",
		m_dlcPins.size(),
		m_createFlags.size()
	);

	for (size_t i = 0; i < m_dlcPins.size(); ++i)
	{
		CLL_TRACE(
			"PIN",
			"index=%zu pin=%u",
			i,
			m_dlcPins[i]
		);
	}

	for (size_t i = 0; i < m_createFlags.size(); ++i)
	{
		CLL_TRACE(
			"CREATE_FLAG",
			"index=%zu value=0x%02X",
			i,
			static_cast<unsigned int>(m_createFlags[i])
		);
	}
	CLL_TRACE("STATE", "protocol=%u protocolID=%lu channelID=%lu status=0x%x running=%s", static_cast<unsigned int>(m_protocol), m_protocolID, m_channelID, static_cast<unsigned int>(m_status), m_running ? "true" : "false");
	CLL_TRACE("EXIT", "");
}

long ComLogicalLink::Connect()
{
	long ret = STATUS_NOERROR;
	CLL_TRACE("ENTER", "hMod=%u hCLL=%u deviceID=%lu protocol=%u protocolID=%lu channelID=%lu running=%s", m_hMod, m_hCLL, m_deviceID, static_cast<unsigned int>(m_protocol), m_protocolID, m_channelID, m_running ? "true" : "false");
	// StartMsgFilter may connect a link before the D-PDU host calls PDUConnect.
	// Treat the later explicit call as idempotent instead of replacing a live
	// jthread (which joins the still-running worker and blocks forever).
	if (m_running && m_channelID != 0)
	{
		CLL_TRACE("EXIT", "already connected channelID=%lu", m_channelID);
		return STATUS_NOERROR;
	}

	const unsigned long flags =
		(m_protocol == CLL_ISO14230 || m_protocol == CLL_KW82)
		? ISO9141_NO_CHECKSUM
		: 0;
	const unsigned long baudrate =
		m_protocol == CLL_ISO14230 ? 10400 :
		m_protocol == CLL_KW82 ? 8192 : 0;
	CLL_TRACE("BEFORE", "_PassThruConnect DeviceID=%lu ProtocolID=%lu Flags=%lu Baudrate=%lu pChannelID=%p", m_deviceID, m_protocolID, flags, baudrate, &m_channelID);
	ret = _PassThruConnect(m_deviceID, m_protocolID, flags, baudrate, &m_channelID);
	CLL_TRACE("AFTER", "_PassThruConnect ret=%ld channelID=%lu", ret, m_channelID);

	if (ret != STATUS_NOERROR)
	{
		char err[256];
		_PassThruGetLastError(err);
		CLL_TRACE("RESULT", "_PassThruConnect failed ret=%ld error=%s", ret, err);
		CLL_TRACE("EXIT", "ret=%ld status=0x%x running=%s channelID=%lu", ret, static_cast<unsigned int>(m_status), m_running ? "true" : "false", m_channelID);
		return ret;
	}

	if (ret == STATUS_NOERROR)
	{
		m_running = true;
		m_runLoop = std::jthread(&ComLogicalLink::run, this);

		m_status = PDU_CLLST_ONLINE;
		CLL_TRACE("STATE", "m_status=0x%x", static_cast<unsigned int>(m_status));

		PDU_EVENT_ITEM* pEvt = new PDU_EVENT_ITEM;
		pEvt->hCop = PDU_HANDLE_UNDEF;
		pEvt->ItemType = PDU_IT_STATUS;
		pEvt->pCoPTag = nullptr;
		pEvt->pData = new PDU_STATUS_DATA;
		*(PDU_STATUS_DATA*)(pEvt->pData) = m_status;

		SignalEvent(pEvt);
	}

	CLL_TRACE("EXIT", "ret=%ld status=0x%x running=%s channelID=%lu", ret, static_cast<unsigned int>(m_status), m_running ? "true" : "false", m_channelID);
	return ret;
}

long ComLogicalLink::Disconnect()
{
	long ret = STATUS_NOERROR;

	CLL_TRACE(
		"ENTER",
		"hMod=%u hCLL=%u deviceID=%lu channelID=%lu status=0x%x running=%s",
		m_hMod,
		m_hCLL,
		m_deviceID,
		m_channelID,
		static_cast<unsigned int>(m_status),
		m_running ? "true" : "false"
	);

	if (!m_running && m_channelID == 0)
	{
		CLL_TRACE(
			"RESULT",
			"Already disconnected: running=false channelID=0"
		);

		return STATUS_NOERROR;
	}

	m_running = false;

	CLL_TRACE("STATE", "m_running=false channelID=%lu", m_channelID);
	{
		const std::lock_guard<std::mutex> lock(m_copLock);
		for (auto& cop : m_copQueue)
		{
			PDU_EVENT_ITEM* cancelEvent = nullptr;
			cop->Cancel(cancelEvent);
			QueueEvent(cancelEvent);
		}
	}
	if (m_runLoop.joinable())
	{
		CLL_TRACE("ACTION", "waiting for primitive worker to exit");
		m_runLoop.join();
	}

	{
		const std::lock_guard<std::mutex> lock(m_copLock);
		for (auto it = m_copQueue.begin(); it != m_copQueue.end(); ++it)
		{
			(*it)->Destroy();
		}
		m_copQueue.clear();
	}

	m_status = PDU_CLLST_OFFLINE;
	CLL_TRACE("STATE", "m_status=0x%x channelID=%lu", static_cast<unsigned int>(m_status), m_channelID);

	PDU_EVENT_ITEM* pEvt = new PDU_EVENT_ITEM;
	pEvt->hCop = PDU_HANDLE_UNDEF;
	pEvt->ItemType = PDU_IT_STATUS;
	pEvt->pCoPTag = nullptr;
	pEvt->pData = new PDU_STATUS_DATA;
	*(PDU_STATUS_DATA*)(pEvt->pData) = m_status;

	SignalEvent(pEvt);
	
	CLL_TRACE("BEFORE", "_PassThruDisconnect ChannelID=%lu", m_channelID);
	ret = _PassThruDisconnect(m_channelID);
	CLL_TRACE("AFTER", "_PassThruDisconnect ret=%ld ChannelID=%lu", ret, m_channelID);
	if (ret == STATUS_NOERROR)
	{
		m_channelID = 0;
	}
	CLL_TRACE("EXIT", "ret=%ld status=0x%x running=%s channelID=%lu", ret, static_cast<unsigned int>(m_status), m_running ? "true" : "false", m_channelID);
	return ret;
}

T_PDU_ERROR ComLogicalLink::GetStatus(T_PDU_STATUS& status)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;
	CLL_TRACE("ENTER", "hCLL=%u statusOut=%p", m_hCLL, &status);

	status = m_status;

	CLL_TRACE("EXIT", "ret=0x%08X status=0x%x", static_cast<unsigned int>(ret), static_cast<unsigned int>(status));
	return ret;
}

void ComLogicalLink::GetLastError(T_PDU_ERR_EVT& error, UNUM32& hCoP, UNUM32& timestamp, UNUM32& extraInfo)
{
	const std::lock_guard<std::mutex> lock(m_errorLock);
	error = m_lastErrorCode;
	hCoP = m_lastErrorCoP;
	timestamp = m_lastErrorTimestamp;
	extraInfo = m_lastExtraErrorInfo;
}

T_PDU_ERROR ComLogicalLink::GetStatus(UNUM32 hCoP, T_PDU_STATUS& status)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;
	CLL_TRACE("ENTER", "hCLL=%u hCoP=%u statusOut=%p", m_hCLL, hCoP, &status);

	{
		const std::lock_guard<std::mutex> lock(m_copLock);

		auto it = m_copQueue.end();
		for (it = m_copQueue.begin(); it != m_copQueue.end(); ++it)
		{
			if ((*it)->getHandle() == hCoP)
			{
				status = (*it)->GetStatus();
				break;
			}
		}

		if (it == m_copQueue.end() || (*it)->getHandle() == 0)
		{
			status = PDU_COPST_FINISHED;
			if (m_finishedCopHandles.find(hCoP) == m_finishedCopHandles.end())
				ret = PDU_ERR_INVALID_HANDLE;
		}
	}

	CLL_TRACE("EXIT", "ret=0x%08X hCoP=%u status=0x%x", static_cast<unsigned int>(ret), hCoP, static_cast<unsigned int>(status));
	return ret;
}

T_PDU_ERROR ComLogicalLink::Cancel(UNUM32 hCoP)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;
	CLL_TRACE("ENTER", "hCLL=%u hCoP=%u", m_hCLL, hCoP);

	PDU_EVENT_ITEM* pEvt = nullptr;
	{
		const std::lock_guard<std::mutex> lock(m_copLock);

		auto it = m_copQueue.end();
		for (it = m_copQueue.begin(); it != m_copQueue.end(); ++it)
		{
			if ((*it)->getHandle() == hCoP)
			{
				(*it)->Cancel(pEvt);
				break;
			}
		}

		if (it == m_copQueue.end() || (*it)->getHandle() == 0)
		{
			ret = PDU_ERR_INVALID_HANDLE;
		}
	}

	CLL_TRACE("ACTION", "cancellation result=0x%08X event=%p", static_cast<unsigned int>(ret), pEvt);
	SignalEvent(pEvt);

	CLL_TRACE("EXIT", "ret=0x%08X", static_cast<unsigned int>(ret));
	return ret;
}

UNUM32 ComLogicalLink::StartComPrimitive(UNUM32 CoPType, UNUM32 CoPDataSize, UNUM8* pCoPData, PDU_COP_CTRL_DATA* pCopCtrlData, void* pCoPTag)
{
    std::shared_ptr<ComPrimitive> cop;
	CLL_TRACE("ENTER", "hCLL=%u CoPType=%u CoPDataSize=%u pCoPData=%p pCopCtrlData=%p pCoPTag=%p", m_hCLL, CoPType, CoPDataSize, pCoPData, pCopCtrlData, pCoPTag);

	switch (m_protocol)
	{
	case CLL_ISO14230:

		cop = std::make_shared<ISO14230ComPrimitive>(
			CoPType,
			CoPDataSize,
			pCoPData,
			pCopCtrlData,
			pCoPTag,
			m_protocolID
		);

		break;

	case CLL_KW82:

		cop = std::make_shared<KW82ComPrimitive>(
			CoPType,
			CoPDataSize,
			pCoPData,
			pCopCtrlData,
			pCoPTag,
			m_protocolID
		);

		break;

	default:

		CLL_TRACE(
			"GENERIC",
			"Using GenericComPrimitive protocol=%u dPduProtocolId=%u busTypeId=%u",
			static_cast<unsigned int>(m_protocol),
			m_dPduProtocolId,
			m_busTypeId
		);

		cop = std::make_shared<GenericComPrimitive>(
			CoPType,
			CoPDataSize,
			pCoPData,
			pCopCtrlData,
			pCoPTag,
			m_protocolID,
			m_dPduProtocolId
		);

		break;
	}

	const std::lock_guard<std::mutex> lock(m_copLock);
	m_copQueue.push_back(cop);

	const UNUM32 handle = cop->getHandle();
	CLL_TRACE("EXIT", "hCoP=%u queueSize=%zu", handle, m_copQueue.size());
	return handle;
}

long ComLogicalLink::StartMsgFilter(unsigned long filterType)
{
	long ret = STATUS_NOERROR;
	CLL_TRACE("ENTER", "hCLL=%u filterType=%lu running=%s channelID=%lu", m_hCLL, filterType, m_running ? "true" : "false", m_channelID);

	if (!m_running)
	{
		//D-PDU host might not call PDUConnect before this
		CLL_TRACE("BEFORE", "Connect (implicit because m_running=false)");
		const long connectRet = Connect();
		CLL_TRACE("AFTER", "Connect ret=%ld channelID=%lu", connectRet, m_channelID);
		if (connectRet != STATUS_NOERROR)
		{
			CLL_TRACE("EXIT", "ret=%ld because implicit Connect failed", connectRet);
			return connectRet;
		}
	}
	
	unsigned long j2534FilterType = 0;
	if (filterType == PDU_FLT_PASS)
		j2534FilterType = PASS_FILTER;
	else if (filterType == PDU_FLT_BLOCK)
		j2534FilterType = BLOCK_FILTER;
	else
		return ERR_INVALID_FILTER_ID;
	unsigned long filterId = 0;
	PASSTHRU_MSG mask = { m_protocolID, 0, 0, 0, 4, 4, {0, 0, 0, 0} };
	PASSTHRU_MSG pattern = { m_protocolID, 0, 0, 0, 4, 4, {0, 0, 0, 0} };
	CLL_TRACE("BEFORE", "_PassThruStartMsgFilter ChannelID=%lu FilterType=%u mask=%p pattern=%p filterId=%p", m_channelID, j2534FilterType, &mask, &pattern, &filterId);
	ret = _PassThruStartMsgFilter(m_channelID, j2534FilterType, &mask, &pattern, nullptr, &filterId);
	CLL_TRACE("AFTER", "_PassThruStartMsgFilter ret=%ld filterId=%lu", ret, filterId);
	if (ret == STATUS_NOERROR)
		m_filterIDs.push_back(filterId);
	
	if (ret != STATUS_NOERROR)
	{
		char err[256];
		_PassThruGetLastError(err);
		CLL_TRACE("RESULT", "_PassThruStartMsgFilter failed ret=%ld error=%s", ret, err);
	}

	CLL_TRACE("EXIT", "ret=%ld channelID=%lu filterId=%lu", ret, m_channelID, filterId);
	return ret;
}

long ComLogicalLink::ClearMsgFilters()
{
	if (!m_running || m_channelID == 0)
		return STATUS_NOERROR;
	const long ret = _PassThruIoctl(m_channelID, CLEAR_MSG_FILTERS, nullptr, nullptr);
	if (ret == STATUS_NOERROR)
		m_filterIDs.clear();
	return ret;
}

void ComLogicalLink::RegisterEventCallback(CALLBACKFNC cb)
{
	CLL_TRACE("ENTER", "hCLL=%u callback=%p", m_hCLL, cb);
	m_eventCallbackFnc = cb;
	CLL_TRACE("EXIT", "callback=%p", m_eventCallbackFnc);
}

bool ComLogicalLink::GetEvent(PDU_EVENT_ITEM* & pEvt)
{
	CLL_TRACE("ENTER", "hCLL=%u pEvtOut=%p", m_hCLL, &pEvt);
	{
		const std::lock_guard<std::mutex> lock(m_eventLock);
		if (m_eventQueue.empty())
		{
			CLL_TRACE("EXIT", "available=false queueEmpty=true");
			return false;
		}

		pEvt = m_eventQueue.front();
		m_eventQueue.pop();
	}

	if (pEvt->ItemType == PDU_IT_STATUS && pEvt->pData != nullptr)
	{
		const PDU_STATUS_DATA state = *(PDU_STATUS_DATA*)(pEvt->pData);
		CLL_TRACE("EVENT", "hCoP=%u ItemType=%u state=0x%x event=%p", pEvt->hCop, pEvt->ItemType, static_cast<unsigned int>(state), pEvt);
	}
	else
	{
		CLL_TRACE("EVENT", "hCoP=%u ItemType=%u event=%p", pEvt->hCop, pEvt->ItemType, pEvt);
	}

	CLL_TRACE("EXIT", "available=true hCoP=%u ItemType=%u", pEvt->hCop, pEvt->ItemType);
	return true;
}

void ComLogicalLink::SignalEvent(PDU_EVENT_ITEM* pEvt)
{
	CLL_TRACE("ENTER", "hCLL=%u event=%p", m_hCLL, pEvt);
	if (pEvt == nullptr)
	{
		CLL_TRACE("EXIT", "event=null no-op");
		return;
	}

	CLL_TRACE("BEFORE", "QueueEvent event=%p", pEvt);
	QueueEvent(pEvt);
	CLL_TRACE("AFTER", "QueueEvent");
	CLL_TRACE("BEFORE", "SignalEvents");
	SignalEvents();
	CLL_TRACE("AFTER", "SignalEvents");
	CLL_TRACE("EXIT", "event=%p", pEvt);
}

long ComLogicalLink::SetComParam(const PDU_PARAM_ITEM* pParamItem)
{
	CLL_TRACE(
		"ENTER",
		"hCLL=%u paramItem=%p",
		m_hCLL,
		pParamItem
	);

	if (pParamItem == nullptr)
	{
		CLL_TRACE("RESULT", "pParamItem=null");
		CLL_TRACE("EXIT", "ret=%ld", PDU_ERR_INVALID_PARAMETERS);
		return PDU_ERR_INVALID_PARAMETERS;
	}

	if (pParamItem->pComParamData == nullptr)
	{
		CLL_TRACE("RESULT", "pComParamData=null ParamId=%u",
			pParamItem->ComParamId);

		CLL_TRACE("EXIT", "ret=%ld", PDU_ERR_INVALID_PARAMETERS);
		return PDU_ERR_INVALID_PARAMETERS;
	}

	ComParamValue value;

	value.id = pParamItem->ComParamId;
	value.paramClass = pParamItem->ComParamClass;
	value.dataType = pParamItem->ComParamDataType;

	/*
	 * Currently handle the scalar types explicitly.
	 *
	 * PDU_PT_UNUM32 is the type Tech2Win is currently using:
	 * 0x00000105 = 261.
	 */
	switch (pParamItem->ComParamDataType)
	{
	case PDU_PT_UNUM8:
		value.data.resize(sizeof(UNUM8));
		memcpy(
			value.data.data(),
			pParamItem->pComParamData,
			sizeof(UNUM8)
		);
		break;

	case PDU_PT_SNUM8:
		value.data.resize(sizeof(SNUM8));
		memcpy(
			value.data.data(),
			pParamItem->pComParamData,
			sizeof(SNUM8)
		);
		break;

	case PDU_PT_UNUM16:
		value.data.resize(sizeof(UNUM16));
		memcpy(
			value.data.data(),
			pParamItem->pComParamData,
			sizeof(UNUM16)
		);
		break;

	case PDU_PT_SNUM16:
		value.data.resize(sizeof(SNUM16));
		memcpy(
			value.data.data(),
			pParamItem->pComParamData,
			sizeof(SNUM16)
		);
		break;

	case PDU_PT_UNUM32:
	{
		value.data.resize(sizeof(UNUM32));

		memcpy(
			value.data.data(),
			pParamItem->pComParamData,
			sizeof(UNUM32)
		);

		UNUM32 parameterValue =
			*(reinterpret_cast<const UNUM32*>(
				pParamItem->pComParamData
				));

		CLL_TRACE(
			"PARAM",
			"ParamId=%u Class=%u Type=UNUM32 Value=%lu (0x%08X)",
			pParamItem->ComParamId,
			pParamItem->ComParamClass,
			parameterValue,
			parameterValue
		);

		break;
	}

	case PDU_PT_SNUM32:
		value.data.resize(sizeof(SNUM32));
		memcpy(
			value.data.data(),
			pParamItem->pComParamData,
			sizeof(SNUM32)
		);
		break;

	default:
		CLL_TRACE(
			"RESULT",
			"Unsupported ComParamDataType=%u ParamId=%u",
			pParamItem->ComParamDataType,
			pParamItem->ComParamId
		);

		CLL_TRACE(
			"EXIT",
			"ret=%ld",
			PDU_ERR_COMPARAM_NOT_SUPPORTED
		);

		return PDU_ERR_COMPARAM_NOT_SUPPORTED;
	}

	/*
	 * PDUSetComParam modifies the WORKING buffer.
	 * It does not modify the ACTIVE buffer yet.
	 */
	m_workingParams[pParamItem->ComParamId] = std::move(value);

	CLL_TRACE(
		"STATE",
		"WorkingBuffer ParamId=%u updated. WorkingCount=%zu",
		pParamItem->ComParamId,
		m_workingParams.size()
	);

	CLL_TRACE("EXIT", "ret=%ld", STATUS_NOERROR);

	return STATUS_NOERROR;
}

long ComLogicalLink::UpdateComParams()
{
	CLL_TRACE(
		"ENTER",
		"hCLL=%u working=%zu active=%zu",
		m_hCLL,
		m_workingParams.size(),
		m_activeParams.size()
	);

	m_activeParams = m_workingParams;

	CLL_TRACE(
		"STATE",
		"WorkingBuffer -> ActiveBuffer count=%zu",
		m_activeParams.size()
	);

	/*
	 * Dump active parameters for debugging.
	 */
	for (const auto& entry : m_activeParams)
	{
		const ComParamValue& param = entry.second;

		if (param.dataType == PDU_PT_UNUM32 &&
			param.data.size() >= sizeof(UNUM32))
		{
			UNUM32 value =
				*(reinterpret_cast<const UNUM32*>(
					param.data.data()
					));

			CLL_TRACE(
				"ACTIVE_PARAM",
				"ParamId=%u Class=%u Type=UNUM32 Value=%lu (0x%08X)",
				param.id,
				param.paramClass,
				value,
				value
			);
		}
		else
		{
			CLL_TRACE(
				"ACTIVE_PARAM",
				"ParamId=%u Class=%u Type=%u Size=%zu",
				param.id,
				param.paramClass,
				param.dataType,
				param.data.size()
			);
		}
	}

	CLL_TRACE("EXIT", "ret=%ld", STATUS_NOERROR);

	return STATUS_NOERROR;
}

long ComLogicalLink::RestoreComParams()
{
	CLL_TRACE(
		"ENTER",
		"hCLL=%u working=%zu active=%zu",
		m_hCLL,
		m_workingParams.size(),
		m_activeParams.size()
	);

	m_workingParams = m_activeParams;

	CLL_TRACE(
		"STATE",
		"ActiveBuffer -> WorkingBuffer count=%zu",
		m_workingParams.size()
	);

	CLL_TRACE("EXIT", "ret=%ld", STATUS_NOERROR);

	return STATUS_NOERROR;
}

void ComLogicalLink::SignalEvents()
{
	CLL_TRACE(
		"ENTER",
		"hMod=%u hCLL=%u callback=%p",
		m_hMod,
		m_hCLL,
		m_eventCallbackFnc
	);

	if (m_eventCallbackFnc == nullptr)
	{
		CLL_TRACE(
			"RESULT",
			"No event callback registered; event remains queued"
		);

		CLL_TRACE("EXIT", "");
		return;
	}

	CLL_TRACE(
		"BEFORE",
		"eventCallback event=PDU_EVT_DATA_AVAILABLE hMod=%u hCLL=%u",
		m_hMod,
		m_hCLL
	);

	m_eventCallbackFnc(
		PDU_EVT_DATA_AVAILABLE,
		m_hMod,
		m_hCLL,
		nullptr,
		nullptr
	);

	CLL_TRACE(
		"AFTER",
		"eventCallback event=PDU_EVT_DATA_AVAILABLE"
	);

	CLL_TRACE("EXIT", "");
}

void ComLogicalLink::QueueEvent(PDU_EVENT_ITEM* pEvt)
{
	CLL_TRACE("ENTER", "hCLL=%u event=%p", m_hCLL, pEvt);
	if (pEvt == nullptr)
	{
		CLL_TRACE("EXIT", "event=null not queued");
		return;
	}
	CLL_TRACE("EVENT", "hCoP=%u ItemType=%u", pEvt->hCop, pEvt->ItemType);

	auto now = std::chrono::high_resolution_clock::now();
	auto duration = now.time_since_epoch();
	pEvt->Timestamp = (UNUM32)(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());

	{
		const std::lock_guard<std::mutex> lock(m_eventLock);
		m_eventQueue.push(pEvt);
		CLL_TRACE("STATE", "eventQueueSize=%zu", m_eventQueue.size());
	}
	CLL_TRACE("EXIT", "timestamp=%u", pEvt->Timestamp);
}

void ComLogicalLink::run()
{
	CLL_TRACE("ENTER", "channelID=%lu running=%s", m_channelID, m_running ? "true" : "false");

	while (m_running)
	{
		std::vector<std::shared_ptr<ComPrimitive>> pending;
		{
			const std::lock_guard<std::mutex> lock(m_copLock);
			m_copQueue.erase(
				std::remove_if(
					m_copQueue.begin(),
					m_copQueue.end(),
					[](const std::shared_ptr<ComPrimitive>& cop)
					{
						return cop->getHandle() == 0;
					}
				),
				m_copQueue.end()
			);
			pending = m_copQueue;
		}

		for (const auto& cop : pending)
		{
			if (cop->getHandle() != 0)
			{
				CLL_TRACE("ACTION", "ProcessCop hCoP=%u type=%u", cop->getHandle(), cop->getType());
				ProcessCop(cop);
			}
		}
		

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CLL_TRACE("EXIT", "channelID=%lu running=%s", m_channelID, m_running ? "true" : "false");
}

void ComLogicalLink::ProcessCop(std::shared_ptr<ComPrimitive> cop)
{
	long ret = STATUS_NOERROR;
	PDU_EVENT_ITEM* pEvt = nullptr;
	CLL_TRACE("ENTER", "channelID=%lu hCoP=%u type=%u", m_channelID, cop->getHandle(), cop->getType());
	if (cop->GetStatus() == PDU_COPST_CANCELLED)
	{
		CLL_TRACE("ACTION", "discarding cancelled primitive hCoP=%u", cop->getHandle());
		const std::lock_guard<std::mutex> lock(m_copLock);
		cop->Destroy();
		return;
	}

	CLL_TRACE("BEFORE", "ComPrimitive::Execute hCoP=%u", cop->getHandle());
	cop->Execute(pEvt);
	CLL_TRACE("AFTER", "ComPrimitive::Execute event=%p", pEvt);
	SignalEvent(pEvt);

	switch (cop->getType())
	{
	case PDU_COPT_UPDATEPARAM:
		CLL_TRACE("BEFORE", "UpdateComParams hCoP=%u", cop->getHandle());
		ret = UpdateComParams();
		CLL_TRACE("AFTER", "UpdateComParams ret=%ld", ret);
		break;
	case PDU_COPT_RESTORE_PARAM:
		CLL_TRACE("BEFORE", "RestoreComParams hCoP=%u", cop->getHandle());
		ret = RestoreComParams();
		CLL_TRACE("AFTER", "RestoreComParams ret=%ld", ret);
		break;
	case PDU_COPT_DELAY:
		CLL_TRACE("BEFORE", "Delay hCoP=%u timeMs=%u", cop->getHandle(), cop->getTime());
		for (UNUM32 elapsed = 0; elapsed < cop->getTime() && m_running && cop->GetStatus() != PDU_COPST_CANCELLED; elapsed += 10)
		{
			const UNUM32 remaining = cop->getTime() - elapsed;
			std::this_thread::sleep_for(std::chrono::milliseconds(remaining < 10 ? remaining : 10));
		}
		CLL_TRACE("AFTER", "Delay hCoP=%u", cop->getHandle());
		break;
	case PDU_COPT_STARTCOMM:
		CLL_TRACE("BEFORE", "StartComm hCoP=%u", cop->getHandle());
		ret = StartComm(cop);
		CLL_TRACE("AFTER", "StartComm ret=%ld", ret);
		break;
	case PDU_COPT_STOPCOMM:
		CLL_TRACE("BEFORE", "StopComm hCoP=%u", cop->getHandle());
		ret = StopComm(cop);
		CLL_TRACE("AFTER", "StopComm ret=%ld", ret);
		break;
	case PDU_COPT_SENDRECV:
		CLL_TRACE("BEFORE", "SendRecv hCoP=%u", cop->getHandle());
		ret = SendRecv(cop);
		CLL_TRACE("AFTER", "SendRecv ret=%ld", ret);
		break;
	}

	if (cop->GetStatus() == PDU_COPST_CANCELLED)
	{
		const std::lock_guard<std::mutex> lock(m_copLock);
		cop->Destroy();
		return;
	}

	if (ret != STATUS_NOERROR)
	{
		T_PDU_ERR_EVT errorCode = PDU_ERR_EVT_RX_ERROR;
		if (ret == ERR_TIMEOUT || ret == ERR_BUFFER_EMPTY)
			errorCode = PDU_ERR_EVT_RX_TIMEOUT;
		else if (cop->getType() == PDU_COPT_STARTCOMM)
			errorCode = PDU_ERR_EVT_INIT_ERROR;
		else if (cop->getType() != PDU_COPT_SENDRECV)
			errorCode = PDU_ERR_EVT_PROT_ERR;

		pEvt = new PDU_EVENT_ITEM{};
		pEvt->hCop = cop->getHandle();
		pEvt->ItemType = PDU_IT_ERROR;
		pEvt->pCoPTag = cop->getTag();
		pEvt->pData = new PDU_ERROR_DATA{};
		static_cast<PDU_ERROR_DATA*>(pEvt->pData)->ErrorCodeId = errorCode;
		static_cast<PDU_ERROR_DATA*>(pEvt->pData)->ExtraErrorInfoId = static_cast<UNUM32>(ret);
		const auto errorTime = std::chrono::steady_clock::now().time_since_epoch();
		const UNUM32 errorTimestamp = static_cast<UNUM32>(std::chrono::duration_cast<std::chrono::microseconds>(errorTime).count());
		pEvt->Timestamp = errorTimestamp;
		SignalEvent(pEvt);
		const std::lock_guard<std::mutex> errorLock(m_errorLock);
		m_lastErrorCode = errorCode;
		m_lastErrorCoP = cop->getHandle();
		m_lastErrorTimestamp = errorTimestamp;
		m_lastExtraErrorInfo = static_cast<UNUM32>(ret);
	}

	pEvt = nullptr;
	CLL_TRACE("BEFORE", "ComPrimitive::Finish hCoP=%u", cop->getHandle());
	const bool oneShotLifecyclePrimitive =
		cop->getType() == PDU_COPT_STARTCOMM ||
		cop->getType() == PDU_COPT_STOPCOMM;
	cop->Finish(pEvt, ret != STATUS_NOERROR || oneShotLifecyclePrimitive);
	CLL_TRACE("AFTER", "ComPrimitive::Finish event=%p", pEvt);
	SignalEvent(pEvt);
	if (cop->GetStatus() == PDU_COPST_FINISHED)
	{
		CLL_TRACE("ACTION", "removing finished primitive hCoP=%u", cop->getHandle());
		const std::lock_guard<std::mutex> lock(m_copLock);
		m_finishedCopHandles.insert(cop->getHandle());
		cop->Destroy();
	}

	if (ret != STATUS_NOERROR)
	{
		char err[256];
		_PassThruGetLastError(err);
		CLL_TRACE("RESULT", "primitive failed hCoP=%u type=%u ret=%ld error=%s", cop->getHandle(), cop->getType(), ret, err);
	}
	CLL_TRACE("EXIT", "hCoP=%u ret=%ld", cop->getHandle(), ret);
}

long ComLogicalLink::StartComm(std::shared_ptr<ComPrimitive> cop)
{
	long ret = STATUS_NOERROR;

	PDU_EVENT_ITEM* pEvt = nullptr;
	CLL_TRACE("ENTER", "channelID=%lu hCoP=%u", m_channelID, cop->getHandle());

	CLL_TRACE("BEFORE", "_PassThruIoctl CLEAR_RX_BUFFER ChannelID=%lu", m_channelID);
	_PassThruIoctl(m_channelID, CLEAR_RX_BUFFER, nullptr, nullptr);
	CLL_TRACE("AFTER", "_PassThruIoctl CLEAR_RX_BUFFER");

	CLL_TRACE("BEFORE", "ComPrimitive::StartComm channelID=%lu hCoP=%u", m_channelID, cop->getHandle());
	ret = cop->StartComm(m_channelID, pEvt);
	CLL_TRACE("AFTER", "ComPrimitive::StartComm ret=%ld event=%p", ret, pEvt);
	if (ret == STATUS_NOERROR)
	{
		QueueEvent(pEvt);

		m_status = PDU_CLLST_COMM_STARTED;
		CLL_TRACE("STATE", "m_status=0x%x", static_cast<unsigned int>(m_status));

		pEvt = new PDU_EVENT_ITEM;
		pEvt->hCop = PDU_HANDLE_UNDEF;
		pEvt->ItemType = PDU_IT_STATUS;
		pEvt->pCoPTag = nullptr;
		pEvt->pData = new PDU_STATUS_DATA;
		*(PDU_STATUS_DATA*)(pEvt->pData) = m_status;

		SignalEvent(pEvt);
	}
	
	CLL_TRACE("EXIT", "ret=%ld status=0x%x", ret, static_cast<unsigned int>(m_status));
	return ret;
}

long ComLogicalLink::StopComm(std::shared_ptr<ComPrimitive> cop)
{
	long ret = STATUS_NOERROR;

	PDU_EVENT_ITEM* pEvt = nullptr;
	CLL_TRACE("ENTER", "channelID=%lu hCoP=%u", m_channelID, cop->getHandle());

	CLL_TRACE("BEFORE", "ComPrimitive::StopComm channelID=%lu hCoP=%u", m_channelID, cop->getHandle());
	ret = cop->StopComm(m_channelID, pEvt);
	CLL_TRACE("AFTER", "ComPrimitive::StopComm ret=%ld event=%p", ret, pEvt);
	if (ret == STATUS_NOERROR)
	{
		m_status = PDU_CLLST_ONLINE;
		CLL_TRACE("STATE", "m_status=0x%x", static_cast<unsigned int>(m_status));

		PDU_EVENT_ITEM* pEvt = nullptr;
		pEvt = new PDU_EVENT_ITEM;
		pEvt->hCop = PDU_HANDLE_UNDEF;
		pEvt->ItemType = PDU_IT_STATUS;
		pEvt->pCoPTag = nullptr;
		pEvt->pData = new PDU_STATUS_DATA;
		*(PDU_STATUS_DATA*)(pEvt->pData) = m_status;

		SignalEvent(pEvt);

		CLL_TRACE("BEFORE", "_PassThruIoctl CLEAR_RX_BUFFER ChannelID=%lu", m_channelID);
		ret = _PassThruIoctl(m_channelID, CLEAR_RX_BUFFER, nullptr, nullptr);
		CLL_TRACE("AFTER", "_PassThruIoctl CLEAR_RX_BUFFER ret=%ld", ret);
	}

	CLL_TRACE("EXIT", "ret=%ld status=0x%x", ret, static_cast<unsigned int>(m_status));
	return ret;
}

long ComLogicalLink::SendRecv(std::shared_ptr<ComPrimitive> cop)
{
	long ret = STATUS_NOERROR;
	PDU_EVENT_ITEM* pEvt = nullptr;
	CLL_TRACE("ENTER", "channelID=%lu hCoP=%u", m_channelID, cop->getHandle());

	CLL_TRACE("BEFORE", "ComPrimitive::SendRecv channelID=%lu hCoP=%u", m_channelID, cop->getHandle());
	ret = cop->SendRecv(m_channelID, pEvt);
	CLL_TRACE("AFTER", "ComPrimitive::SendRecv ret=%ld event=%p", ret, pEvt);
	if (ret == STATUS_NOERROR)
	{
		SignalEvent(pEvt);
	}


	CLL_TRACE("EXIT", "ret=%ld", ret);
	return ret;
}

#undef CLL_TRACE
