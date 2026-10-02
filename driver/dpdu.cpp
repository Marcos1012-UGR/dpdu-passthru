#include "pch.h"
#include "dpdu.h"
#include "Logger.h"
#include "pdu_api.h"
#include "j2534/shim_loader.h"
#include "ComLogicalLink.h"

#include <chrono>
#include <vector>
#include <string>
#include <map>
#include <iomanip>

static std::vector<cPassThruInfo> m_registryList;
static unsigned long m_deviceID = 0;
static std::map<UNUM32, std::string>m_objectIdMap;
static std::map<UNUM32, std::shared_ptr<ComLogicalLink>> m_commChannels;

#define DPDU_TRACE(event, ...) LOGGER.trace("dpdu.cpp", __FUNCTION__, event, __VA_ARGS__)
#define DPDU_RETURN(value) do { const T_PDU_ERROR traceRet = (value); DPDU_TRACE("EXIT", "ret=0x%08X", static_cast<unsigned int>(traceRet)); return traceRet; } while (0)

T_PDU_ERROR __stdcall PDUConstruct(CHAR8* OptionStr, void* pAPITag)
{
	DPDU_TRACE("ENTER", "OptionStr=%s pAPITag=%p", OptionStr != nullptr ? OptionStr : "<null>", pAPITag);
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUDestruct()
{
	DPDU_TRACE("ENTER", "");
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUModuleConnect(UNUM32 hMod)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;

	DPDU_TRACE("ENTER", "hMod=%u registryCount=%zu", hMod, m_registryList.size());
	if (hMod >= m_registryList.size())
		DPDU_RETURN(PDU_ERR_MODULE_NOT_CONNECTED);
	DPDU_TRACE("MODULE", "library=%s", m_registryList[hMod].FunctionLibrary.c_str());

	DPDU_TRACE("BEFORE", "shim_loadLibrary");
	bool sret = shim_loadLibrary(m_registryList[hMod].FunctionLibrary.c_str());
	DPDU_TRACE("AFTER", "shim_loadLibrary loaded=%s", sret ? "true" : "false");
	if (!sret)
	{
		DPDU_TRACE("RESULT", "shim_loadLibrary failed");
		ret = PDU_ERR_MODULE_NOT_CONNECTED;
	}

	if (ret == PDU_STATUS_NOERROR)
	{
		DPDU_TRACE("BEFORE", "_PassThruOpen pName=null pDeviceID=%p", &m_deviceID);
		long ptret = _PassThruOpen(NULL, &m_deviceID);
		DPDU_TRACE("AFTER", "_PassThruOpen ret=%ld DeviceID=%lu", ptret, m_deviceID);
		if (ptret != STATUS_NOERROR)
		{
			DPDU_TRACE("RESULT", "_PassThruOpen failed ret=%ld", ptret);
			ret = PDU_ERR_MODULE_NOT_CONNECTED;
		}
	}

	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUModuleDisconnect(UNUM32 hMod)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;

	DPDU_TRACE("ENTER", "hMod=%u DeviceID=%lu", hMod, m_deviceID);

	DPDU_TRACE("BEFORE", "_PassThruClose DeviceID=%lu", m_deviceID);
	long ptret = _PassThruClose(m_deviceID);
	DPDU_TRACE("AFTER", "_PassThruClose ret=%ld", ptret);
	if (ptret != STATUS_NOERROR)
	{
		DPDU_TRACE("RESULT", "_PassThruClose failed ret=%ld", ptret);
		ret = PDU_ERR_MODULE_NOT_CONNECTED;
	}

	m_deviceID = 0;
	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUGetTimestamp(UNUM32 hMod, UNUM32* pTimestamp)
{
	DPDU_TRACE("ENTER", "hMod=%u pTimestamp=%p", hMod, pTimestamp);
	if (pTimestamp == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	*pTimestamp = static_cast<UNUM32>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUIoCtl(UNUM32 hMod, UNUM32 hCLL, UNUM32 IoCtlCommandId, PDU_DATA_ITEM* pInputData, PDU_DATA_ITEM** pOutputData)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;
	long cllret = STATUS_NOERROR;
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u IoCtlCommandId=%u pInputData=%p pOutputData=%p", hMod, hCLL, IoCtlCommandId, pInputData, pOutputData);
	if (pOutputData != nullptr)
		*pOutputData = nullptr;

	auto itCLL = m_commChannels.find(hCLL);

	auto it = m_objectIdMap.find(IoCtlCommandId);
	if (it != m_objectIdMap.end())
	{
		if (it->second == std::string("PDU_IOCTL_READ_VBATT"))
		{
			if (pOutputData == nullptr)
				DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
			unsigned long volt = 0;

			DPDU_TRACE("BEFORE", "_PassThruIoctl ChannelID=0 Ioctl=READ_VBATT input=null output=%p", &volt);
			long ptret = _PassThruIoctl(0, READ_VBATT, NULL, &volt);
			if (ptret == STATUS_NOERROR)
				DPDU_TRACE("AFTER", "_PassThruIoctl ret=%ld voltage=%lu", ptret, volt);
			else
				DPDU_TRACE("AFTER", "_PassThruIoctl ret=%ld voltage unavailable", ptret);
			if (ptret != STATUS_NOERROR)
			{
				DPDU_TRACE("RESULT", "_PassThruIoctl failed ret=%ld", ptret);
				ret = PDU_ERR_CABLE_UNKNOWN;
			}

			*pOutputData = new PDU_DATA_ITEM{};
			(*pOutputData)->ItemType = PDU_IT_IO_UNUM32;
			(*pOutputData)->pData = new UNUM32;
			*(UNUM32*)((*pOutputData)->pData) = volt;
		}
		else if (it->second == std::string("PDU_IOCTL_CLEAR_MSG_FILTER"))
		{
			if (itCLL == m_commChannels.end())
				DPDU_RETURN(PDU_ERR_CLL_NOT_CONNECTED);
			cllret = itCLL->second->ClearMsgFilters();
			if (cllret != STATUS_NOERROR)
				ret = PDU_ERR_CABLE_UNKNOWN;
		}
		else if (it->second == std::string("PDU_IOCTL_START_MSG_FILTER"))
		{
			if (pInputData == nullptr || pInputData->pData == nullptr)
				DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
			DPDU_TRACE("INPUT", "START_MSG_FILTER ItemType=0x%x data=%u", pInputData->ItemType, *(UNUM32*)(pInputData->pData));

			if (itCLL != m_commChannels.end())
			{
				DPDU_TRACE("BEFORE", "ComLogicalLink::StartMsgFilter hCLL=%u filterType=%lu", hCLL, *(unsigned long*)(pInputData->pData));
				cllret = itCLL->second->StartMsgFilter(*(unsigned long*)(pInputData->pData));
				DPDU_TRACE("AFTER", "ComLogicalLink::StartMsgFilter ret=%ld", cllret);

				if (cllret != STATUS_NOERROR)
				{
					DPDU_TRACE("RESULT", "ComLogicalLink::StartMsgFilter failed ret=%ld", cllret);
					ret = PDU_ERR_CABLE_UNKNOWN;
				}
			}
			else
			{
				ret = PDU_ERR_CLL_NOT_CONNECTED;
			}
		}
	}

	DPDU_TRACE("OUTPUT", "pOutputData=%p output=%p ret=0x%08X", pOutputData, pOutputData != nullptr ? *pOutputData : nullptr, static_cast<unsigned int>(ret));
	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUGetVersion(UNUM32 hMod, PDU_VERSION_DATA* pVersionData)
{
	DPDU_TRACE("ENTER", "hMod=%u pVersionData=%p", hMod, pVersionData);
	if (pVersionData == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	memset(pVersionData, 0, sizeof(*pVersionData));
	strncpy_s(pVersionData->HwName, "Nyanko J2534", _TRUNCATE);
	strncpy_s(pVersionData->VendorName, "Nyanko", _TRUNCATE);
	strncpy_s(pVersionData->PDUApiSwName, "dpdu-passthru", _TRUNCATE);
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUGetStatus(
	UNUM32 hMod,
	UNUM32 hCLL,
	UNUM32 hCoP,
	T_PDU_STATUS* pStatusCode,
	UNUM32* pTimestamp,
	UNUM32* pExtraInfo)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;
	T_PDU_STATUS status = PDU_MODST_NOT_AVAIL;

	DPDU_TRACE(
		"ENTER",
		"hMod=%u hCLL=%u hCoP=%u pStatusCode=%p pTimestamp=%p pExtraInfo=%p",
		hMod,
		hCLL,
		hCoP,
		pStatusCode,
		pTimestamp,
		pExtraInfo
	);

	if (pStatusCode == nullptr)
	{
		DPDU_TRACE(
			"ERROR",
			"pStatusCode == nullptr"
		);

		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	}

	//
	// Module status
	//

	if (hCLL == PDU_HANDLE_UNDEF &&
		hCoP == PDU_HANDLE_UNDEF)
	{
		DPDU_TRACE(
			"PATH",
			"Requesting MODULE status"
		);

		// Existing behavior
		status = PDU_MODST_READY;
	}

	//
	// CLL status
	//

	else if (hCoP == PDU_HANDLE_UNDEF)
	{
		DPDU_TRACE(
			"PATH",
			"Requesting CLL status hCLL=%u",
			hCLL
		);

		auto it = m_commChannels.find(hCLL);

		if (it != m_commChannels.end())
		{
			ret = it->second->GetStatus(status);

			DPDU_TRACE(
				"CLL_STATUS",
				"hCLL=%u ret=0x%08X status=0x%04X",
				hCLL,
				static_cast<unsigned int>(ret),
				static_cast<unsigned int>(status)
			);
		}
		else
		{
			DPDU_TRACE(
				"ERROR",
				"hCLL=%u NOT FOUND in m_commChannels",
				hCLL
			);

			ret = PDU_ERR_CLL_NOT_CONNECTED;
		}
	}

	//
	// CoP status
	//

	else
	{
		DPDU_TRACE(
			"PATH",
			"Requesting CoP status hCLL=%u hCoP=%u",
			hCLL,
			hCoP
		);

		auto it = m_commChannels.find(hCLL);

		if (it != m_commChannels.end())
		{
			ret = it->second->GetStatus(
				hCoP,
				status
			);

			DPDU_TRACE(
				"COP_STATUS",
				"hCLL=%u hCoP=%u ret=0x%08X status=0x%04X",
				hCLL,
				hCoP,
				static_cast<unsigned int>(ret),
				static_cast<unsigned int>(status)
			);
		}
		else
		{
			DPDU_TRACE(
				"ERROR",
				"hCLL=%u NOT FOUND in m_commChannels",
				hCLL
			);

			ret = PDU_ERR_CLL_NOT_CONNECTED;
		}
	}

	*pStatusCode = status;
	if (pTimestamp != nullptr)
		*pTimestamp = 0;

	if (pExtraInfo != nullptr)
	{
		*pExtraInfo = 0;
	}

	DPDU_TRACE(
		"OUTPUT",
		"hCLL=%u hCoP=%u status=0x%04X extraInfo=%u ret=0x%08X",
		hCLL,
		hCoP,
		static_cast<unsigned int>(*pStatusCode),
		pExtraInfo != nullptr ? *pExtraInfo : 0,
		static_cast<unsigned int>(ret)
	);

	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUGetLastError(UNUM32 hMod, UNUM32 hCLL, T_PDU_ERR_EVT* pErrorCode, UNUM32* phCoP, UNUM32* pTimestamp, UNUM32* pExtraErrorInfo)
{
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u pErrorCode=%p phCoP=%p pTimestamp=%p pExtraErrorInfo=%p", hMod, hCLL, pErrorCode, phCoP, pTimestamp, pExtraErrorInfo);
	if (pErrorCode == nullptr || phCoP == nullptr || pTimestamp == nullptr || pExtraErrorInfo == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	auto it = m_commChannels.find(hCLL);
	if (it == m_commChannels.end())
	{
		*pErrorCode = PDU_ERR_EVT_NOERROR;
		*phCoP = PDU_HANDLE_UNDEF;
		*pTimestamp = 0;
		*pExtraErrorInfo = 0;
	}
	else
	{
		it->second->GetLastError(*pErrorCode, *phCoP, *pTimestamp, *pExtraErrorInfo);
	}
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

static Protocol mapDpuProtocolToCLL(UNUM32 protocolId);

T_PDU_ERROR __stdcall PDUGetResourceStatus(PDU_RSC_STATUS_ITEM* pResourceStatus)
{
	DPDU_TRACE("ENTER", "pResourceStatus=%p", pResourceStatus);
	if (pResourceStatus == nullptr ||
		(pResourceStatus->NumEntries > 0 && pResourceStatus->pResourceStatusData == nullptr))
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	for (UNUM32 i = 0; i < pResourceStatus->NumEntries; ++i)
		pResourceStatus->pResourceStatusData[i].ResourceStatus = 0;
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUCreateComLogicalLink(
	UNUM32 hMod,
	PDU_RSC_DATA* pRscData,
	UNUM32 resourceId,
	void* pCllTag,
	UNUM32* phCLL,
	PDU_FLAG_DATA* pCllCreateFlag)
{
	UNUM32 idx = static_cast<UNUM32>(m_commChannels.size());
	while (m_commChannels.find(idx) != m_commChannels.end())
		++idx;
	Protocol protocol = CLL_UNKNOWN;

	DPDU_TRACE(
		"ENTER",
		"hMod=%u pRscData=%p resourceId=%u pCllTag=%p phCLL=%p pCllCreateFlag=%p",
		hMod,
		pRscData,
		resourceId,
		pCllTag,
		phCLL,
		pCllCreateFlag
	);
	if (phCLL == nullptr)
	{
		DPDU_TRACE("ERROR", "phCLL=NULL");
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	}

	//
	// ------------------------------------------------------------
	// Resource data
	// ------------------------------------------------------------
	//

	if (pRscData == nullptr)
	{
		DPDU_TRACE("ERROR", "pRscData == nullptr");
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	}

	std::vector<UNUM32> dlcPins;

	if (pRscData->pDLCPinData != nullptr)
	{
		for (UNUM32 i = 0;
			i < pRscData->NumPinData;
			++i)
		{
			dlcPins.push_back(
				pRscData->pDLCPinData[i].DLCPinNumber
			);
		}
	}

	std::vector<UNUM8> createFlags;

	if (pCllCreateFlag != nullptr &&
		pCllCreateFlag->pFlagData != nullptr &&
		pCllCreateFlag->NumFlagBytes > 0)
	{
		createFlags.assign(
			pCllCreateFlag->pFlagData,
			pCllCreateFlag->pFlagData +
			pCllCreateFlag->NumFlagBytes
		);
	}

	DPDU_TRACE(
		"RESOURCE",
		"BusTypeId=%u ProtocolId=%u NumPinData=%u pDLCPinData=%p",
		pRscData->BusTypeId,
		pRscData->ProtocolId,
		pRscData->NumPinData,
		pRscData->pDLCPinData
	);

	//
	// Dump every DLC pin
	//

	if (pRscData->pDLCPinData != nullptr)
	{
		for (UNUM32 i = 0; i < pRscData->NumPinData; ++i)
		{
			DPDU_TRACE(
				"PIN",
				"index=%u DLCPinNumber=%u DLCPinTypeId=0x%08X",
				i,
				pRscData->pDLCPinData[i].DLCPinNumber,
				pRscData->pDLCPinData[i].DLCPinTypeId
			);
		}
	}
	else
	{
		DPDU_TRACE(
			"PIN",
			"pDLCPinData == nullptr NumPinData=%u",
			pRscData->NumPinData
		);
	}

	protocol = mapDpuProtocolToCLL(pRscData->ProtocolId);
	DPDU_TRACE("PROTOCOL", "ProtocolId=%u mappedProtocol=%u", pRscData->ProtocolId, static_cast<unsigned int>(protocol));

	//
	// ------------------------------------------------------------
	// Create Logical Link
	// ------------------------------------------------------------
	//

	auto cll = std::shared_ptr<ComLogicalLink>(
		new ComLogicalLink(
			hMod,
			idx,
			m_deviceID,
			protocol,
			pRscData->ProtocolId,
			pRscData->BusTypeId,
			resourceId,
			dlcPins,
			createFlags
		)
	);

	m_commChannels.insert({ idx, cll });

	*phCLL = idx;

	DPDU_TRACE(
		"ACTION",
		"created hCLL=%u mappedChannels=%zu",
		*phCLL,
		m_commChannels.size()
	);

	//
	// ------------------------------------------------------------
	// CLL create flags
	// ------------------------------------------------------------
	//

	if (pCllCreateFlag == nullptr)
	{
		DPDU_TRACE(
			"FLAG",
			"pCllCreateFlag == nullptr"
		);
	}
	else
	{
		DPDU_TRACE(
			"FLAG",
			"NumFlagBytes=%u pFlagData=%p",
			pCllCreateFlag->NumFlagBytes,
			pCllCreateFlag->pFlagData
		);

		if (pCllCreateFlag->NumFlagBytes > 0 &&
			pCllCreateFlag->pFlagData != nullptr)
		{
			std::stringstream ss;

			ss << "FlagData:";

			for (UNUM32 i = 0;
				i < pCllCreateFlag->NumFlagBytes;
				++i)
			{
				ss << " "
					<< std::hex
					<< std::uppercase
					<< std::setw(2)
					<< std::setfill('0')
					<< static_cast<unsigned int>(
						pCllCreateFlag->pFlagData[i]
						);
			}

			DPDU_TRACE(
				"FLAG_DATA",
				"%s",
				ss.str().c_str()
			);
		}
	}

	//
	// ------------------------------------------------------------
	// Final state
	// ------------------------------------------------------------
	//

	DPDU_TRACE(
		"STATE",
		"hCLL=%u protocol=%u resourceId=%u",
		*phCLL,
		static_cast<unsigned int>(protocol),
		resourceId
	);

	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUDestroyComLogicalLink(UNUM32 hMod, UNUM32 hCLL)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u", hMod, hCLL);

	auto it = m_commChannels.find(hCLL);
	if (it != m_commChannels.end())
	{
		DPDU_TRACE("BEFORE", "ComLogicalLink::Disconnect hCLL=%u", hCLL);
		const long cllret = it->second->Disconnect();
		DPDU_TRACE("AFTER", "ComLogicalLink::Disconnect ret=%ld", cllret);
		it->second.reset();
		m_commChannels.erase(it);
	}

	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUConnect(UNUM32 hMod, UNUM32 hCLL)
{
	T_PDU_ERROR ret = PDU_ERR_CLL_NOT_CONNECTED;
	long cllret = ERR_FAILED;

	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u", hMod, hCLL);

	auto it = m_commChannels.find(hCLL);
	if (it != m_commChannels.end())
	{
		DPDU_TRACE("BEFORE", "ComLogicalLink::Connect hCLL=%u", hCLL);
		cllret = it->second->Connect();
		DPDU_TRACE("AFTER", "ComLogicalLink::Connect ret=%ld", cllret);
	}

	if (cllret == STATUS_NOERROR)
	{
		ret = PDU_STATUS_NOERROR;
	}
	else
	{
		DPDU_TRACE("RESULT", "ComLogicalLink::Connect failed ret=%ld", cllret);
	}

	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUDisconnect(UNUM32 hMod, UNUM32 hCLL)
{
	T_PDU_ERROR ret = PDU_ERR_CLL_NOT_CONNECTED;
	long cllret = ERR_FAILED;

	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u", hMod, hCLL);

	auto it = m_commChannels.find(hCLL);
	if (it != m_commChannels.end())
	{
		DPDU_TRACE("BEFORE", "ComLogicalLink::Disconnect hCLL=%u", hCLL);
		cllret = it->second->Disconnect();
		DPDU_TRACE("AFTER", "ComLogicalLink::Disconnect ret=%ld", cllret);
	}

	if (cllret == STATUS_NOERROR)
	{
		ret = PDU_STATUS_NOERROR;
	}
	else
	{
		DPDU_TRACE("RESULT", "ComLogicalLink::Disconnect failed ret=%ld", cllret);
	}

	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDULockResource(UNUM32 hMod, UNUM32 hCLL, UNUM32 LockMask)
{
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u LockMask=0x%x", hMod, hCLL, LockMask);
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUUnlockResource(UNUM32 hMod, UNUM32 hCLL, UNUM32 LockMask)
{
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u LockMask=0x%x", hMod, hCLL, LockMask);
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUGetComParam(
	UNUM32 hMod,
	UNUM32 hCLL,
	UNUM32 ParamId,
	PDU_PARAM_ITEM** pParamItem)
{
	DPDU_TRACE(
		"ENTER",
		"hMod=%u hCLL=%u ParamId=%u pParamItem=%p",
		hMod,
		hCLL,
		ParamId,
		pParamItem
	);

	if (pParamItem == nullptr)
	{
		DPDU_TRACE(
			"ERROR",
			"pParamItem == nullptr"
		);

		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	}

	*pParamItem = nullptr;

	if (ParamId == 43)
	{
		DPDU_TRACE(
			"PARAM",
			"Returning hardcoded ParamId=43"
		);

		*pParamItem = new PDU_PARAM_ITEM;

		(*pParamItem)->ItemType = PDU_IT_PARAM;
		(*pParamItem)->ComParamId = ParamId;
		(*pParamItem)->ComParamClass = PDU_PC_BUSTYPE;
		(*pParamItem)->ComParamDataType = PDU_PT_UNUM32;
		(*pParamItem)->pComParamData = new UNUM32;

		*(UNUM32*)((*pParamItem)->pComParamData) = 4800;

		DPDU_TRACE(
			"OUTPUT",
			"ParamId=%u Class=%u Type=%u Value=%lu item=%p data=%p",
			(*pParamItem)->ComParamId,
			(*pParamItem)->ComParamClass,
			(*pParamItem)->ComParamDataType,
			*(UNUM32*)((*pParamItem)->pComParamData),
			*pParamItem,
			(*pParamItem)->pComParamData
		);
	}
	else
	{
		DPDU_TRACE(
			"RESULT",
			"ParamId=%u not implemented -> returning NULL item",
			ParamId
		);
	}

	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUSetComParam(UNUM32 hMod, UNUM32 hCLL, PDU_PARAM_ITEM* pParamItem)
{
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u pParamItem=%p", hMod, hCLL, pParamItem);
	if (pParamItem == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	DPDU_TRACE("PARAM", "ComParamId=%u ComParamClass=%u ComParamDataType=%u pComParamData=%p", pParamItem->ComParamId, pParamItem->ComParamClass, pParamItem->ComParamDataType, pParamItem->pComParamData);

	auto it = m_commChannels.find(hCLL);
	if (it != m_commChannels.end())
	{
		DPDU_TRACE(
			"BEFORE",
			"ComLogicalLink::SetComParam hCLL=%u ParamId=%u",
			hCLL,
			pParamItem->ComParamId
		);

		const long cllret =
			it->second->SetComParam(pParamItem);

		DPDU_TRACE(
			"AFTER",
			"ComLogicalLink::SetComParam ret=%ld",
			cllret
		);

		if (cllret != STATUS_NOERROR)
		{
			DPDU_TRACE(
				"RESULT",
				"SetComParam failed ParamId=%u ret=0x%08X",
				pParamItem->ComParamId,
				static_cast<unsigned int>(cllret)
			);

			DPDU_RETURN(
				static_cast<T_PDU_ERROR>(cllret)
			);
		}
	}

	DPDU_RETURN(PDU_STATUS_NOERROR);
}

static UNUM32 m_copCtr = 1;

T_PDU_ERROR __stdcall PDUStartComPrimitive(
	UNUM32 hMod,
	UNUM32 hCLL,
	UNUM32 CoPType,
	UNUM32 CoPDataSize,
	UNUM8* pCoPData,
	PDU_COP_CTRL_DATA* pCopCtrlData,
	void* pCoPTag,
	UNUM32* phCoP)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;

	// ------------------------------------------------------------
	// ENTER
	// ------------------------------------------------------------

	DPDU_TRACE(
		"ENTER",
		"hMod=%u hCLL=%u CoPType=0x%08X CoPDataSize=%u "
		"pCoPData=%p pCopCtrlData=%p pCoPTag=%p phCoP=%p",
		hMod,
		hCLL,
		CoPType,
		CoPDataSize,
		pCoPData,
		pCopCtrlData,
		pCoPTag,
		phCoP
	);
	if (phCoP == nullptr || (CoPDataSize > 0 && pCoPData == nullptr))
	{
		DPDU_TRACE("ERROR", "invalid output or data buffer phCoP=%p CoPDataSize=%u pCoPData=%p", phCoP, CoPDataSize, pCoPData);
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	}

	// ------------------------------------------------------------
	// Identify CoP type
	// ------------------------------------------------------------

	const char* copName = "UNKNOWN";

	switch (CoPType)
	{
	case PDU_COPT_STARTCOMM:
		copName = "STARTCOMM";
		break;

	case PDU_COPT_STOPCOMM:
		copName = "STOPCOMM";
		break;

	case PDU_COPT_UPDATEPARAM:
		copName = "UPDATEPARAM";
		break;

	case PDU_COPT_SENDRECV:
		copName = "SENDRECV";
		break;

	case PDU_COPT_DELAY:
		copName = "DELAY";
		break;

	case PDU_COPT_RESTORE_PARAM:
		copName = "RESTORE_PARAM";
		break;

	default:
		break;
	}

	DPDU_TRACE(
		"COP",
		"Type=0x%08X (%s)",
		CoPType,
		copName
	);

	// ------------------------------------------------------------
	// ComPrimitive data
	// ------------------------------------------------------------

	DPDU_TRACE(
		"DATA_INFO",
		"CoPDataSize=%u pCoPData=%p",
		CoPDataSize,
		pCoPData
	);

	if (CoPDataSize > 0 && pCoPData == nullptr)
	{
		DPDU_TRACE(
			"DATA_ERROR",
			"CoPDataSize=%u but pCoPData=NULL",
			CoPDataSize
		);
	}

	// ------------------------------------------------------------
	// Control data
	// ------------------------------------------------------------

	if (pCopCtrlData != nullptr)
	{
		DPDU_TRACE(
			"CONTROL",
			"Time=%u NumSendCycles=%d NumReceiveCycles=%d "
			"TempParamUpdate=%u NumPossibleExpectedResponses=%u "
			"TxFlag.NumFlagBytes=%u",
			pCopCtrlData->Time,
			pCopCtrlData->NumSendCycles,
			pCopCtrlData->NumReceiveCycles,
			pCopCtrlData->TempParamUpdate,
			pCopCtrlData->NumPossibleExpectedResponses,
			pCopCtrlData->TxFlag.NumFlagBytes
		);

	}
	else
	{
		DPDU_TRACE(
			"CONTROL",
			"pCopCtrlData=NULL"
		);
	}

	// ------------------------------------------------------------
	// ComLogicalLink lookup
	// ------------------------------------------------------------

	auto it = m_commChannels.find(hCLL);

	if (it == m_commChannels.end())
	{
		DPDU_TRACE(
			"CLL_LOOKUP",
			"hCLL=%u NOT FOUND in m_commChannels",
			hCLL
		);

		ret = PDU_ERR_CLL_NOT_CONNECTED;

		if (phCoP != nullptr)
		{
			*phCoP = PDU_HANDLE_UNDEF;
		}

		DPDU_TRACE(
			"OUTPUT",
			"ret=0x%08X hCoP=%u",
			ret,
			phCoP != nullptr ? *phCoP : PDU_HANDLE_UNDEF
		);

		DPDU_RETURN(ret);
	}

	// ------------------------------------------------------------
	// CLL found
	// ------------------------------------------------------------

	DPDU_TRACE(
		"CLL_LOOKUP",
		"hCLL=%u FOUND",
		hCLL
	);

	// ------------------------------------------------------------
	// IMPORTANT: protect phCoP
	// ------------------------------------------------------------

	if (phCoP == nullptr)
	{
		DPDU_TRACE(
			"ERROR",
			"phCoP=NULL"
		);

		ret = PDU_ERR_INVALID_PARAMETERS;

		DPDU_RETURN(ret);
	}

	// ------------------------------------------------------------
	// ComPrimitive dispatch
	// ------------------------------------------------------------
	if (CoPType == PDU_COPT_UPDATEPARAM ||
		CoPType == PDU_COPT_RESTORE_PARAM ||
		CoPType == PDU_COPT_DELAY)
	{
		*phCoP = it->second->StartComPrimitive(
			CoPType,
			CoPDataSize,
			pCoPData,
			pCopCtrlData,
			pCoPTag
		);
		DPDU_TRACE("RESULT", "queued CoP Type=0x%08X hCoP=%u", CoPType, *phCoP);
		DPDU_RETURN(PDU_STATUS_NOERROR);
	}

	PDU_EVENT_ITEM* pEvt = nullptr;

	switch (CoPType)
	{
		// ============================================================
		// UPDATEPARAM
		// ============================================================

	case PDU_COPT_UPDATEPARAM:
	{
		DPDU_TRACE(
			"DISPATCH",
			"UPDATEPARAM -> ComLogicalLink::UpdateComParams()"
		);

		const long cllret =
			it->second->UpdateComParams();

		DPDU_TRACE(
			"RESULT",
			"UpdateComParams ret=%ld (0x%08lX)",
			cllret,
			static_cast<unsigned long>(cllret)
		);

		*phCoP = PDU_ID_UNDEF - 1;

		if (cllret != STATUS_NOERROR)
		{
			ret = static_cast<T_PDU_ERROR>(cllret);
			break;
		}

		// --------------------------------------------------------
		// EXECUTING
		// --------------------------------------------------------

		pEvt = new PDU_EVENT_ITEM;

		pEvt->hCop = *phCoP;
		pEvt->ItemType = PDU_IT_STATUS;
		pEvt->pCoPTag = pCoPTag;
		pEvt->pData = new PDU_STATUS_DATA;

		*(PDU_STATUS_DATA*)(pEvt->pData) =
			PDU_COPST_EXECUTING;

		DPDU_TRACE(
			"EVENT",
			"UPDATEPARAM -> PDU_COPST_EXECUTING hCoP=%u",
			*phCoP
		);

		it->second->SignalEvent(pEvt);

		// --------------------------------------------------------
		// FINISHED
		// --------------------------------------------------------

		pEvt = new PDU_EVENT_ITEM;

		pEvt->hCop = *phCoP;
		pEvt->ItemType = PDU_IT_STATUS;
		pEvt->pCoPTag = pCoPTag;
		pEvt->pData = new PDU_STATUS_DATA;

		*(PDU_STATUS_DATA*)(pEvt->pData) =
			PDU_COPST_FINISHED;

		DPDU_TRACE(
			"EVENT",
			"UPDATEPARAM -> PDU_COPST_FINISHED hCoP=%u",
			*phCoP
		);

		it->second->SignalEvent(pEvt);

		break;
	}

	// ============================================================
	// RESTORE_PARAM
	// ============================================================

	case PDU_COPT_RESTORE_PARAM:
	{
		DPDU_TRACE(
			"DISPATCH",
			"RESTORE_PARAM -> ComLogicalLink::RestoreComParams()"
		);

		const long cllret =
			it->second->RestoreComParams();

		DPDU_TRACE(
			"RESULT",
			"RestoreComParams ret=%ld (0x%08lX)",
			cllret,
			static_cast<unsigned long>(cllret)
		);

		*phCoP = PDU_ID_UNDEF - 1;

		if (cllret != STATUS_NOERROR)
		{
			ret = static_cast<T_PDU_ERROR>(cllret);
			break;
		}

		// --------------------------------------------------------
		// EXECUTING
		// --------------------------------------------------------

		pEvt = new PDU_EVENT_ITEM;

		pEvt->hCop = *phCoP;
		pEvt->ItemType = PDU_IT_STATUS;
		pEvt->pCoPTag = pCoPTag;
		pEvt->pData = new PDU_STATUS_DATA;

		*(PDU_STATUS_DATA*)(pEvt->pData) =
			PDU_COPST_EXECUTING;

		DPDU_TRACE(
			"EVENT",
			"RESTORE_PARAM -> PDU_COPST_EXECUTING hCoP=%u",
			*phCoP
		);

		it->second->SignalEvent(pEvt);

		// --------------------------------------------------------
		// FINISHED
		// --------------------------------------------------------

		pEvt = new PDU_EVENT_ITEM;

		pEvt->hCop = *phCoP;
		pEvt->ItemType = PDU_IT_STATUS;
		pEvt->pCoPTag = pCoPTag;
		pEvt->pData = new PDU_STATUS_DATA;

		*(PDU_STATUS_DATA*)(pEvt->pData) =
			PDU_COPST_FINISHED;

		DPDU_TRACE(
			"EVENT",
			"RESTORE_PARAM -> PDU_COPST_FINISHED hCoP=%u",
			*phCoP
		);

		it->second->SignalEvent(pEvt);

		break;
	}

	// ============================================================
	// STARTCOMM
	// ============================================================

	case PDU_COPT_STARTCOMM:
	{
		DPDU_TRACE("DISPATCH", "STARTCOMM -> ComLogicalLink::StartComPrimitive hCLL=%u", hCLL);
		*phCoP = it->second->StartComPrimitive(
			CoPType,
			CoPDataSize,
			pCoPData,
			pCopCtrlData,
			pCoPTag
		);
		DPDU_TRACE("RESULT", "STARTCOMM primitive created hCoP=%u", *phCoP);

		break;
	}

	// ============================================================
	// STOPCOMM
	// ============================================================

	case PDU_COPT_STOPCOMM:
	{
		DPDU_TRACE("DISPATCH", "STOPCOMM -> ComLogicalLink::StartComPrimitive hCLL=%u", hCLL);
		*phCoP = it->second->StartComPrimitive(
			CoPType,
			CoPDataSize,
			pCoPData,
			pCopCtrlData,
			pCoPTag
		);
		DPDU_TRACE("RESULT", "STOPCOMM primitive created hCoP=%u", *phCoP);

		break;
	}

	// ============================================================
	// SENDRECV
	// ============================================================

	case PDU_COPT_SENDRECV:
	{
		DPDU_TRACE("DISPATCH", "SENDRECV -> ComLogicalLink::StartComPrimitive hCLL=%u", hCLL);
		*phCoP = it->second->StartComPrimitive(
			CoPType,
			CoPDataSize,
			pCoPData,
			pCopCtrlData,
			pCoPTag
		);
		DPDU_TRACE("RESULT", "SENDRECV primitive created hCoP=%u", *phCoP);

		break;
	}

	// ============================================================
	// DELAY
	// ============================================================

	case PDU_COPT_DELAY:
	{
		DPDU_TRACE(
			"UNIMPLEMENTED",
			"DELAY reached! hCLL=%u -- NO HARDWARE ACTION YET",
			hCLL
		);

		*phCoP = PDU_ID_UNDEF - 1;

		break;
	}

	// ============================================================
	// UNKNOWN
	// ============================================================

	default:
	{
		DPDU_TRACE(
			"UNKNOWN_COP",
			"Unknown CoPType=0x%08X hCLL=%u",
			CoPType,
			hCLL
		);

		*phCoP = PDU_HANDLE_UNDEF;

		ret = PDU_ERR_INVALID_PARAMETERS;

		break;
	}
	}

	// ------------------------------------------------------------
	// OUTPUT
	// ------------------------------------------------------------

	DPDU_TRACE(
		"OUTPUT",
		"CoPType=0x%08X (%s) hCLL=%u ret=0x%08X hCoP=%u",
		CoPType,
		copName,
		hCLL,
		ret,
		*phCoP
	);

	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUCancelComPrimitive(UNUM32 hMod, UNUM32 hCLL, UNUM32 hCoP)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;

	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u hCoP=%u", hMod, hCLL, hCoP);

	auto it = m_commChannels.find(hCLL);
	if (it != m_commChannels.end())
	{
		ret = it->second->Cancel(hCoP);
	}

	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUGetEventItem(UNUM32 hMod, UNUM32 hCLL, PDU_EVENT_ITEM** pEventItem)
{
	T_PDU_ERROR ret = PDU_STATUS_NOERROR;
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u pEventItem=%p", hMod, hCLL, pEventItem);
	if (pEventItem == nullptr)
	{
		DPDU_TRACE("ERROR", "pEventItem=NULL");
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	}

	auto it = m_commChannels.find(hCLL);
	if (it != m_commChannels.end())
	{
		bool cllret = it->second->GetEvent(*pEventItem);

		if (!cllret)
		{
			*pEventItem = nullptr;
			ret = PDU_ERR_EVENT_QUEUE_EMPTY;
		}
	}
	else
	{
		*pEventItem = nullptr;
		ret = PDU_ERR_EVENT_QUEUE_EMPTY;
	}

	DPDU_TRACE("OUTPUT", "pEventItem=%p event=%p ret=0x%08X", pEventItem, pEventItem != nullptr ? *pEventItem : nullptr, static_cast<unsigned int>(ret));
	DPDU_RETURN(ret);
}

T_PDU_ERROR __stdcall PDUDestroyItem(PDU_ITEM* pItem)
{
	DPDU_TRACE("ENTER", "pItem=%p ItemType=0x%x", pItem, pItem != nullptr ? pItem->ItemType : 0);
	if (pItem == nullptr)
	{
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	}
	switch (pItem->ItemType)
	{
		case PDU_IT_IO_UNUM32:
		{
			PDU_DATA_ITEM* pIt = (PDU_DATA_ITEM*)pItem;
			delete pIt->pData;
			pIt->pData = nullptr;
			delete pIt;
			pIt = nullptr;

			DPDU_TRACE("ACTION", "destroyed PDU_IT_IO_UNUM32");
			break;
		}
		case PDU_IT_STATUS:
		{
			PDU_EVENT_ITEM* pIt = (PDU_EVENT_ITEM*)pItem;
			delete pIt->pData;
			pIt->pData = nullptr;
			delete pIt;
			pIt = nullptr;

			DPDU_TRACE("ACTION", "destroyed PDU_IT_STATUS");
			break;
		}
		case PDU_IT_ERROR:
		case PDU_IT_INFO:
		{
			PDU_EVENT_ITEM* pIt = reinterpret_cast<PDU_EVENT_ITEM*>(pItem);
			const T_PDU_IT itemType = pIt->ItemType;
			delete pIt->pData;
			pIt->pData = nullptr;
			delete pIt;
			DPDU_TRACE("ACTION", "destroyed event ItemType=0x%x", itemType);
			break;
		}
		case PDU_IT_RESULT:
		{
			PDU_EVENT_ITEM* pIt = (PDU_EVENT_ITEM*)pItem;
			PDU_RESULT_DATA* pData = (PDU_RESULT_DATA*)pIt->pData;
			delete[] pData->pDataBytes;
			pData->pDataBytes = nullptr;
			delete pIt->pData;
			pIt->pData = nullptr;
			delete pIt;
			pIt = nullptr;

			DPDU_TRACE("ACTION", "destroyed PDU_IT_RESULT");
			break;
		}
		default:
		{
			DPDU_TRACE("RESULT", "unhandled ItemType=0x%x", pItem->ItemType);
		}
	}

	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDURegisterEventCallback(
	UNUM32 hMod,
	UNUM32 hCLL,
	CALLBACKFNC EventCallbackFunction)
{
	DPDU_TRACE(
		"ENTER",
		"hMod=%u hCLL=%u callback=%p",
		hMod,
		hCLL,
		EventCallbackFunction
	);

	auto it = m_commChannels.find(hCLL);

	if (it == m_commChannels.end())
	{
		DPDU_TRACE(
			"ERROR",
			"hCLL=%u NOT FOUND in m_commChannels",
			hCLL
		);

		DPDU_RETURN(PDU_ERR_CLL_NOT_CONNECTED);
	}

	//
	// Ask the CLL for its current state BEFORE callback registration.
	//

	T_PDU_STATUS status = PDU_MODST_NOT_AVAIL;

	const T_PDU_ERROR statusRet =
		it->second->GetStatus(status);

	DPDU_TRACE(
		"STATE_BEFORE",
		"hCLL=%u GetStatus ret=0x%08X status=0x%04X",
		hCLL,
		static_cast<unsigned int>(statusRet),
		static_cast<unsigned int>(status)
	);

	//
	// Register callback
	//

	DPDU_TRACE(
		"BEFORE",
		"ComLogicalLink::RegisterEventCallback hCLL=%u oldCallback=%p newCallback=%p",
		hCLL,
		nullptr,
		EventCallbackFunction
	);

	it->second->RegisterEventCallback(EventCallbackFunction);

	DPDU_TRACE(
		"AFTER",
		"ComLogicalLink::RegisterEventCallback hCLL=%u callback=%p",
		hCLL,
		EventCallbackFunction
	);

	//
	// Ask again after registration.
	//

	status = PDU_MODST_NOT_AVAIL;

	const T_PDU_ERROR statusRetAfter =
		it->second->GetStatus(status);

	DPDU_TRACE(
		"STATE_AFTER",
		"hCLL=%u GetStatus ret=0x%08X status=0x%04X",
		hCLL,
		static_cast<unsigned int>(statusRetAfter),
		static_cast<unsigned int>(status)
	);

	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUGetObjectId(T_PDU_OBJT pduObjectType, CHAR8* pShortname, UNUM32* pPduObjectId)
{
	DPDU_TRACE("ENTER", "pduObjectType=%d shortname=%s pPduObjectId=%p", static_cast<int>(pduObjectType), pShortname != nullptr ? pShortname : "<null>", pPduObjectId);
	if (pShortname == nullptr || pPduObjectId == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	for (const auto& object : m_objectIdMap)
	{
		if (object.second == pShortname)
		{
			*pPduObjectId = object.first;
			DPDU_RETURN(PDU_STATUS_NOERROR);
		}
	}
	UNUM32 id = m_objectIdMap.size();
	m_objectIdMap.insert({ id , std::string(pShortname) });

	*pPduObjectId = id;

	DPDU_TRACE("OUTPUT", "objectId=%u", *pPduObjectId);
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

//storage for PDUGetModuleIds
static std::vector<std::string> m_pduModuleNames;
static std::vector<PDU_MODULE_DATA> m_pduModules;
static CHAR8 m_additionalInfo[32] = "ConnectionType = 'unknown'";

static PDU_MODULE_ITEM m_moduleItem = {
	PDU_IT_MODULE_ID,
	0,
	nullptr,
};

T_PDU_ERROR __stdcall PDUGetModuleIds(PDU_MODULE_ITEM** pModuleIdList)
{
	DPDU_TRACE("ENTER", "pModuleIdList=%p", pModuleIdList);
	if (pModuleIdList == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	m_registryList.clear();
	m_pduModules.clear();
	m_pduModuleNames.clear();

	m_moduleItem.NumEntries = 0;
	m_moduleItem.pModuleData = nullptr;

	std::set<cPassThruInfo> reg;
	DPDU_TRACE("BEFORE", "shim_enumPassThruInterfaces");
	shim_enumPassThruInterfaces(reg);
	DPDU_TRACE("AFTER", "shim_enumPassThruInterfaces interfaces=%zu", reg.size());

	std::copy(
		reg.begin(),
		reg.end(),
		std::back_inserter(m_registryList)
	);

	/*
	 * IMPORTANT:
	 *
	 * PDU_MODULE_DATA::pName points directly into
	 * m_pduModuleNames.
	 *
	 * Reserve BEFORE inserting strings so that push_back()
	 * cannot invalidate the c_str() pointers.
	 */
	m_pduModuleNames.reserve(
		m_registryList.size()
	);

	/*
	 * Same idea for m_pduModules. The PDU_MODULE_ITEM returned
	 * below points to this vector's storage.
	 */
	m_pduModules.reserve(
		m_registryList.size()
	);

	UNUM32 idx = 0;

	for (const auto& iface : m_registryList)
	{
		m_pduModuleNames.push_back(
			iface.Name
		);

		DPDU_TRACE("MODULE", "index=%u name=%s library=%s", idx, m_pduModuleNames.back().c_str(), iface.FunctionLibrary.c_str());

		PDU_MODULE_DATA d = {
			1,
			idx,
			(CHAR8*)m_pduModuleNames.back().c_str(),
			m_additionalInfo,
			PDU_MODST_AVAIL
		};

		m_pduModules.push_back(d);

		++idx;
	}

	m_moduleItem.pModuleData =
		m_pduModules.empty()
		? nullptr
		: m_pduModules.data();

	m_moduleItem.NumEntries = idx;

	*pModuleIdList = &m_moduleItem;

	DPDU_TRACE("OUTPUT", "moduleCount=%u pModuleIdList=%p item=%p", idx, pModuleIdList, *pModuleIdList);
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUGetResourceIds(UNUM32 hMod, PDU_RSC_DATA* pResourceIdData, PDU_RSC_ID_ITEM** pResourceIdList)
{
	DPDU_TRACE("ENTER", "hMod=%u pResourceIdData=%p pResourceIdList=%p", hMod, pResourceIdData, pResourceIdList);
	if (pResourceIdList == nullptr || pResourceIdData == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	static UNUM32 resourceId = 1;
	static PDU_RSC_ID_ITEM_DATA moduleResources{};
	static PDU_RSC_ID_ITEM resourceList{};
	moduleResources.hMod = hMod;
	moduleResources.NumIds = 1;
	moduleResources.pResourceIdArray = &resourceId;
	resourceList.ItemType = PDU_IT_RSC_ID;
	resourceList.NumModules = 1;
	resourceList.pResourceIdDataArray = &moduleResources;
	*pResourceIdList = &resourceList;
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUGetConflictingResources(UNUM32 resourceId, PDU_MODULE_ITEM* pInputModuleList, PDU_RSC_CONFLICT_ITEM** pOutputConflictList)
{
	DPDU_TRACE("ENTER", "resourceId=%u pInputModuleList=%p pOutputConflictList=%p", resourceId, pInputModuleList, pOutputConflictList);
	if (pOutputConflictList == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	static PDU_RSC_CONFLICT_ITEM conflicts{};
	conflicts.ItemType = PDU_IT_RSC_CONFLICT;
	conflicts.NumEntries = 0;
	conflicts.pRscConflictData = nullptr;
	*pOutputConflictList = &conflicts;
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUGetUniqueRespIdTable(UNUM32 hMod, UNUM32 hCLL, PDU_UNIQUE_RESP_ID_TABLE_ITEM** pUniqueRespIdTable)
{
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u pUniqueRespIdTable=%p", hMod, hCLL, pUniqueRespIdTable);
	if (pUniqueRespIdTable == nullptr)
		DPDU_RETURN(PDU_ERR_INVALID_PARAMETERS);
	static PDU_UNIQUE_RESP_ID_TABLE_ITEM table{};
	table.ItemType = PDU_IT_UNIQUE_RESP_ID_TABLE;
	table.NumEntries = 0;
	table.pUniqueData = nullptr;
	*pUniqueRespIdTable = &table;
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

T_PDU_ERROR __stdcall PDUSetUniqueRespIdTable(UNUM32 hMod, UNUM32 hCLL, PDU_UNIQUE_RESP_ID_TABLE_ITEM* pUniqueRespIdTable)
{
	DPDU_TRACE("ENTER", "hMod=%u hCLL=%u pUniqueRespIdTable=%p", hMod, hCLL, pUniqueRespIdTable);
	DPDU_RETURN(PDU_STATUS_NOERROR);
}

static Protocol mapDpuProtocolToCLL(UNUM32 protocolId)
{
	auto it = m_objectIdMap.find(protocolId);

	if (it == m_objectIdMap.end())
	{
		DPDU_TRACE(
			"PROTOCOL",
			"ProtocolId=%u not found in object map",
			protocolId
		);

		return CLL_UNKNOWN;
	}

	const std::string& name = it->second;

	if (name == "ISO_14230_3_on_ISO_14230_2" || name == "ISO_14230_1_UART")
		return CLL_ISO14230;

	if (name == "KW82_on_KW_UART")
		return CLL_KW82;

	if (name == "ISO_9141_2_UART" || name == "SAE_J2190_on_ISO_9141_2")
		return CLL_ISO9141;

	if (name == "ISO_15765_3_on_ISO_15765_2" || name == "ISO_15031_5_on_ISO_15765_4")
		return CLL_ISO15765;

	if (name == "ISO_11898_RAW")
		return CLL_CAN;

	if (name == "SAE_J2411_SWCAN")
		return CLL_SWCAN;

	if (name == "SAE_J1850_VPW" || name == "SAE_J2190_on_SAE_J1850_VPW")
		return CLL_J1850_VPW;

	if (name == "SAE_J1850_PWM")
		return CLL_J1850_PWM;

	if (name == "SAE_J1939")
		return CLL_J1939;

	if (name == "KW_UART")
		return CLL_UART;

	DPDU_TRACE(
		"PROTOCOL",
		"Unknown protocol name=\"%s\" ProtocolId=%u",
		name.c_str(),
		protocolId
	);

	return CLL_UNKNOWN;
}

#undef DPDU_RETURN
#undef DPDU_TRACE
