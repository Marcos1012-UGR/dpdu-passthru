# Nyanko J2534 — Codex Project Instructions

## 1. Project identity

Nyanko J2534 is a custom J2534 PassThru implementation being developed for a Chevrolet Nubira/Lacetti diagnostic VCI project.

This repository is part of a larger diagnostic stack:

Tech2Win
→ D-PDU
→ dpdu-passthru
→ Nyanko J2534 PassThru DLL
→ VCI_* abstraction
→ future hardware/firmware

Read `docs/NYANKO_PROJECT.md` for the full project history and architecture.

Read `docs/CURRENT_STATE.md` before making changes to understand the exact current development phase.

## 2. Critical development boundary

The current development focus is the Nyanko J2534 PassThru DLL and its interaction with dpdu-passthru.

Do NOT modify the raw VCI communication, Arduino communication, USB/serial/socket protocol, CAN implementation, K-Line implementation, or firmware unless the user explicitly asks for that phase.

The VCI_* layer is currently intentionally stubbed.

Do not prematurely implement real hardware communication.

## 3. Work from the actual repository

The repository is the source of truth for current code.

Never assume that the current source matches an older conversation, dump, memory, or previous version.

Before proposing or applying a change:

1. Inspect the actual current files.
2. Search for all references to the relevant function/class.
3. Understand the current call path.
4. Check whether the requested change has already been implemented.
5. Only then propose or make the change.

Do not ask the user to repeat changes that already exist in the repository.

## 4. D-PDU architecture

`dpdu-passthru` is an existing D-PDU implementation.

It is not currently the main development target.

Nyanko is the J2534 layer loaded by dpdu-passthru.

The intended architecture is:

Tech2Win
→ D-PDU
→ dpdu-passthru
→ Nyanko PassThru DLL
→ VCI_Open / VCI_Connect / VCI_Send / VCI_Receive / VCI_*
→ future physical VCI

The current task is to make the interfaces and protocol abstraction correct before implementing physical communication.

## 5. Protocol abstraction

`ComLogicalLink` must be protocol-agnostic.

D-PDU protocol/resource information must not be silently converted into ISO14230 merely because a protocol is not yet implemented.

Unknown protocols should remain identifiable as unknown.

The D-PDU protocol ID, bus type, resource ID, DLC pins and create flags are important information and must not be discarded.

## 6. GenericComPrimitive

`GenericComPrimitive` exists intentionally.

It is used for protocols that do not currently have specialized primitive implementations.

Do not remove it or replace it with arbitrary protocol-specific behavior unless there is a documented reason.

The generic primitive is currently an acceptance/staging mechanism, not a real hardware implementation.

## 7. Existing specialized primitives

ISO14230 and KW82 currently have specialized `ComPrimitive` subclasses.

Do not replace them with GenericComPrimitive without first understanding their existing behavior.

## 8. J2534 implementation

All public J2534 PassThru functions should exist and delegate to the appropriate internal abstraction.

The project intentionally separates:

* J2534 API
* channel management
* protocol handlers
* VCI abstraction
* physical hardware

Keep those layers separate.

## 9. Hardware boundary

The eventual design is for `VCI_*` to receive protocol/data/configuration information and let the future firmware decide what physical protocols are supported.

A protocol that is not physically supported may later be handled by a controlled synthetic response/acceptance mechanism so Tech2Win can continue its discovery/configuration process.

Do not implement that behavior yet unless explicitly requested.

## 10. Logging

Logging is important during development.

Preserve useful trace information around:

* D-PDU protocol/resource creation
* ComLogicalLink creation
* protocol mapping
* channel creation
* StartComPrimitive
* ProcessCop
* STARTCOMM
* STOPCOMM
* SENDRECV
* J2534 calls
* VCI calls

Do not remove diagnostic logging merely to simplify code.

## 11. Safety when editing

Prefer small, testable changes.

Do not rewrite large portions of the architecture without first explaining why.

When a change is proposed, identify:

* current behavior
* desired behavior
* exact code path
* affected files
* possible side effects

## 12. Validation

After code changes:

* build the affected project;
* inspect compiler errors/warnings;
* run relevant tests or diagnostic tools if available;
* inspect git diff;
* do not claim a change works without verification.

If the repository has existing build/test instructions, follow them.

## 13. Documentation

`docs/NYANKO_PROJECT.md` contains historical and architectural context.

`docs/CURRENT_STATE.md` describes the current state.

If an architectural decision changes, update the appropriate documentation.

Do not turn `AGENTS.md` into a large project encyclopedia.

## 14. Communication style

The human developer is Spanish-speaking and prefers detailed technical explanations.

When explaining changes, be explicit about the actual call chain and distinguish:

* verified facts from source code/logs;
* observations from experiments;
* hypotheses;
* planned future behavior.

Never present an assumption as something already implemented.
