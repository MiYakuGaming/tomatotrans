; 08078C18 draw's the character's name and "can be modified here" originally.
; The phrasing isn't great, and neither is the spacing.
; Let's rewrite to put it at the end.

@GimmickStatusBlock equ 0x084904B8

org 0x08078C18
.area 0x08078CAC-.,0x00
; Args: uint8_t charNumber, void *dest
.func MenuStatusGimmickPickWho
push r4-r7,r14
add sp,-8

; Originally, this temporarily put things in 0x030041DC before writing to the screen.
; We skip that, opting to draw directly.

; Add an 0x4040 offset (our actual dest) to the dest and move it to r7.
mov r2,0x40
add r7,r1,r2
lsl r2,8
add r7,r2

; Multiply r0 (charNumber) by 96 to get offset into char data, and save in r5.
; (x * 2 + x) * 32 = x * 96
lsl r5,r0,1
add r5,r0
lsl r5,r5,5

; Not sure what this is, I assume menu status struct?  We want the 0x37 offset.
ldr r0,=0x03004FF8 + 0x37
ldrb r0,[r0,0]
mov r1,1
and r1,r0 ; Sets Z/eq if zero.
bne @@return

; This has first the text pointer, then the size (bytes: x, y, clearWidth, 0.)
ldr r0,=@GimmickStatusBlock
ldr r1,[r0,4]
ldr r0,[r0,0]
; Draw8x8FixedStrIndexed takes the size on the stack.  We're not calling it immediately, but save now.
str r1,[sp,0]

; Calculate the width of the first line in that desc.
mov r3,0xFF
and r1,r3
mov r2,1
bl Calc8x8PixelWidth
; That kept r0 as str, but let's save r2 (pixelWidth) for later.
; While at it, let's convert to bytes.
lsl r4,r2,2

; Okay now let's draw it.  The size at sp is already there, r0 was str, r7=dest.
mov r3,r0
mov r2,0
mov r1,r7
mov r0,0
bl Draw8x8FixedStrIndexed

; And now the name of the character.  We calculated the offset earlier.
ldr r0,=0x03001EDC
add r0,r0,r5
; This is the length currently...
ldrb r1,[r0,4]

; Font drawing params struct.
ldr r2,=0x030018BC
strb r1,[r2,5]
str r0,[r2,12]

; Add dest (r7) and our calculated width (r4) of line 1.
add r1,r7,r4
; Add 7 pixels in width for a space.
add r1,28
str r1,[r2,8]
bl CopyString8x8ToVRAM

@@return:
add sp,8
pop r4-r7
pop r0
bx r0
.pool
.endfunc
.endarea
