global drmlist_draw_box_asm

section .data
start_x:    dq  0
start_y:    dq  0
box_width:  dq  32
box_height: dq  32
speed:      dq  0x05
go_right:   db  0xFF


section .text
; void drmlist_draw_box_asm(uint32_t* pixels, mydrm_data_t* screen, uint32_t color);
drmlist_draw_box_asm:
    ; rdi = uint32_t* pixels  
    ; rsi = mydrm_data_t* screen
    ; rdx = color

    ; Here we move function parameters into registers 
    mov eax, DWORD [rsi + 92]   ; move screen->height into EAX
    mov QWORD [box_height], rax ; move EAX into box_height (in memory)
    mov eax, DWORD [rsi + 88]   ; move screen->width into EAX
    mov r15, rax    ; width       move RAX into r15
    mov r14, rdi                ; move RDI (pixels) into R14
    xor r11, r11                ; y = 0

    ; Here we compare [go_right], and if true, plus start_x, else subtract start_x
    mov rax, [start_x]          ; move [start_x] into RAX
    cmp byte [go_right], 0x00      ; Compare [go_right] with 0
    jz go_left                  ; Jump to go_left if [go_right] is 0
    add rax, QWORD [speed]      ; else RAX += [speed]
    jmp done_speed_adding       ; Jump done_speed_adding
go_left: 
    sub rax, QWORD [speed]      ; RAX -= [speed]

done_speed_adding:
    mov QWORD [start_x], rax    ; Store RAX back to [start_x]

    ; Checking boundaries
    add rax, QWORD [box_width]  ; RAX += [box_width]        (RAX is still [start_x])
    cmp rax, r15                ; Compare RAX with R15 (screen->width)
    jge switch_go_right         ; if greater than or equal to, jump to switch_go_right
    cmp QWORD [start_x], 0      ; OR compare [start_x] with 0
    jle switch_go_right         ; jump if less or equal to switch_go_right
    jmp for_y_start             ; if false, do nothing
switch_go_right:
    not byte [go_right]         ; go_right = !go_right;

    ; Nested loop, y and x
for_y_start:
    xor r10, r10                ; x = 0
    cmp r11, QWORD [box_height] ; Compare R11 (Y) with [box_height]
    jge for_y_end               ; if R11 (Y) is greater than or equal, then jump to for_y_end

for_x_start:
    cmp r10, QWORD [box_width]  ; Compare R10 (X) with [box_width]
    jge for_x_end               ; if R10 (X) is greater than or equal, then jump to for_x_end

    ; Here we calculate the pixel position based on (x + start_x) + ((y + start_y) * screen->width)
    mov r12, QWORD [start_x]    ; move [start_x] into R12
    add r12, r10                ; R12 = R12 (start_x) + R10 (X)
    mov r13, QWORD [start_y]    ; move [start_y] into R13
    add r13, r11                ; R13 = R13 (start_y) + R11 (Y)
    imul r13, r15               ; R13 = R13 * R15 (screen->width)
    add r13, r12                ; R13 = R13 ((y + start_y) * screen->width) + R12 (x + start_x)
    imul r13, 4                 ; MUL it with 4, because each pixel is 4 bytes

    ; Compare if pixel position is bigger than pixels size
    mov eax, DWORD [rsi + 108]  ; move screen->front_buf into EAX
    imul rax, 40                ; MUL it with 40, because mydrm_fb_t is 40 bytes
    lea rax, [rsi + rax]        ; load address of screen->framebuffer[rax] into RAX
    mov ecx, DWORD [rax + 24]   ; move screen->framebuffer[screen->front_buf].size into ECX
    cmp r13, rcx                ; Compare R13 (pixel position) with RCX (pixels size)
    jge dont_write_pixel        ; if R13 greater than or equal to RCX (pixels size), then don't write pixel

    ; pixels[pos] = color
    lea rax, [r14 + r13]        ; Load the address with the calculated X and Y position
    mov DWORD [rax], edx        ; Move EDX (color) into pixels[position]
dont_write_pixel:

    inc r10                     ; x++
    jmp for_x_start
for_x_end:

    inc r11                     ; y++
    jmp for_y_start
for_y_end:

    ret
