# MSP430 Instruction Set

A compact reference to the classic MSP430 CPU (not MSP430X) as mspsim implements it. TI's description is in the *MSP430x2xx Family User's Guide* (SLAU144), chapter 3. Where the hardware leaves details open, this document follows mspsim, which follows openMSP430; those points are marked **(mspsim)**.

## 1. Overview

- 16-bit RISC CPU with 16 registers, 27 core instructions and 7 addressing modes.
- One 64 KB address space for code, data and peripherals (von Neumann).
- Byte-addressed, little-endian. Words live at even addresses.
- Every instruction is one opcode word, followed by 0–2 extension words: first the source's, then the destination's.
- Most instructions work on words or on bytes (`.B` suffix; `.W` or none means word).

## 2. Memory map

The classic layout of an MSP430G2xx device, as mspsim uses it:

| Range | Use |
|---|---|
| 0x0000–0x000F | Special function registers (IE1, IFG1, IE2, IFG2, …) |
| 0x0010–0x00FF | 8-bit peripherals |
| 0x0100–0x01FF | 16-bit peripherals |
| 0x0200–… | RAM (512 bytes on a G2553: 0x0200–0x03FF) |
| 0x1000–0x10FF | Information flash |
| 0xC000–0xFFFF | Main flash (16 KB on a G2553) |
| 0xFFE0–0xFFFF | Interrupt vectors, inside flash |

**(mspsim)** All 64 KB is plain RAM: flash is writable, and there are no peripherals except these registers:

| Address | Register | Behaviour |
|---|---|---|
| 0x0001 | `IE2` | Bit 0 `UCA0RXIE` enables the UART receive interrupt (vector 7, 0xFFEE) |
| 0x0003 | `IFG2` | Bit 1 `UCA0TXIFG` always reads 1; bit 0 `UCA0RXIFG` is 1 while an input byte waits |
| 0x0066 | `UCA0RXBUF` | Read: the input byte; clears `UCA0RXIFG` |
| 0x0067 | `UCA0TXBUF` | Write: the byte goes to stdout |
| 0x01FE | stop register | Any write ends the run; the value is the exit status |

At startup the information flash and 0xC000–0xFFFF read as erased flash (0xFF).

### Memory access rules

- A word access ignores address bit 0, so it always touches the aligned word.
- A byte access reads or writes exactly one byte.
- Opcode and extension-word fetches are word accesses at PC.

## 3. Registers

| Register | Name | Role |
|---|---|---|
| R0 | PC | Program counter. Points at the next word to fetch. |
| R1 | SP | Stack pointer. The stack grows down; SP points at the last pushed word. |
| R2 | SR / CG1 | Status register; also constant generator 1 |
| R3 | CG2 | Constant generator 2. Reads give constants; writes are discarded. |
| R4–R15 | | General purpose |

All registers are 16 bits wide. Any instruction may use any register, including PC, SP and SR.

### Register writes

- A byte operation with a register destination writes the low byte and clears the high byte.
- Writes to R3 are discarded.
- SR keeps only bits 8–0. Bits 15–9 are reserved and read as 0 **(mspsim)**.

### Status register (SR)

| Bit | Mask | Name | Meaning |
|---|---|---|---|
| 0 | 0x0001 | C | Carry: carry out of the MSB; for subtraction, 1 means no borrow |
| 1 | 0x0002 | Z | Zero: the result is 0 |
| 2 | 0x0004 | N | Negative: the result's MSB (bit 15 for words, bit 7 for bytes) |
| 3 | 0x0008 | GIE | Maskable interrupts enabled |
| 4 | 0x0010 | CPUOFF | CPU stopped (low-power mode) |
| 5 | 0x0020 | OSCOFF | Crystal oscillator off |
| 6 | 0x0040 | SCG0 | System clock generator 0 off |
| 7 | 0x0080 | SCG1 | System clock generator 1 off |
| 8 | 0x0100 | V | Overflow: the signed result does not fit |
| 15–9 | | — | Reserved |

The low-power modes are combinations of these bits: LPM0 = CPUOFF, LPM1 = CPUOFF + SCG0, LPM2 = CPUOFF + SCG1, LPM3 = CPUOFF + SCG0 + SCG1, LPM4 = all four. mspsim has no clocks, so only CPUOFF has an effect (§11).

## 4. Instruction formats

```
Format I, two operands (opcodes 0x4000–0xFFFF)
 15     12 11      8   7    6   5  4  3      0
+---------+---------+----+-----+----+---------+
| opcode  |  S-reg  | Ad | B/W | As |  D-reg  |
+---------+---------+----+-----+----+---------+

Format II, one operand (opcodes 0x1000–0x13FF)
 15                    10 9      7   6   5  4  3      0
+------------------------+--------+-----+----+---------+
|      0 0 0 1 0 0       | opcode | B/W | As | D/S-reg |
+------------------------+--------+-----+----+---------+

Format III, jumps (opcodes 0x2000–0x3FFF)
 15   13 12     10 9                          0
+-------+---------+----------------------------+
| 0 0 1 | cond    | 10-bit signed word offset  |
+-------+---------+----------------------------+
```

- **B/W**: 0 = word, 1 = byte.
- **As** (2 bits): the source addressing mode.
- **Ad** (1 bit): the destination addressing mode.

Opcodes 0x0000–0x0FFF and 0x1380–0x13FF are illegal. mspsim stops with "illegal instruction", leaving PC on the instruction.

## 5. Addressing modes

### Source (As) and destination (Ad)

| As/Ad | Register | Syntax | Mode | Operand | Ext. word |
|---|---|---|---|---|---|
| 00 / 0 | Rn | `Rn` | Register | Rn itself | — |
| 01 / 1 | Rn | `X(Rn)` | Indexed | memory at Rn + X | X |
| 01 / 1 | R0 (PC) | `ADDR` | Symbolic | memory at ext-word address + X | X = ADDR − ext-word address |
| 01 / 1 | R2 (SR) | `&ADDR` | Absolute | memory at X | X = ADDR |
| 10 / — | Rn | `@Rn` | Indirect | memory at Rn | — |
| 11 / — | Rn | `@Rn+` | Autoincrement | memory at Rn, then Rn += 1 or 2 | — |
| 11 / — | R0 (PC) | `#N` | Immediate | N (it is `@PC+`) | N |

- **Destinations** have only Ad = 0 (`Rn`) and Ad = 1 (`X(Rn)`, `ADDR`, `&ADDR`). An assembler writes `@Rn` as a destination as `0(Rn)`.
- **Symbolic offsets** are taken from the address of the extension word itself, not from the instruction.
- **Autoincrement step:** 1 for byte operations, 2 for words. SP always steps by 2, even for bytes.
- **When the increment happens:** in a Format I instruction, the source register is incremented *after* the destination address is computed. So `mov @sp+, 8(sp)` writes to old SP + 8 **(mspsim)**.

### Constant generators

R2 and R3 in some modes produce constants with no extension word:

| Register | As | Constant |
|---|---|---|
| R2 | 10 | 4 |
| R2 | 11 | 8 |
| R3 | 00 | 0 |
| R3 | 01 | 1 |
| R3 | 10 | 2 |
| R3 | 11 | −1 (0xFFFF; 0xFF for bytes) |

R2 with As = 00 is the SR register; R2 with As = 01 is absolute mode. Assemblers encode `#0`, `#1`, `#2`, `#4`, `#8` and `#-1` this way, which makes the instruction shorter and faster.

## 6. Byte and word operations

- `.B` operations read and write single bytes in memory, and set N from bit 7, C from carry out of bit 7, and V from bit 7.
- A byte result written to a register clears its high byte; the instructions that write nothing (CMP, BIT) leave the register unchanged.
- `SWPB`, `SXT` and `CALL` are word-only; their B/W bit is ignored.

## 7. Core instructions

Notation:
- `src`, `dst`: the operands.
- `C`: the carry flag before the instruction.
- `.B` applies the same operation to bytes.
- Flags: `*` = set from the result, `0` = cleared, `-` = unchanged.

### Format I (two operands)

| Opcode | Mnemonic | Operation | V | N | Z | C |
|---|---|---|---|---|---|---|
| 4 | `MOV(.B) src, dst` | dst = src | - | - | - | - |
| 5 | `ADD(.B) src, dst` | dst = dst + src | * | * | * | * |
| 6 | `ADDC(.B) src, dst` | dst = dst + src + C | * | * | * | * |
| 7 | `SUBC(.B) src, dst` | dst = dst + ~src + C | * | * | * | * |
| 8 | `SUB(.B) src, dst` | dst = dst + ~src + 1 | * | * | * | * |
| 9 | `CMP(.B) src, dst` | dst + ~src + 1, flags only | * | * | * | * |
| A | `DADD(.B) src, dst` | dst = src + dst + C, in BCD | * | * | * | * |
| B | `BIT(.B) src, dst` | src & dst, flags only | 0 | * | * | ¬Z |
| C | `BIC(.B) src, dst` | dst = dst & ~src | - | - | - | - |
| D | `BIS(.B) src, dst` | dst = dst \| src | - | - | - | - |
| E | `XOR(.B) src, dst` | dst = dst ^ src | * | * | * | ¬Z |
| F | `AND(.B) src, dst` | dst = dst & src | 0 | * | * | ¬Z |

**Arithmetic (ADD, ADDC, SUBC, SUB, CMP).** All five use one adder: `dst + addend + carry-in`.

| Instruction | Addend | Carry-in |
|---|---|---|
| ADD | src | 0 |
| ADDC | src | C |
| SUBC | ~src | C |
| SUB, CMP | ~src | 1 |

- **C** is the carry out of the MSB. For SUB and CMP, C = 1 means dst ≥ src as unsigned (no borrow).
- **V** is set when the addend and dst have the same sign but the result's sign differs.
- **N** and **Z** come from the result.

**DADD.** Adds src, dst and C as 4 (word) or 2 (byte) BCD digits. Each digit sum above 9 has 10 subtracted and carries 1 into the next digit.
- C is the carry out of the top digit: the result exceeded 9999 (or 99 for bytes).
- N is the MSB of the result, and Z is set when the result is 0.
- V: TI leaves it undefined. mspsim sets it as for a binary addition of src and dst giving the BCD result **(mspsim)**.

**XOR.** V is set when both operands are negative.

**BIC and BIS** with SR as destination are how programs change GIE and the low-power bits (e.g. `bis #0x18, sr` enters LPM0 with interrupts enabled).

### Format II (one operand)

| Bits 9–7 | Mnemonic | Operation | V | N | Z | C |
|---|---|---|---|---|---|---|
| 000 | `RRC(.B) dst` | C → MSB → … → LSB → C | 0 | * | * | * |
| 001 | `SWPB dst` | swap the two bytes | - | - | - | - |
| 010 | `RRA(.B) dst` | MSB → MSB → … → LSB → C | 0 | * | * | * |
| 011 | `SXT dst` | bits 15–8 = bit 7 | 0 | * | * | ¬Z |
| 100 | `PUSH(.B) src` | SP −= 2; @SP = src | - | - | - | - |
| 101 | `CALL src` | SP −= 2; @SP = PC; PC = src | - | - | - | - |
| 110 | `RETI` | SR = @SP+; PC = @SP+ | restored | | | |

Encoding: `0x1000 | op << 7 | B << 6 | As << 4 | reg`.

- **RRC and RRA** set C from the bit shifted out of the LSB. RRC shifts the old C into the MSB; RRA keeps the MSB, so it divides a signed value by 2.
- **PUSH and CALL** decrement SP *before* computing the operand, so `push sp`, `call @sp` and `call 2(sp)` see the new SP. The value goes to the decremented address, even if `@SP+` then moves SP **(mspsim)**.
- **PUSH.B** writes one byte; SP still moves by 2.
- **CALL** pushes the address of the next instruction, after any extension word.
- **RETI** pops SR first, then PC. Its operand bits are ignored, so 0x1300–0x137F all decode as RETI. Bits 9–7 = 111 (0x1380–0x13FF) are illegal.

### Format III (jumps)

| Cond | Mnemonic | Alias | Jumps if |
|---|---|---|---|
| 000 | `JNE` | `JNZ` | Z = 0 |
| 001 | `JEQ` | `JZ` | Z = 1 |
| 010 | `JNC` | `JLO` | C = 0 (unsigned <) |
| 011 | `JC` | `JHS` | C = 1 (unsigned ≥) |
| 100 | `JN` | | N = 1 |
| 101 | `JGE` | | N = V (signed ≥) |
| 110 | `JL` | | N ≠ V (signed <) |
| 111 | `JMP` | | always |

- **Target** = jump address + 2 + 2 × offset, where offset is the signed 10-bit field (−512…+511 words).
- **Encoding:** `0x2000 | cond << 10 | (offset & 0x3FF)`; for example `jmp $` is 0x3FFF.
- **Flags:** jumps leave the flags unchanged.
- **Missing conditions:** there is no "jump if positive", "jump if unsigned >" or "jump if signed >". Use the reverse jump with swapped CMP operands.

## 8. Emulated instructions

These are not real opcodes. Assemblers accept them and encode the core instruction shown; most use the constant generators, so they take one word.

| Emulated | Encoded as | Operation |
|---|---|---|
| `ADC(.B) dst` | `ADDC(.B) #0, dst` | add carry |
| `BR src` | `MOV src, PC` | branch |
| `CLR(.B) dst` | `MOV(.B) #0, dst` | clear |
| `CLRC` | `BIC #1, SR` | clear C |
| `CLRN` | `BIC #4, SR` | clear N |
| `CLRZ` | `BIC #2, SR` | clear Z |
| `DADC(.B) dst` | `DADD(.B) #0, dst` | decimal add carry |
| `DEC(.B) dst` | `SUB(.B) #1, dst` | decrement |
| `DECD(.B) dst` | `SUB(.B) #2, dst` | decrement by 2 |
| `DINT` | `BIC #8, SR` | disable interrupts |
| `EINT` | `BIS #8, SR` | enable interrupts |
| `INC(.B) dst` | `ADD(.B) #1, dst` | increment |
| `INCD(.B) dst` | `ADD(.B) #2, dst` | increment by 2 |
| `INV(.B) dst` | `XOR(.B) #-1, dst` | invert bits |
| `NOP` | `MOV #0, R3` | no operation |
| `POP(.B) dst` | `MOV(.B) @SP+, dst` | pop |
| `RET` | `MOV @SP+, PC` | return from subroutine |
| `RLA(.B) dst` | `ADD(.B) dst, dst` | shift left; C = old MSB |
| `RLC(.B) dst` | `ADDC(.B) dst, dst` | rotate left through C |
| `SBC(.B) dst` | `SUBC(.B) #0, dst` | subtract borrow (dst − 1 + C) |
| `SETC` | `BIS #1, SR` | set C |
| `SETN` | `BIS #4, SR` | set N |
| `SETZ` | `BIS #2, SR` | set Z |
| `TST(.B) dst` | `CMP(.B) #0, dst` | test: N, Z set; C = 1; V = 0 |

## 9. Reset

- PC is loaded from the reset vector at 0xFFFE.
- SR is cleared, and R2–R15 are 0.
- The program sets SP. **(mspsim)** SP starts at 0x0400, the top of a G2553's RAM.
- **(mspsim)** If the reset vector is erased (0xFFFF), PC starts at the loader's entry address instead: the ELF entry point, the Intel HEX start address, or the `-b` address.

## 10. Interrupts

- **Vectors:** there are 16, at 0xFFE0 + 2N for N = 0…15. N = 15 (0xFFFE) is reset, and a higher N has higher priority.
- **Maskable requests** (N = 0…13) are accepted only while GIE = 1.
- **NMI** (N = 14, 0xFFFC) ignores GIE.
- **When:** requests are checked after every instruction, and after every idle step while the CPU is off.

**Entry:** 6 cycles.
1. SP −= 2; @SP = PC (the address of the next instruction).
2. SP −= 2; @SP = SR.
3. PC = the vector word.
4. SR = SR & SCG0: every bit except SCG0 is cleared, which disables interrupts and wakes the CPU.

**Return:** `RETI` restores SR and PC, re-enabling interrupts and the low-power mode that was active. To stay awake after the handler, clear CPUOFF in the saved SR on the stack, e.g. `bic #0x10, 0(sp)`.

**(mspsim)** Request lines are level-sensitive: a source holds its request until it deasserts it. The NMI is cleared when it is accepted. The only source is the UART receive interrupt (vector 7, when `UCA0RXIE` is set and a byte is waiting).

## 11. Low-power mode

- **Effect:** while CPUOFF is set, the CPU fetches nothing; mspsim counts 1 cycle per idle step. An accepted interrupt clears CPUOFF for the duration of the handler.
- **(mspsim) Ending the run:** the run ends when the CPU is off and nothing can wake it: GIE is clear, no interrupt source is enabled, or stdin is at end of file.

## 12. Cycle counts

Counts from SLAU144, which mspsim adds up in `cpu->cycles`. Constant-generator sources count as `Rn`.

### Format I

| Source \ Destination | Rm | PC | x(Rm), EDE, &EDE |
|---|---|---|---|
| Rn, constant | 1 | 2 | 4 |
| @Rn | 2 | 2 | 5 |
| @Rn+ | 2 | 3 | 5 |
| #N | 2 | 3 | 5 |
| x(Rn), EDE, &EDE | 3 | 3 | 6 |

### Format II

| Source | RRA, RRC, SWPB, SXT | PUSH | CALL |
|---|---|---|---|
| Rn, constant | 1 | 3 | 4 |
| @Rn | 3 | 4 | 4 |
| @Rn+ | 3 | 5 | 5 |
| #N | — | 4 | 5 |
| x(Rn), EDE, &EDE | 4 | 5 | 5 |

### Other

| Event | Cycles |
|---|---|
| Jump, taken or not | 2 |
| RETI | 5 |
| Interrupt entry | 6 |
| Idle step with CPUOFF (mspsim) | 1 |

## 13. Encoding examples

| Assembly | Words | Notes |
|---|---|---|
| `mov #0x5a80, &0x0120` | 40B2 5A80 0120 | immediate source, absolute destination |
| `mov r4, r5` | 4405 | |
| `add @r4+, 2(r5)` | 54B5 0002 | |
| `mov.b r4, 3(r5)` | 44C5 0003 | |
| `mov 0xc010, r5` at 0xC000 | 4015 000E | symbolic: 0xC010 − 0xC002 |
| `mov #1, r5` | 4315 | constant generator, R3 As = 01 |
| `mov.b #0xff, r5` | 4375 | constant −1 as a byte |
| `bis #8, sr` (`eint`) | D232 | constant generator, R2 As = 11 |
| `rra.b r5` | 1145 | |
| `push #0x1234` | 1230 1234 | |
| `call #0xc010` | 12B0 C010 | |
| `reti` | 1300 | |
| `jne $+4` | 2001 | skip the next word |
| `jmp $` | 3FFF | offset −1 |
| `nop` | 4303 | `mov #0, r3` |
| `ret` | 4130 | `mov @sp+, pc` |
