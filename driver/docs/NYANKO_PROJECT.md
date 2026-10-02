# Nyanko J2534 — Project Knowledge Base

## 1. Purpose

Nyanko J2534 is a custom J2534 PassThru implementation developed as part of a diagnostic VCI project for a Chevrolet Nubira/Lacetti.

The target vehicle is a 2007 Chevrolet Nubira 2.0 TCDI with Z20S-D engine.

The broader goal is to build a custom VCI capable of eventually communicating with the vehicle's diagnostic networks.

The immediate goal is NOT yet physical vehicle communication.

The immediate goal is to make the software stack behave correctly and establish a clean abstraction boundary between D-PDU, J2534 and the future VCI hardware.

---

# 2. Overall architecture

The intended stack is:

```
Tech2Win
    |
    v
D-PDU
    |
    v
dpdu-passthru
    |
    v
Nyanko J2534 PassThru DLL
    |
    v
VCI_* abstraction
    |
    v
future physical VCI / firmware
    |
    v
vehicle diagnostic buses
```

The existing `dpdu-passthru` implementation was developed separately and is considered functional enough for the current phase.

The current development target is Nyanko's own PassThru DLL.

---

# 3. Development phases

## Phase 1 — J2534 API skeleton

Goal:

Make the PassThru DLL expose the J2534 API correctly.

All J2534 functions should exist and route into internal abstractions.

The DLL should be loadable by dpdu-passthru.

VCI functions may be stubbed.

Status:

Mostly established.

---

## Phase 2 — D-PDU / ComLogicalLink abstraction

Goal:

Allow dpdu-passthru to represent D-PDU logical links without incorrectly assuming every protocol is ISO14230.

This phase includes:

* protocol identification;
* D-PDU protocol IDs;
* bus types;
* resource IDs;
* DLC pin information;
* CLL create flags;
* generic primitives;
* correct STARTCOMM / STOPCOMM / SENDRECV routing.

Status:

Currently in progress.

Several important parts have already been implemented in the current source.

---

## Phase 3 — VCI abstraction

Goal:

Route protocol/data/configuration from the J2534 layer into `VCI_*`.

The VCI layer should eventually expose operations such as:

* VCI_Open
* VCI_Close
* VCI_Connect
* VCI_Disconnect
* VCI_Send
* VCI_Receive
* VCI_StartPeriodicMsg
* VCI_StopPeriodicMsg
* VCI_SetProgrammingVoltage
* VCI_Ioctl

At this phase the functions may remain stubbed.

Status:

Stubbed architecture exists.

---

## Phase 4 — Physical VCI

Future phase.

Potential hardware is based on an Arduino/MCU-based custom VCI.

Possible physical buses include:

* CAN
* K-Line / ISO9141 / ISO14230
* other vehicle protocols as required.

Do not implement this phase while working on Phase 2 unless explicitly requested.

---

# 4. Why the architecture is deliberately layered

The important design decision is that J2534 should not know how the physical VCI works.

The J2534 DLL should express:

* protocol;
* baud rate;
* flags;
* channel;
* data;
* configuration;
* periodic operations;
* filters;
* IOCTL operations.

The VCI layer will later translate those operations into whatever transport the hardware requires.

The hardware/firmware should ultimately decide how a requested protocol is physically implemented.

This avoids coupling Tech2Win/D-PDU behavior to the Arduino or MCU implementation.

---

# 5. J2534 DLL

The main PassThru implementation contains functions such as:

* PassThruOpen
* PassThruClose
* PassThruConnect
* PassThruDisconnect
* PassThruReadMsgs
* PassThruWriteMsgs
* PassThruStartPeriodicMsg
* PassThruStopPeriodicMsg
* PassThruStartMsgFilter
* PassThruStopMsgFilter
* PassThruSetProgrammingVoltage
* PassThruReadVersion
* PassThruGetLastError
* PassThruIoctl

The intended pattern is:

J2534 API
→ channel/protocol abstraction
→ VCI_* abstraction

---

# 6. Channel architecture

`channel_group` manages J2534 channels.

Creating a channel roughly follows:

PassThruConnect
→ channels.addChannel
→ create channel
→ set protocol
→ set flags
→ set baudrate
→ connectVCI
→ register channel

Disconnect removes the channel and releases the ID.

A previous bug existed where `PassThruDisconnect(0)` could result in invalid access/undefined behavior.

`channel_group::removeChannel()` was made defensive so invalid/nonexistent channel IDs return an error instead of dereferencing an invalid iterator.

This is important because D-PDU cleanup can call PassThruDisconnect with channel ID 0.

---

# 7. Protocol handlers

The project has protocol handler abstractions including:

* CAN
* ISO15765
* ISO9141

`channel::setProtocol()` selects the appropriate handler.

`channel::connectVCI()` calls:

```
VCI_Connect(channelID, protocolID, flags, baudrate)
```

`channel::sendPayload()` eventually calls VCI send functionality.

`channel::requestData()` eventually calls VCI receive functionality.

The current protocol handlers still contain remnants of the original Macchina-oriented behavior.

Do not blindly remove them.

They need to be cleaned/refactored only when their role in the current architecture is understood.

---

# 8. Globals

The project has a global battery voltage abstraction.

Current concept:

```
globals::BAT_VOLTAGE
```

with getter/setter functions.

Default value has been approximately:

```
12000 mV
```

The IOCTL handler can expose this as the battery voltage.

This is currently synthetic/stubbed.

---

# 9. IOCTL layer

`ioctl_handler` contains handlers for operations including:

* set config
* get config
* read battery voltage
* read programming voltage
* five baud init
* fast init
* clear TX buffer
* clear RX buffer
* clear periodic messages
* clear message filters
* clear MLT
* add/delete MLT entries

Most are currently stubs returning success.

This is intentional during the software bring-up phase.

Null parameters should still be validated.

---

# 10. ComLogicalLink

`ComLogicalLink` represents a D-PDU logical link.

The current architecture contains protocol identifiers such as:

* CLL_ISO14230
* CLL_KW82
* CLL_CAN
* CLL_ISO15765
* CLL_SWCAN
* CLL_J1850_VPW
* CLL_J1850_PWM
* CLL_J1939
* CLL_UART
* CLL_UNKNOWN

The exact current enum and implementation in the repository are authoritative.

Do not assume this document is newer than the code.

---

# 11. D-PDU resource information

A major architectural correction was made so that `ComLogicalLink` does not lose D-PDU resource information.

The logical link now stores information including:

* D-PDU protocol ID
* bus type ID
* resource ID
* DLC pin list
* CLL create flags

These fields are important because the same high-level protocol behavior can depend on the actual D-PDU resource configuration.

The J2534 layer should not blindly reduce all unknown D-PDU protocols to ISO14230.

---

# 12. D-PDU protocol mapping

The project has a `mapDpuProtocolToCLL()` concept/function.

It maps D-PDU protocol IDs to internal `ComLogicalLink` protocol categories.

Known mappings include concepts such as:

* ISO 14230
* KW82
* ISO 15765
* CAN
* SAE J2411 SWCAN
* SAE J1850 VPW
* SAE J1850 PWM
* SAE J1939
* KW UART

The exact numeric values and object names must always be read from the current source.

Do not invent protocol IDs or D-PDU object names.

---

# 13. GenericComPrimitive

A `GenericComPrimitive` was introduced.

Its purpose is to provide a generic D-PDU primitive implementation for protocols that do not yet have specialized implementations.

It inherits from:

```
ComPrimitive
```

and implements:

* StartComm
* StopComm
* SendRecv

It receives:

* CoPType
* CoPDataSize
* pCoPData
* pCopCtrlData
* pCoPTag
* J2534 protocol ID
* D-PDU protocol ID

The generic primitive currently accepts operations without performing real hardware communication.

This is intentional.

It is a staging mechanism so that the D-PDU/J2534 state machine can progress before physical VCI support exists.

---

# 14. Existing specialized primitives

Specialized primitives currently exist for at least:

* ISO14230
* KW82

They inherit from `ComPrimitive`.

Their behavior should not be replaced with GenericComPrimitive unless explicitly justified.

The generic primitive is for protocols without a specialized implementation.

---

# 15. ComPrimitive

`ComPrimitive` is the common base.

It stores:

* primitive handle
* primitive type
* CoP data
* control data
* CoP tag
* protocol ID
* state

It handles primitive lifecycle operations such as:

* Execute
* Finish
* Cancel
* Destroy
* GenerateStatusEvent

Typical states include:

* IDLE
* EXECUTING
* FINISHED
* CANCELLED

The current implementation is inherited from the existing dpdu-passthru architecture and should be changed cautiously.

---

# 16. ComLogicalLink lifecycle

Typical logical link lifecycle:

PDUCreateComLogicalLink
→ create ComLogicalLink
→ PDUConnect / Connect
→ StartComPrimitive
→ ProcessCop
→ StartComm / StopComm / SendRecv
→ primitive Finish
→ events/status
→ Disconnect

The exact current implementation must be inspected before modifying any stage.

---

# 17. Important current STARTCOMM issue

A significant issue discovered during development is that there are two layers capable of handling a primitive.

One path is:

```
PDUStartComPrimitive()
    |
    v
ComLogicalLink::StartComPrimitive()
    |
    v
GenericComPrimitive / ISO14230ComPrimitive / KW82ComPrimitive
```

However an older dispatcher in `PDUStartComPrimitive()` has historically contained direct handling/logging for:

* STARTCOMM
* STOPCOMM
* SENDRECV

At one point it logged:

```
STARTCOMM reached! ... NO HARDWARE ACTION YET
```

This direct handling can bypass the newer `ComLogicalLink` primitive machinery.

When debugging STARTCOMM, always verify the actual current call path in the repository.

Do not assume GenericComPrimitive is being invoked merely because it exists.

---

# 18. ComLogicalLink StartComm

`ComLogicalLink::StartComm()` has historically:

* cleared the J2534 RX buffer using CLEAR_RX_BUFFER;
* called the primitive StartComm;
* changed the logical link status to COMM_STARTED;
* queued/emitted a status event.

One important possible issue is that CLEAR_RX_BUFFER may be called with channel ID 0 for generic protocols that are not physically connected yet.

Its return value has historically not been the main focus.

If this causes incorrect behavior, instrument and fix it deliberately rather than silently ignoring the issue.

---

# 19. Tech2Win observations

Tech2Win is the real diagnostic client driving the D-PDU stack.

Important observed behavior:

* Tech2Win creates different D-PDU logical links for different protocols/resources.
* It can create CAN / ISO15765 resources.
* It can create other resources during discovery.
* Some resources may be tested before real communication is required.
* Tech2Win registers callbacks.
* Tech2Win may disconnect during probing.
* Some tests can reach STARTCOMM.

One observed STARTCOMM example had:

```
CoPType = 0x8001
CoPDataSize = 5
CoPData = 81 28 F1 81 1B
```

The software accepted this without real hardware action during an earlier test.

The exact meaning of every observed CoP must be derived from the D-PDU API definitions/current trace, not guessed.

---

# 20. Tech2Win crash history

There have been crashes during protocol discovery/testing.

One earlier crash was fixed by adding a null callback guard to:

```
ComLogicalLink::SignalEvents()
```

If no event callback is registered, the event remains queued rather than dereferencing a null callback.

This was an actual bug fix.

Do not remove the guard.

---

# 21. D-PDU protocol/resource discovery

Tech2Win has shown behavior involving:

* ISO15765
* CAN
* SWCAN
* other protocol/resource probing

Earlier traces showed:

D-PDU ProtocolId 69
→ CLL_ISO15765
→ J2534 ISO15765 (6)

D-PDU ProtocolId 74
→ CLL_CAN
→ J2534 CAN (5)

Those values are historical observations.

Verify against current source before using them as hard-coded assumptions.

---

# 22. SWCAN

SWCAN is important because Tech2Win has tested it during discovery.

The exact D-PDU object/resource name and protocol ID must be taken from the current source/trace.

Do NOT invent a SWCAN object name.

The current architecture is intended to preserve SWCAN as its own protocol/resource rather than silently treating it as ISO14230.

Physical SWCAN implementation is a later phase.

---

# 23. Future VCI philosophy

The desired future architecture is:

Tech2Win requests a protocol/configuration
→ D-PDU translates it
→ J2534 represents it
→ VCI receives protocol + configuration + data
→ firmware determines whether the physical VCI supports it.

This is preferable to hard-coding every possible protocol inside the J2534 layer.

For unsupported physical protocols, the future VCI/firmware may optionally provide a controlled synthetic response/acceptance behavior.

This should NOT be implemented until the software state machine is stable.

---

# 24. Physical hardware history

The broader project eventually targets a custom VCI.

Previous hardware exploration included:

* Arduino/MCU based communication;
* CAN transceiver options;
* K-Line interfaces;
* L9637D;
* SN65HVD-230;
* TCAN332GDR;
* custom EOBD hardware;
* J2534 PassThru DLL communicating with an Arduino-side protocol.

There has also been investigation into J2534 PassThru DLL ↔ Arduino communication and response buffering.

These are future layers and should not be mixed into the current D-PDU/ComLogicalLink phase.

---

# 25. Important historical J2534 development decisions

The project originally considered several existing projects as references.

The current architectural template is based on:

```
rnd-ash/m2-utd-passthru
```

The project retains the J2534 API/architecture but replaces the Macchina M2 hardware layer.

Other repositories were considered:

```
rnd-ash/Macchina-J2534
gorevyoneticisi/taskmanager-j2534
diamondman/J2534-PassThru-Logger
```

The last one is useful conceptually as a proxy/logging layer, but is not the base architecture.

---

# 26. Logging

There are multiple logs involved.

Nyanko's own activity log was moved away from protected Program Files locations.

A working path used during development is:

```
C:\Users\Public\Nyanko_activity.log
```

D-PDU logging is separate.

The project has used generated text dumps to inspect source and traces remotely, but now that Codex has repository access, the repository itself should be treated as the source of truth.

Do not rely on old dumps if the local working tree is available.

---

# 27. What has already been learned from experiments

The most important lesson is that Tech2Win's behavior is not simply:

```
connect → send diagnostic message
```

It performs protocol/resource discovery and state-machine operations first.

Therefore the driver must correctly handle:

* logical link creation;
* protocol identification;
* resource identity;
* callbacks;
* primitive creation;
* primitive lifecycle;
* status events;
* STARTCOMM;
* STOPCOMM;
* SENDRECV;
* disconnect/cleanup.

A driver that only implements raw CAN/K-Line communication is not sufficient.

---

# 28. Current strategic objective

The current strategic objective is:

Make the D-PDU → ComLogicalLink → ComPrimitive → J2534 state machine robust and protocol-agnostic enough that Tech2Win can progress through its initialization/discovery process without requiring real hardware yet.

Only after this works should physical VCI communication become the main development target.

---

# 29. Important rule for future agents

Do not "fix" something by replacing it with a simplified implementation merely because it makes the immediate test pass.

This project is intentionally being developed layer-by-layer.

Preserve the architecture.

When something fails, first identify which layer is responsible:

1. Tech2Win
2. D-PDU
3. dpdu-passthru
4. ComLogicalLink
5. ComPrimitive
6. J2534 PassThru
7. VCI abstraction
8. hardware/firmware

Then modify the lowest appropriate layer.

---

# 30. Source of truth

The actual repository source code is authoritative.

This document is project memory and architectural context.

If this document conflicts with current source code:

* inspect the source;
* report the discrepancy;
* update this document after confirming the intended state.

Never silently assume the document is newer.
