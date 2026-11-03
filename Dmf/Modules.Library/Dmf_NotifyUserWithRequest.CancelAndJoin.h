// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
// Private implementation included after the owning module's definitions.
#pragma code_seg("PAGE")
_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(_In_ DMFMODULE DmfModule)
{
    DMF_CONTEXT_NotifyUserWithRequest* moduleContext;
    NTSTATUS ntStatus;
    PAGED_CODE();
    DMFMODULE_VALIDATE_IN_METHOD(DmfModule, NotifyUserWithRequest);
    ntStatus = DMF_ModuleReference(DmfModule);
    if (!NT_SUCCESS(ntStatus)) return ntStatus;
    moduleContext = DMF_CONTEXT_GET(DmfModule);
    if (moduleContext->EventRequestQueue == NULL)
    {
        ntStatus = STATUS_INVALID_DEVICE_STATE;
    }
    else
    {
        // Purge delivers cancellation through EvtIoCanceledOnQueue, rather than
        // ReturnAll's direct completion. It waits for requests AND cancellation
        // callback return. The caller has already joined all queue producers and
        // retrieval/completion actors, and must not hold callback-required locks.
        WdfIoQueuePurgeSynchronously(moduleContext->EventRequestQueue);
        ntStatus = moduleContext->EventCountHeld == 0 ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    }
    // The queue stays purged. This terminal method never restarts it or retries
    // completion. Retain dependencies on any nonexact result.
    DMF_ModuleDereference(DmfModule);
    return ntStatus;
}
#pragma code_seg()
