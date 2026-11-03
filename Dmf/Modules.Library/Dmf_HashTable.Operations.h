/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT license. */
// Private implementation included by Dmf_HashTable.c after its context/helpers.
// No entry address or index escapes the module lock.
static ULONG*
HashTable_EntryLinkFind(DMF_CONTEXT_HashTable* ModuleContext, ULONG_PTR Hash, ULONG EntryIndex)
{
    ULONG* link = &ModuleContext->HashMap[Hash % ModuleContext->HashMapSize];
    while (*link != INVALID_INDEX && *link != EntryIndex)
    {
        link = &HashTable_IndexToDataEntry(ModuleContext, *link)->NextEntryIndex;
    }
    return *link == EntryIndex ? link : NULL;
}

static NTSTATUS
HashTable_EntryRemoveLocked(DMFMODULE DmfModule, DATA_ENTRY* Entry)
{
    DMF_CONTEXT_HashTable* moduleContext = DMF_CONTEXT_GET(DmfModule);
    ULONG index = (ULONG)(((UCHAR*)Entry - (UCHAR*)moduleContext->DataTable) / moduleContext->DataEntrySize);
    ULONG lastIndex = moduleContext->DataEntriesAllocated - 1;
    DATA_ENTRY* last = HashTable_IndexToDataEntry(moduleContext, lastIndex);
    ULONG_PTR hash = moduleContext->EvtHashTableHashCalculate(DmfModule, HashTable_KeyBufferGet(Entry), Entry->KeyLength);
    ULONG* link = HashTable_EntryLinkFind(moduleContext, hash, index);
    ULONG* lastLink = NULL;
    if (!link) return STATUS_INVALID_DEVICE_STATE;
    if (index != lastIndex)
    {
        hash = moduleContext->EvtHashTableHashCalculate(DmfModule, HashTable_KeyBufferGet(last), last->KeyLength);
        lastLink = HashTable_EntryLinkFind(moduleContext, hash, lastIndex);
        if (!lastLink) return STATUS_INVALID_DEVICE_STATE;
    }
    // Keep the occupied prefix dense so allocation and enumeration retain their
    // existing behavior. Repair collision links before moving the last entry.
    *link = Entry->NextEntryIndex;
    if (lastLink)
    {
        if (lastLink == &Entry->NextEntryIndex) lastLink = link;
        *lastLink = index;
        RtlCopyMemory(Entry, last, moduleContext->DataEntrySize);
    }
    RtlSecureZeroMemory(last, moduleContext->DataEntrySize);
    --moduleContext->DataEntriesAllocated;
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
DMF_HashTable_FindExistingEx(_In_ DMFMODULE DmfModule,
                           _In_reads_(KeyLength) UCHAR* Key,
                           _In_ ULONG KeyLength,
                           _In_ EVT_DMF_HashTable_FindEx* CallbackFindEx,
                           _In_ VOID* CallbackContext)
{
    DMF_CONTEXT_HashTable* moduleContext;
    DATA_ENTRY* entry;
    NTSTATUS status;
    DMFMODULE_VALIDATE_IN_METHOD(DmfModule, HashTable);
    moduleContext = DMF_CONTEXT_GET(DmfModule);
    if ((!Key && KeyLength) || KeyLength > moduleContext->MaximumKeyLength || !CallbackFindEx)
        return STATUS_INVALID_PARAMETER;
    DMF_ModuleLock(DmfModule);
    status = HashTable_DataEntryFind(DmfModule, Key, KeyLength, &entry);
    if (NT_SUCCESS(status))
    {
        CallbackFindEx(DmfModule, CallbackContext, Key, KeyLength,
                       HashTable_ValueBufferGet(entry), &entry->ValueLength);
    }
    DMF_ModuleUnlock(DmfModule);
    return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
DMF_HashTable_RemoveEx(_In_ DMFMODULE DmfModule,
                     _In_reads_(KeyLength) UCHAR* Key,
                     _In_ ULONG KeyLength,
                     _In_opt_ EVT_DMF_HashTable_Remove* CallbackRemove,
                     _In_opt_ VOID* CallbackContext)
{
    DMF_CONTEXT_HashTable* moduleContext;
    DATA_ENTRY* entry;
    NTSTATUS status;
    DMFMODULE_VALIDATE_IN_METHOD(DmfModule, HashTable);
    moduleContext = DMF_CONTEXT_GET(DmfModule);
    if ((!Key && KeyLength) || KeyLength > moduleContext->MaximumKeyLength)
        return STATUS_INVALID_PARAMETER;
    DMF_ModuleLock(DmfModule);
    status = HashTable_DataEntryFind(DmfModule, Key, KeyLength, &entry);
    if (NT_SUCCESS(status))
    {
        if (CallbackRemove && !CallbackRemove(DmfModule, CallbackContext, Key, KeyLength,
                                             HashTable_ValueBufferGet(entry), entry->ValueLength))
            status = STATUS_NOT_FOUND;
        else status = HashTable_EntryRemoveLocked(DmfModule, entry);
    }
    DMF_ModuleUnlock(DmfModule);
    return status;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_
NTSTATUS
DMF_HashTable_Remove(_In_ DMFMODULE DmfModule,
                   _In_reads_(KeyLength) UCHAR* Key,
                   _In_ ULONG KeyLength)
{
    return DMF_HashTable_RemoveEx(DmfModule, Key, KeyLength, NULL, NULL);
}
