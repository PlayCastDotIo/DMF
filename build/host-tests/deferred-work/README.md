# Reusable deferred-work joins

From the DMF repository root:

```powershell
./build/host-tests/deferred-work/Test.ps1 -Configuration Debug
./build/host-tests/deferred-work/Test.ps1 -Configuration Release
```

`DMF_ScheduledTask_DeferredJoin` joins on-demand work without permanently stopping
the scheduler. It does not join timer or synchronous ExecuteNow actors.
`DMF_QueuedWorkItem_Drain` also requires every queued descriptor to have retired
after client callback return and framework buffer reuse. Residual calls or an
orphan descriptor return `STATUS_DEVICE_BUSY`; that result does not authorize reuse.

Callers must close and join producers before invoking either method, retain the
modules, run at PASSIVE_LEVEL, and hold no lock needed by callbacks. Do not call
from a callback being joined. These methods neither cancel nor reschedule work;
existing Cancel/Flush behavior is unchanged.

Nine host fault-injection scenarios compile the production join implementations.
They cover reusable joins, reference failures, pending calls, descriptor retirement,
child failures and callback/buffer-reuse barriers. Framework behavior is injected;
these are not installed WDF, Driver Verifier or IRQL validation results.
