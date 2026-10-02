# Nyanko J2534 — Current Development State

Last updated 2026-09-30

## 1. Current phase

We are currently working on

D-PDU  ComLogicalLink  ComPrimitive integration inside the Nyanko J2534 stack.

We are NOT currently implementing

 Arduino communication
 USB communication
 serial communication
 socket communication
 CAN hardware
 K-Line hardware
 firmware
 physical VCI timing

The VCI_ layer may remain stubbed.

---

# 2. Current source already contains

The current source already contains the following work.

## Protocol enum expansion

`ComLogicalLink` has been expanded beyond the original protocols.

Current conceptual protocol categories include

 CLL_ISO14230
 CLL_KW82
 CLL_CAN
 CLL_ISO15765
 CLL_SWCAN
 CLL_J1850_VPW
 CLL_J1850_PWM
 CLL_J1939
 CLL_UART
 CLL_UNKNOWN

Always verify the exact current enum in the repository.

---

## D-PDU resource information

`ComLogicalLink` has fields for

 D-PDU protocol ID
 bus type ID
 resource ID
 DLC pins
 create flags

This was deliberately added so that D-PDU resource information is not lost.

---

## GenericComPrimitive

`GenericComPrimitive` already exists.

It inherits from `ComPrimitive`.

It implements

 StartComm
 StopComm
 SendRecv

It currently accepts operations without performing real hardware communication.

This is intentional.

---

## Protocol mapping

A `mapDpuProtocolToCLL()` function exists and supports multiple D-PDU protocol categories, including the newer protocols.

Do not recreate it.

Do not ask the developer to add it again.

Inspect and use the existing implementation.

---

# 3. Important current problem area

The next task is to verify the actual execution path for primitive creation.

There has historically been an older switchdispatcher in `PDUStartComPrimitive()` that directly handles

 PDU_COPT_STARTCOMM
 PDU_COPT_STOPCOMM
 PDU_COPT_SENDRECV

This may bypass the newer

```
ComLogicalLinkStartComPrimitive()
    ↓
GenericComPrimitive  specialized primitive
```

Before making any change, inspect the current implementation and determine whether this bypass still exists.

Do not assume it exists solely because it existed in an older dump.

Do not assume GenericComPrimitive is being executed solely because the class exists.

Instrument the call path if necessary.

---

# 4. Desired primitive flow

The desired architecture is approximately

```
PDUStartComPrimitive
        
        v
ComLogicalLinkStartComPrimitive
        
        +---- ISO14230 → ISO14230ComPrimitive
        
        +---- KW82     → KW82ComPrimitive
        
        +---- other    → GenericComPrimitive
        
        v
ProcessCop
        
        +---- StartComm
        +---- StopComm
        +---- SendRecv
        
        v
primitive Finish  events  status
```

The exact current code should be inspected before modifying it.

---

# 5. Current generic behavior

Generic primitives currently do not communicate with real hardware.

Their job is currently to let the software state machine proceed.

They should therefore not

 generate arbitrary CAN frames;
 generate arbitrary K-Line traffic;
 access Arduino;
 access sockets;
 invent diagnostic responses.

Those belong to later phases.

---

# 6. Current physical VCI state

The VCI abstraction is intentionally stubbed.

The future design is

```
ComLogicalLink  J2534
    ↓
VCI_
    ↓
physical VCI
```

The current phase ends before physical communication.

---

# 7. Current known bug fixes

## Channel ID 0

`channel_groupremoveChannel()` was made defensive against invalid channel IDs.

In particular, ChannelID 0 must not cause undefined behavior when D-PDU cleanup calls

```
PassThruDisconnect(0)
```

---

## Null event callback

`ComLogicalLinkSignalEvents()` has a null callback guard.

If there is no callback registered, the event remains queued instead of dereferencing a null callback.

Do not remove this guard.

---

# 8. Current known behavior from Tech2Win

Tech2Win can

 create D-PDU logical links;
 select different protocolsresources;
 register callbacks;
 perform initializationdiscovery;
 reach STARTCOMM for some protocols;
 disconnect during probing.

An earlier observed STARTCOMM was

```
CoPType = 0x8001
CoPDataSize = 5
Data = 81 28 F1 81 1B
```

This was accepted without hardware action during an earlier test.

---

# 9. Historical protocol observations

Previously observed

```
D-PDU ProtocolId 69
    → ISO15765
    → J2534 ISO15765 (6)

D-PDU ProtocolId 74
    → CAN
    → J2534 CAN (5)
```

These are observations from previous traces, not a substitute for checking the current sourceAPI definitions.

---

# 10. SWCAN

SWCAN has been observed during Tech2Win protocolresource testing.

Do not guess its D-PDU object name.

Use the current sourceresource map or current trace.

SWCAN must remain distinguishable from ISO14230.

Physical SWCAN implementation is later.

---

# 11. Do not repeat already completed work

The following should NOT be proposed as new work without first checking whether the current implementation has regressed

 adding CLL_SWCAN;
 adding CLL_J1850_VPW;
 adding CLL_J1850_PWM;
 adding CLL_J1939;
 adding CLL_UART;
 adding CLL_UNKNOWN;
 adding D-PDU protocol ID storage;
 adding bus type storage;
 adding resource ID storage;
 adding DLC pin storage;
 adding create flag storage;
 creating GenericComPrimitive;
 adding mapDpuProtocolToCLL.

These changes have already been made in the current development state.

---

# 12. Do not trust old dumps over the repository

Previous development used remote source dumps such as

 dumpSource.txt
 dumpSourceUpdate.txt
 dumpDPDU.txt
 dumpTrace.txt
 dumpT2W.txt

These are historical diagnostic artifacts.

If the local repository is available, inspect the repository first.

A newer source dump may exist, but it is still secondary to the actual working tree.

---

# 13. Current immediate goal

Before implementing real VCI communication

1. Verify `PDUCreateComLogicalLink()` uses the current protocol mapping correctly.
2. Verify the full primitive creation path.
3. Verify GenericComPrimitive is actually instantiated for generic protocols.
4. Verify STARTCOMM reaches the correct primitive.
5. Verify STOPCOMM reaches the correct primitive.
6. Verify SENDRECV reaches the correct primitive.
7. Verify PDUCLL state transitions.
8. Verify eventscallback behavior.
9. Test Tech2Win again.
10. Only after the state machine is stable, move toward VCI_ implementation.

---

# 14. Important distinction

There are three different meanings of works

### Software state-machine works

Tech2Win can create links and primitives and receive appropriate statusevents.

### J2534 works

The PassThru API correctly represents the requested operations.

### Hardware works

The VCI actually communicates with the vehicle.

The current objective is the first two.

Do not jump to the third prematurely.

---

# 15. Working philosophy

When debugging

Do not immediately add fake responses.

First determine exactly what Tech2Win is waiting for.

Use logging to establish

```
Tech2Win request
    ↓
D-PDU function
    ↓
ComLogicalLink
    ↓
ComPrimitive
    ↓
J2534
    ↓
VCI
```

Only implement the minimum required behavior at the layer responsible for the problem.

---

# 16. Current repository is authoritative

This document is a snapshot of the development state.

If code has changed after this document was written

 inspect the code;
 determine what changed;
 update this document.

Never blindly apply instructions from this document if the source already contains the requested change.
