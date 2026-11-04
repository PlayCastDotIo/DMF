# Terminal notification cancellation and join

From the DMF repository root:

```powershell
./build/host-tests/notify-cancel/Test.ps1 -Configuration Debug
./build/host-tests/notify-cancel/Test.ps1 -Configuration Release
```

`DMF_NotifyUserWithRequest_EventRequestCancelAndJoin` synchronously purges the
request queue through its configured cancellation callback. Success requires
request completion, callback return and zero held-request accounting. It retains
a module reference, does not retry completion and leaves the queue purged.

First stop/join all forwarding, retrieval and data producers, serialize terminal
calls, retain the module and use PASSIVE_LEVEL. Never invoke this API from a WDF
request-dispatch callback or while holding a lock needed by cancellation. Client
deferred actors must be joined separately before their context is released.
Residual accounting returns busy, even if the native queue is empty. Failed
request-context allocation and queue forwarding release their reserved slots
without completing the caller-owned request.

Eight host fault-injection scenarios compile the production cancellation callback
and join implementation. They exercise reference failure, missing/empty queues,
pending requests, deferred completion, callback return and residual accounting.
Injected framework barriers do not establish real WDF/Verifier behavior.
