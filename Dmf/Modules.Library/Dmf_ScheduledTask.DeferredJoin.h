// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
// Private implementation included after the owning module's definitions.
#pragma code_seg("PAGE")
_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
DMF_ScheduledTask_DeferredJoin(_In_ DMFMODULE DmfModule)
{
    DMF_CONTEXT_ScheduledTask* moduleContext;
    NTSTATUS ntStatus;
    PAGED_CODE();
    DMFMODULE_VALIDATE_IN_METHOD(DmfModule, ScheduledTask);
    ntStatus = DMF_ModuleReference(DmfModule);
    if (!NT_SUCCESS(ntStatus)) return ntStatus;
    moduleContext = DMF_CONTEXT_GET(DmfModule);
    // Caller has closed deferred producers and retains the module. Never call
    // from its callback or hold a lock required by that callback. Timer and
    // synchronous ExecuteNow actors are not joined by this on-demand barrier.
    WdfWorkItemFlush(moduleContext->DeferredOnDemand);
    ntStatus = moduleContext->NumberOfPendingCalls == 0 ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    DMF_ModuleDereference(DmfModule);
    return ntStatus;
}
#pragma code_seg()
