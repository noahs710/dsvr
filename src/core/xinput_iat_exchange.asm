; Native x64 CAS for the pinned PE image's unaligned, within-cache-line IAT slots.
PUBLIC DsvrIatCompareExchange
.code
DsvrIatCompareExchange PROC
    mov rax, rdx
    lock cmpxchg qword ptr [rcx], r8
    ret
DsvrIatCompareExchange ENDP
END
