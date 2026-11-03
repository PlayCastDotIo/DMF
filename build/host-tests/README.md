# Notification request accounting host contracts

Run from the DMF repository root with Visual Studio 2022 C++ tools and a Windows SDK installed:

```powershell
./build/Test-NotifyRequestAccounting.ps1 -Configuration Debug
./build/Test-NotifyRequestAccounting.ps1 -Configuration Release
```

These tests compile `DMF_NotifyUserWithRequest_EventRequestAdd` directly from
the production module source, injecting only framework boundaries. They verify
that a failed module reference, timestamp-context allocation, cancelled forward
or stopped-queue forward does not consume the next pending-request slot. They
also check that rejecting an over-capacity add preserves the existing request's
reservation until its cancellation/return boundary.

The five scenarios are host-only fault-injection contracts. They require no WDK,
driver installation or device access. They do not establish real WDF queue
cancellation ordering, IRQL correctness or Driver Verifier behavior. Installed
KMDF/UMDF validation remains separate. No library packaging or release is performed.
