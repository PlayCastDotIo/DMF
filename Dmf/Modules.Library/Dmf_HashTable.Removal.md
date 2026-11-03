## HashTable removal and lookup without insertion

`DMF_HashTable_FindExistingEx` has the callback/context contract of `FindEx`,
but returns `STATUS_NOT_FOUND` without inserting an absent key. Existing
`Find`/`FindEx` behavior remains compatible. The callback runs under the module
lock and can update the existing value according to its configured bounds.

`DMF_HashTable_Remove` removes an existing key, scrubs its storage and immediately
reclaims one entry of capacity. Missing keys return `STATUS_NOT_FOUND` without
changing the table. Live entries remain visible to Read, Write, Find and
Enumerate. Enumeration order is unspecified and may change after removal.

`DMF_HashTable_RemoveEx` accepts an optional `EVT_DMF_HashTable_Remove` predicate
and context. It checks the current key/value under the same lock as removal.
Returning FALSE preserves the entry and reports `STATUS_NOT_FOUND`: no matching
key/value was removed. A NULL predicate performs unconditional removal.

This supports pointer-valued registries that compare their expected object with
the current value, preventing stale cleanup from removing a replacement at the
same key. Acquire object lifetime references in a locked lookup callback. Remove
the association under the table lock, then stop/destroy/dereference the object
outside the lock. The module owns its byte storage, not any objects represented
by those bytes; removal invokes no object destructor or reference release.

Callbacks must not retain entry/value addresses, reenter the module, block,
destroy objects or invoke operations that can reenter the table. Entry addresses
and internal indices are not stable outside the module lock. Existing callbacks
and enumeration remain serialized with removal. Dispatch-level restrictions
match the existing HashTable methods.

The implementation compacts the occupied entry prefix by moving the last entry
into a removed slot and repairing its hash/collision link. It allocates no new
memory, preserves fixed capacity and scrubs the vacated slot. The custom hash
callback must remain consistent for stored keys, as required by existing reads.

Host contracts compile the complete production module with injected DMF/WDF
boundaries and exercise its public APIs. They cover all deletion orders with
default hashing and forced collisions, enumeration and read preservation,
non-inserting misses, conditional stale-value rejection, repeated capacity reuse
and concurrent existing-entry callback updates. These tests do not substitute
for kernel/UMDF runtime validation.
