;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Project: n64hijack
;; File: patcher.asm
;; Author: jungerman <jungerman.mail@gmail.com>
;; 
;; A majority of the file is originally by parasyte <jay@kodewerx.org>, adapted from his alt64
;;    repository found here: https://github.com/parasyte/alt64
;; This file is somewhat messy as it contains some syntax necessary to let this compile under
;;    anarko's N64ASM assembler. This is no longer relevant since hcs's u64asm assember is used, but
;;    some bits are still in here, nonetheless.
;; The comments found in this file are a mixture of my own and all the original comments found in
;;    parasyte's usage of this assembly.
;; I am not entirely sure if this is still the case, but at one point this file required that the
;;    patcher label be placed at an address ending with 0x0000.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

#include   patcher_address.asm
    org    $B0000000 + PATCHER_ADDRESS

; Installs general exception handler, router, and code engine
patcher:
    lui    t5, patcher > $10   ; Start of temporary patcher location
    lui    t6, $8000           ; Start of cached memory
    lui    t7, $007F           ; Address mask
    ori    t7, $FFFF
    lui    t8, $807C           ; Permanent code engine location
    ori    t8, $5C00
    lui    t9, cheat_list > $10
    ori    t9, cheat_list & $FFFF    ; Get temporary code lists location (stored before patcher)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; BOOT-TIME CHEATS

load_next_boot_cheat:
    lw     v0, $0000(t9)
    bnez   v0, boot_cheat_type ; if it's equal to zero, we're done with the pre-boot cheats.
    lw     v1, $0004(t9)       ; t9 = code list location
    beqz   v1, install_geh     ; Only gets here if first branch doesn't happen (cheat addr = 0)

boot_cheat_type:
    addiu  t9, t9, $0008       ; t9 = 2 words after code list location
    srl    t2, v0, 24          ; t2 = code type
    addiu  at, zero, $EE
    beq    t2, at, boot_cheat_ee
    addiu  at, zero, $F0
    and    v0, v0, t7
    beq    t2, at, boot_cheat_f0
    or     v0, v0, t6
    addiu  at, zero, $F1
    beq    t2, at, boot_cheat_f1
    nop

    ; Fall through, assume FF (probably bad idea)
    ; Apply FF code type
    addiu  at, zero, $FFFC          ; Mask address
    beq    zero, zero, load_next_boot_cheat  ; Our assembler doesn't support the b pseudoinstruction...
    and    t8, v0, at               ; Update permanent code engine location

boot_cheat_f1:
    ; Apply F1 code type
    beq    zero, zero, load_next_boot_cheat
    sh     v1, $0000(v0)

boot_cheat_f0:
    ; Apply F0 code type
    beq    zero, zero, load_next_boot_cheat
    sb     v1, $0000(v0)

boot_cheat_ee:
    ; Apply EE code type
    lui    v0, $0040
    sw     v0, $0318(t6)
    beq    zero, zero, load_next_boot_cheat
    sw     v0, $03F0(t6)


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; INSTALLATION

install_geh:
    ; Install General Exception Handler
    srl    at, t8, 2
    and    v0, at, t7
    lui    at, $0800
    or     v0, v0, at
    ; Installs a jump to the code engine at $80000180
    sw     v0, $0180(t6)      ; v0 = $081F1700 -> j $807c5c00
    sw     zero, $0184(t6)    ; nop

    ; Install code engine to permanent location
    sw     t8, $0188(t6)      ; Save permanent code engine location
    addiu  at, zero, patcher & $FFFF
    addiu  v0, zero, code_engine_start & $FFFF
    addiu  v1, zero, code_engine_end & $FFFF
    subu   at, v0, at         ; at = patcher length
    subu   v1, v1, v0         ; v1 = code engine length
    addu   v0, t5, at         ; v0 = temporary code engine location

loop_copy_code_engine:
    ; Actually do the copying of the code engine to its permanent locatoin
    lw     at, $0000(v0)
    addiu  v1, v1, -4
    sw     at, $0000(t8)
    addiu  v0, v0, 4
    bgtz v1, loop_copy_code_engine
    addiu  t8, t8, 4
    sw     t8, $018C(t6)      ; Save permanent code list location
                              ; comes right after the nop after the jump to the code engine at the GEH

loop_copy_code_list:
    ; Install in-game code list
    lw     v0, $0000(t9)      ; t9 points to temporary code list location for in-game codes
    lw     v1, $0004(t9)
    addiu  t9, t9, 8
    sw     v0, $0000(t8)      ; store code list after code engine
    sw     v1, $0004(t8)      ; ^
    bnez   v0, loop_copy_code_list
    addiu  t8, t8, 8
    bnez   v1, loop_copy_code_list
    nop                       ; only continue if both address and data are zero

    ; Write cache to physical memory and invalidate (GEH)
    ori    t0, t6, $0180
    addiu  at, zero, $0010

loop_cache_geh:  ; Iterates 4 times, so 0180 - 018F get updated
    cache  $19, $0000(t0)     ; Data cache hit writeback
    cache  $10, $0000(t0)     ; Instruction cache hit invalidate
    addiu  at, at, -4
    bnez   at, loop_cache_geh
    addiu  t0, t0, 4


    ; Write cache to physical memory and invalidate (code engine + list)
    lw     t0, $0188(t6)
    subu   at, t8, t0
loop_cache_code_engine:
    cache  $19, $0000(t0)     ; Data cache hit writeback
    cache  $10, $0000(t0)     ; Instruction cache hit invalidate
    addiu  at, at, -4
    bnez   at, loop_cache_code_engine
    addiu  t0, t0, 4


    ; Protect GEH via WatchLo/WatchHi
    addiu  t0, zero, $0181    ; Watch $80000180 for writes
    mtc0   t0, watchlo        ; Cp0 WatchLo
    nop
    mtc0   zero, watchhi      ; Cp0 WatchHi


    ; Start game!
#include   overwritten.asm
    jr     t4
    nop


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; CODE ENGINE

code_engine_start:
    mfc0   k0, cause          ; Cp0 Cause
    andi   k1, k0, $1000      ; Pre-NMI
    bnezl  k1, not_nmi        ; If this is an NMI interrupt, clear WatchLo (since it's not automatically cleared on reboot)
    mtc0   zero, watchlo      ; Cp0 WatchLo

not_nmi:
    andi   k0, k0, $7C
    addiu  k1, zero, $5C      ; Watchpoint
    bne    k0, k1, run_code_engine

    ; Watch exception; manipulate register contents
    ; Changes things to be stored at $80000120 instead of GEH assuming they 
    mfc0   k1, epc            ; Cp0 EPC
    lw     k1, $0000(k1)      ; Load cause instruction
    lui    k0, $03E0
    and    k1, k1, k0         ; Mask (base) register                    ; Register holding offset
    srl    k1, k1, 5          ; Shift it to the "rt" position           ; Upper half of k1 now contains register number
    lui    k0, $3740          ; Upper half "ori <reg number contained in k1>, k0, $0120"     
    or     k1, k1, k0
    ori    k1, k1, $0120      ; Lower half "ori <reg number contained in k1>, k0, $0120"
    lui    k0, $8000
    lw     k0, $0188(k0)      ; Load permanent code engine location     ; Stored during installation
    sw     k1, $0060(k0)      ; Self-modifying code FTW!                ; Place the modified instruction at the placeholder below
    cache  $19, $0060(k0)     ; Data cache hit writeback                ; Cache hit our updated instruction below
    cache  $10, $0060(k0)     ; Instruction cache hit invalidate        ; ^
    lui    k0, $8000
    nop                       ; Short delay for cache sync
    nop
    nop
    nop                       ; Placeholder for self-modifying code     ; Offset reg is changed to $80000120 here
    eret

run_code_engine:
    ; Run code engine
    lui    k0, $8000
    lw     k0, $0188(k0)      ; k0 now contains the address of the code engine
    addiu  k0, k0, -40        ; We're storing things in the 28 bytes before the code engine (which is safe, I guess...)
    sd     v1, $0000(k0)      ; Back up registers we clobber
    sd     v0, $0008(k0)
    sd     t9, $0010(k0)
    sd     t8, $0018(k0)
    sd     t7, $0020(k0)

    ; Handle cheats
    lui    t9, $8000
    lw     t9, $018C(t9)      ; Get code list location
load_next_ingame_cheat:
    lw     v0, $0000(t9)      ; Load address
    bnez   v0, address_not_zero
    lw     v1, $0004(t9)      ; Load value
    beqz   v1, both_are_zero
    nop

    ; Address == 0 (TODO)
    beq    zero, zero, load_next_ingame_cheat

address_not_zero:
    ; Address != 0
    addiu  t9, t9, $0008
    srl    t7, v0, 24
    sltiu  k1, t7, $D0        ; Code type < $D0 ?
    sltiu  t8, t7, $D4        ; Code type < $D4 ?
    xor    k1, k1, t8         ; k1 = ($D0 >= code_type < $D4)
    addiu  t8, zero, $50
    bne    t7, t8, not_repeater     ; Code type != $50 ? -> 3

    ; GS Patch/Repeater
    srl    t8, v0, 8
    andi   t8, t8, $00FF      ; Get address count
    andi   t7, v0, $00FF      ; Get address increment
    lw     v0, $0000(t9)      ; Load address
    lw     k1, $0004(t9)      ; Load value
    addiu  t9, t9, $0008

repeater_write_loop:
    sh     k1, $0000(v0)      ; Repeater/Patch write
    addiu  t8, t8, -1
    addu   v0, v0, t7
    bnez   t8, repeater_write_loop
    addu   k1, k1, v1
    beq    zero, zero, load_next_ingame_cheat

not_repeater:
    ; GS RAM write or Conditional
    lui    t7, $0300
    and    t7, v0, t7         ; Test for 8-bit or 16-bit code type
    lui    t8, $A07F
    ori    t8, $FFFF
    and    v0, v0, t8
    lui    t8, $8000
    beqz   k1, gs_ram_write
    or     v0, v0, t8         ; Mask address

    ; GS Conditional
    sll    k1, t7, 7
    beqzl  k1, skip_word_write1
    lbu    t8, $0000(v0)      ; 8-bit conditional
    lhu    t8, $0000(v0)      ; 16-bit conditional
skip_word_write1:
    srl    t7, t7, 22
    andi   t7, t7, 8          ; Test for equal-to or not-equal-to
    beql   v1, t8, load_next_ingame_cheat
    add    t9, t9, t7         ; Add if equal
    xori   t7, t7, 8
    beq    zero, zero, load_next_ingame_cheat
    add    t9, t9, t7         ; Add if not-equal

gs_ram_write:
    ; GS RAM write
    sll    k1, t7, 7
    beqzl  k1, skip_word_write2
    sb     v1, $0000(v0)      ; Constant 8-bit write
    sh     v1, $0000(v0)      ; Constant 16-bit write
skip_word_write2:
    beq    zero, zero, load_next_ingame_cheat

both_are_zero:
    ; Restore registers from our temporary stack, and back to the game!
    ld     t7, $0020(k0)
    ld     t8, $0018(k0)
    ld     t9, $0010(k0)
    ld     v0, $0008(k0)
    j      $80000120
    ld     v1, $0000(k0)
code_engine_end:
patcher_end:

cheat_list:

    ; BEGIN PRE-BOOT CODES

    ; Insert codes here

    dw $00000000   ; END OF LIST
    dw $0000

    

    ; BEGIN IN-GAME CODES

    ; Example code: play as a glove in Super Mario 64
    ; dw $8133B4D6
    ; dw $3C4C

    dw $00000000   ; END OF LIST
    dw $0000