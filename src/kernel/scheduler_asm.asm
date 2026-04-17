[bits 32]

global Scheduler_Switch
Scheduler_Switch:
    ; Save current task's registers
    pushad

    ; Load parameters BEFORE modifying esp
    mov eax, [esp + 36]     ; eax = oldEsp (uint32_t*)
    mov ecx, [esp + 40]     ; ecx = newEsp (uint32_t)

    ; Save current ESP into old task
    mov [eax], esp

    ; Switch to new task's stack
    mov esp, ecx

    ; Restore registers of new task
    popad

    ; Jump to task entry (via return address)
    ret
