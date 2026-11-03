// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
// Private implementation included after the owning module's definitions.
#pragma code_seg("PAGE")
_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
DMF_QueuedWorkItem_Drain(_In_ DMFMODULE DmfModule)
{
    DMF_CONTEXT_QueuedWorkItem* moduleContext;
    NTSTATUS ntStatus;
    PAGED_CODE();
    DMFMODULE_VALIDATE_IN_METHOD(DmfModule, QueuedWorkItem);
    ntStatus = DMF_ModuleReference(DmfModule);
    if (!NT_SUCCESS(ntStatus)) return ntStatus;
    moduleContext = DMF_CONTEXT_GET(DmfModule);
    // Includes client callback return and the framework's buffer reuse. An
    // orphan descriptor from failed scheduling must not be declared retired.
    ntStatus = DMF_ScheduledTask_DeferredJoin(moduleContext->DmfModuleScheduledTask);
    if (ntStatus == STATUS_SUCCESS && DMF_BufferQueue_Count(moduleContext->DmfModuleBufferQueue) != 0)
        ntStatus = STATUS_DEVICE_BUSY;
    DMF_ModuleDereference(DmfModule);
    return ntStatus;
}
#pragma code_seg()
