org 0x7C00 ; org tells the assembler where to load our code, and 0x7C00 is the memory address where BIOS loads the boot sector
bits 16 ; we need to make our code 16 bits because it needs to be backwards compatible


%define ENDL 0x0D, 0x0A

start:
    jmp main

;prints a string to the screen
;Params: 
;- ds:si points to string

puts:
    ; save the registers we are going to modify
    push si
    push ax

.loop:
    lodsb ; loads next character to al
    or al, al ; checks if next character is null
    jz .done

    mov ah, 0x0E        ; call bios interrupt
    mov bh, 0           ; set page number to 0
    int 0x10
    jmp .loop

.done:
    pop ax
    pop si
    ret
    

main:
    ; set up data segments
    mov ax, 0 ; ; cant write directly to ds/es (data segment and extra segment)
    mov ds, ax
    mov es, ax

    ; set up stack segment
    mov ss, ax
    mov sp, 0x7C00

    mov si, msg_hello
    call puts

    hlt

.halt:
    jmp .halt


msg_hello: db 'Welcome to UamiOS!', ENDL, 0

times 510-($-$$) db 0
dw 0AA55h


