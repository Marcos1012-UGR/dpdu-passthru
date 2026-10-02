#pragma once

#include "pdu_api.h"
#include "ComPrimitive.h"

#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <map>
#include <set>
#include <vector>

enum Protocol
{
	CLL_ISO14230 = 0x01,
	CLL_KW82 = 0x02,
	CLL_ISO9141 = 0x03,
	CLL_CAN = 0x04,
	CLL_ISO15765 = 0x05,
	CLL_SWCAN = 0x06,
	CLL_J1850_VPW = 0x07,
	CLL_J1850_PWM = 0x08,
	CLL_J1939 = 0x09,
	CLL_UART = 0x0A,
	CLL_UNKNOWN = 0xFF
};

class ComLogicalLink
{
public:

	ComLogicalLink(
		UNUM32 hMod,
		UNUM32 hCLL,
		unsigned long deviceID,
		enum Protocol protocol,
		UNUM32 dPduProtocolId,
		UNUM32 busTypeId,
		UNUM32 resourceId,
		const std::vector<UNUM32>& dlcPins,
		const std::vector<UNUM8>& createFlags
	);

	long Connect();
	long Disconnect();

	T_PDU_ERROR GetStatus(T_PDU_STATUS& status);
	T_PDU_ERROR GetStatus(UNUM32 hCoP, T_PDU_STATUS& status);
	void GetLastError(T_PDU_ERR_EVT& error, UNUM32& hCoP, UNUM32& timestamp, UNUM32& extraInfo);
	T_PDU_ERROR Cancel(UNUM32 hCoP);

	UNUM32 StartComPrimitive(UNUM32 CoPType, UNUM32 CoPDataSize, UNUM8* pCoPData, PDU_COP_CTRL_DATA* pCopCtrlData, void* pCoPTag);
	long StartMsgFilter(unsigned long filterType);
	long ClearMsgFilters();

	void RegisterEventCallback(CALLBACKFNC cb);
	bool GetEvent(PDU_EVENT_ITEM* & pEvt);
	void SignalEvent(PDU_EVENT_ITEM* pEvt);

	long SetComParam(const PDU_PARAM_ITEM* pParamItem);
	long UpdateComParams();
	long RestoreComParams();

	long GetComParam(
		UNUM32 paramId,
		PDU_PARAM_ITEM** pParamItem
	);

private:
	void SignalEvents();
	void QueueEvent(PDU_EVENT_ITEM* pEvt);

	void run();

	void ProcessCop(std::shared_ptr<ComPrimitive> cop);
	long StartComm(std::shared_ptr<ComPrimitive> cop);
	long StopComm(std::shared_ptr<ComPrimitive> cop);
	long SendRecv(std::shared_ptr<ComPrimitive> cop);

	CALLBACKFNC m_eventCallbackFnc;

	std::atomic_bool m_running;
	std::jthread m_runLoop;

	std::mutex m_eventLock;
	std::queue<PDU_EVENT_ITEM*> m_eventQueue;

	std::mutex m_copLock;
	std::vector<std::shared_ptr<ComPrimitive>> m_copQueue;
	std::set<UNUM32> m_finishedCopHandles;

	UNUM32 m_hMod;
	UNUM32 m_hCLL;

	std::atomic<T_PDU_STATUS> m_status;
	std::mutex m_errorLock;
	T_PDU_ERR_EVT m_lastErrorCode;
	UNUM32 m_lastErrorCoP;
	UNUM32 m_lastErrorTimestamp;
	UNUM32 m_lastExtraErrorInfo;

	Protocol m_protocol;

	// D-PDU identifiers
	UNUM32 m_dPduProtocolId;
	UNUM32 m_busTypeId;
	UNUM32 m_resourceId;

	// J2534 protocol ID when one exists.
	// 0 means "no direct J2534 equivalent".
	unsigned long m_protocolID;
	unsigned long m_deviceID;
	unsigned long m_channelID;

	// Physical DLC pins requested by D-PDU.
	std::vector<UNUM32> m_dlcPins;

	// Flags supplied during PDUCreateComLogicalLink().
	std::vector<UNUM8> m_createFlags;
	std::vector<unsigned long> m_filterIDs;

	// D-PDU ComParams
	struct ComParamValue
	{
		UNUM32 id;
		T_PDU_PC paramClass;
		T_PDU_PT dataType;
		std::vector<UNUM8> data;
	};

	std::map<UNUM32, ComParamValue> m_workingParams;
	std::map<UNUM32, ComParamValue> m_activeParams;
};

