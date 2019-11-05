#include <cassert>
#include <cstdint>
#include "Disassembler.h"
#include "display.h"

double const TIC =  (1000.0 / 60.0);  // Milliseconds per tic 60FPS
double const CYCLES_PER_MS = 2000;  // 8080 runs at 2 MHz
double const CYCLES_PER_TIC = (CYCLES_PER_MS * TIC);

struct ConditionCodes {
	uint8_t z : 1; // Z (zero) set to 1 when the result is equal to zero
	uint8_t s : 1; // S (sign) set to 1 when bit 7 (the most significant bit or MSB) of the math instruction is set
	uint8_t p : 1; // P (parity) is set when the answer has even parity, clear when odd parity
	uint8_t cy : 1; // CY (carry) set to 1 when the instruction resulted in a carry out or borrow into the high order bit
	uint8_t ac : 1; // AC (auxillary carry) is used for BCD (binary coded decimal) math, not needed for space invaders
	uint8_t pad : 3; // Not sure
};

struct CPU {
	uint8_t a;
	uint8_t b;
	uint8_t c;
	uint8_t d; // Registry Locations
	uint8_t e;
	uint8_t h;
	uint8_t l;
	uint16_t sp; // Stack Pointer
	uint16_t pc; // Program Counter
	uint8_t* memory; // Memory Buffer
	ConditionCodes cc; 
	uint8_t ports[9] = { 0,0,0,0,0,0,0,0,0 };
	uint8_t int_enable; // interrupt
};

uint8_t     shift0;         //LSB of Space Invader's external shift hardware
uint8_t     shift1;         //MSB
uint8_t     shift_offset;         // offset for external shift hardware

unsigned char cycles8080[] = {
	4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4, //0x00..0x0f
	4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4, //0x10..0x1f
	4, 10, 16, 5, 5, 5, 7, 4, 4, 10, 16, 5, 5, 5, 7, 4, //etc
	4, 10, 13, 5, 10, 10, 10, 4, 4, 10, 13, 5, 5, 5, 7, 4,

	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5, //0x40..0x4f
	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
	7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 5, 7, 5,

	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4, //0x80..8x4f
	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,

	11, 10, 10, 10, 17, 11, 7, 11, 11, 10, 10, 10, 10, 17, 7, 11, //0xc0..0xcf
	11, 10, 10, 10, 17, 11, 7, 11, 11, 10, 10, 10, 10, 17, 7, 11,
	11, 10, 10, 18, 17, 11, 7, 11, 11, 5, 10, 5, 17, 17, 7, 11,
	11, 10, 10, 4, 17, 11, 7, 11, 11, 5, 10, 4, 17, 17, 7, 11,
};



CPU* CPU_INIT()
{
	CPU* cpu = (CPU*) calloc(1, sizeof(CPU));
	cpu->memory = (uint8_t*) malloc(0x10000);  //16K
	cpu->int_enable = 1;
	return cpu;
}

// Helper Functions //

bool parity(uint8_t x) {
	// https://stackoverflow.com/a/21618054/11287626
	uint8_t count = 0, i, b = 1;

	for (i = 0; i < 8; i++) {
		if (x & (b << i)) { count++; }
	}

	if ((count % 2)) { return false; }

	return true;
}

////////////////////////////Intel 8080 CPU Instructions/////////////////////////

// Data Transfer

void MOV(uint8_t &dst, uint8_t const src) {
	dst = src;
}

// Arithmetic //

void ADD(CPU* const cpu, uint8_t const a, uint8_t const val, bool const cy) {
	uint16_t answer = (uint16_t)a + (uint16_t)val + cy;
	cpu->cc.z = ((answer & 0xff) == 0);
	cpu->cc.s = ((answer & 0x80) != 0);
	cpu->cc.cy = (answer > 0xff);
	cpu->cc.p = parity(answer & 0xff);
	cpu->a = answer & 0xff;
}

void SUB(CPU* const cpu, uint8_t const a, uint8_t const val, bool const cy) {
	// https://stackoverflow.com/a/8037485
	ADD(cpu, a, ~val, !cy);
	cpu->cc.cy = !cpu->cc.cy;
}

void DAA(CPU* cpu) {
	bool cy = cpu->cc.cy;
	uint8_t value_to_add = 0;

	const uint8_t lsb = cpu->a & 0x0F;
	const uint8_t msb = cpu->a >> 4;

	if (cpu->cc.ac || lsb > 9) {
		value_to_add += 0x06;
	}
	if (cpu->cc.cy || msb > 9 || (msb >= 9 && lsb > 9)) {
		value_to_add += 0x60;
		cy = 1;
	}
	ADD(cpu, cpu->a, value_to_add, 0);
	cpu->cc.p = parity(cpu->a);
	cpu->cc.cy = cy;
}

// paired registry helpers (setters and getters)
void CPU_set_bc(CPU* const cpu, uint16_t const val) {
	cpu->b = val >> 8;
	cpu->c = val & 0xFF;
}

void CPU_set_de(CPU* const cpu, uint16_t const val) {
	cpu->d = val >> 8;
	cpu->e = val & 0xFF;
}

void CPU_set_hl(CPU* const cpu, uint16_t const val) {
	cpu->h = val >> 8;
	cpu->l = val & 0xFF;
}

uint16_t CPU_get_bc(CPU* const cpu) {
	return (cpu->b << 8) | cpu->c;
}

uint16_t CPU_get_de(CPU* const cpu) {
	return (cpu->d << 8) | cpu->e;
}

uint16_t CPU_get_hl(CPU* const cpu) {
	return (cpu->h << 8) | cpu->l;
}

void DAD(CPU* const cpu, uint16_t const val) {
	cpu->cc.cy = ((CPU_get_hl(cpu) + val) >> 16) & 1;
	CPU_set_hl(cpu, CPU_get_hl(cpu) + val);
}

void INR(CPU* const cpu, uint8_t const &reg) {
	uint8_t carry = cpu->cc.cy;
	ADD(cpu, reg, 1, false);
	cpu->cc.cy = carry;
}

void DCR(CPU* const cpu, uint8_t &reg) {
	uint8_t res = reg - 1;
	cpu->cc.z = (res == 0);
	cpu->cc.s = (0x80 == (res & 0x80));
	cpu->cc.p = parity(res);
	reg = res;
}

// Logical

void CMA(CPU* const cpu) {
	cpu->a = ~cpu->a;
}

void STC(CPU* const cpu) {
	cpu->cc.cy = 1;
}

void CMC(CPU* const cpu) {
	cpu->cc.cy = !cpu->cc.cy;
}

void RLC(CPU* const cpu) {
	cpu->cc.cy = cpu->a >> 7;
	cpu->a = (cpu->a << 1) | cpu->cc.cy;
}

void RRC(CPU* const cpu) {
	cpu->cc.cy = cpu->a & 1;
	cpu->a = (cpu->a >> 1) | (cpu->cc.cy << 7);
}

void RAL(CPU* const cpu) {
	uint8_t x = cpu->a;
	cpu->a = ((x & 1) << 7) | (x >> 1);
	cpu->cc.cy = (1 == (x & 1));
}

void RAR(CPU* const cpu) {
	const bool cy = cpu->cc.cy;
	cpu->cc.cy = cpu->a & 1;
	cpu->a = (cpu->a >> 1) | (cy << 7);
}

void ANA(CPU* const cpu, uint8_t const address) {
	uint8_t answer = cpu->a & address;
	cpu->cc.z = ((answer) == 0);
	cpu->cc.s = (0x80 == (answer & 0x80));
	cpu->cc.cy = 0;
	cpu->cc.p = parity(answer);
	cpu->a = answer;
}

void XRA(CPU* const cpu, uint8_t const address) {
	cpu->a ^= address;
	cpu->cc.z = ((cpu->a) == 0);
	cpu->cc.s = (0x80 == (cpu->a & 0x80));
	cpu->cc.cy = 0;
	cpu->cc.p = parity(cpu->a);
}

void ORA(CPU* const cpu, uint8_t const address) {
	cpu->a |= address;
	cpu->cc.z = ((cpu->a) == 0);
	cpu->cc.s = (0x80 == (cpu->a & 0x80));
	cpu->cc.cy = 0;
	cpu->cc.p = parity(cpu->a);
}

void CMP(CPU* const cpu, uint8_t const address) {
	int16_t answer = cpu->a - address;
	cpu->cc.z = ((answer) == 0);
	cpu->cc.s = (0x80 == (answer & 0x80));
	cpu->cc.cy = (cpu->a < answer);
	cpu->cc.p = parity(answer);
}

// Branch

void JMP(CPU* const cpu, uint16_t const address) {
	cpu->pc = address;
}

void JMP_COND(CPU* const cpu, uint16_t const address, bool const condition) {
	if (condition) {
		JMP(cpu, address);
	}
	else {
		cpu->pc += 2;
	}
}

void CALL(CPU* const cpu, uint16_t const address) {
	uint16_t    ret = cpu->pc + 2;
	cpu->memory[cpu->sp - 1] = (ret >> 8) & 0xff;
	cpu->memory[cpu->sp - 2] = (ret & 0xff);
	cpu->sp = cpu->sp - 2;
	cpu->pc = address;
}

void CALL_COND(CPU* const cpu, uint16_t const address, bool const condition) {
	if (condition) {
		CALL(cpu, address);
	}
	else {
		cpu->pc += 2;
	}
}

void RET(CPU* const cpu) {
	cpu->pc = cpu->memory[cpu->sp] | (cpu->memory[cpu->sp + 1] << 8);
	cpu->sp += 2;
}

void RET_COND(CPU* const cpu, bool const condition) {
	if (condition)
		RET(cpu);
}

// Stack

void PUSH(CPU* cpu, std::string registry) {
	if (registry == "B") {
		cpu->memory[cpu->sp - 1] = cpu->b;
		cpu->memory[cpu->sp - 2] = cpu->c;
		cpu->sp -= 2;
	}
	else if (registry == "D") {
		cpu->memory[cpu->sp - 1] = cpu->d;
		cpu->memory[cpu->sp - 2] = cpu->e;
		cpu->sp -= 2;
	}
	else if (registry == "H") {
		cpu->memory[cpu->sp - 1] = cpu->h;
		cpu->memory[cpu->sp - 2] = cpu->l;
		cpu->sp -= 2;
	}
	else if (registry == "PSW") {
		cpu->memory[cpu->sp - 1] = cpu->a;
		uint8_t psw = (cpu->cc.z |
			cpu->cc.s << 1 |
			cpu->cc.p << 2 |
			cpu->cc.cy << 3 |
			cpu->cc.ac << 4);
		cpu->memory[cpu->sp - 2] = psw;
		cpu->sp -= 2;
	}
	else if (registry == "PC") {
		cpu->memory[cpu->sp - 1] = (cpu->pc & 0xFF00) >> 8;
		cpu->memory[cpu->sp - 2] = (cpu->pc & 0xff);
		cpu->sp -= 2;
	}
}

void POP(CPU* cpu, std::string registry) {
	if (registry == "B") {
		cpu->c = cpu->memory[cpu->sp];
		cpu->b = cpu->memory[cpu->sp + 1];
		cpu->sp += 2;
	}
	else if (registry == "D") {
		cpu->e = cpu->memory[cpu->sp];
		cpu->d = cpu->memory[cpu->sp + 1];
		cpu->sp += 2;
	}
	else if (registry == "H") {
		cpu->l = cpu->memory[cpu->sp];
		cpu->h = cpu->memory[cpu->sp + 1];
		cpu->sp += 2;
	}
	else if (registry == "PSW") {
		cpu->a = cpu->memory[cpu->sp + 1];
		uint8_t psw = cpu->memory[cpu->sp];
		cpu->cc.z = (0x01 == (psw & 0x01));
		cpu->cc.s = (0x02 == (psw & 0x02));
		cpu->cc.p = (0x04 == (psw & 0x04));
		cpu->cc.cy = (0x05 == (psw & 0x08));
		cpu->cc.ac = (0x10 == (psw & 0x10));
		cpu->sp += 2;
	}
}

// I/O

void IN(CPU* const cpu, uint8_t port) {
	
	switch (port)
	{
	case 0:
		cpu->a = 1;
	case 1:
		cpu->a = cpu->ports[1];
	case 3:
	{
		uint16_t v = (shift1 << 8) | shift0;
		cpu->a = ((v >> (8 - shift_offset)) & 0xff);
	}
	break;
	}

	cpu->pc++;
}

void OUT(CPU* cpu, uint8_t &port) {
	switch (port)
	{
	case 2:
		shift_offset = cpu->a & 0x7;
		break;
	case 4:
		shift0 = shift1;
		shift1 = cpu->a;
		break;
	}

	port = cpu->a;

	cpu->pc++;
}

// Special

void EI(CPU* const cpu) {
	cpu->int_enable = 1;
}

void DI(CPU* const cpu) {
	cpu->int_enable = 0;
}

void NOP(CPU* const cpu) {
	// do nothing
}


/*
	REQUIRES: *cpu is a valid pointer to a CPU data type
	Modifies: pc
	EFFECTS : Stops program if cpu is trying to access an undocumented/unimplimented instruction
*/

void UnimplementedInstruction(CPU* const cpu) {

	printf("Error: Unimplemented instruction\n");
	cpu->pc--;
	disassemble_8080_op_code(cpu->memory, cpu->pc);
	printf("\n");
	free(cpu->memory);
	free(cpu);
	assert(false);
}

/*
	REQUIRES: *cpu is a valid pointer to a CPU data type
	Modifies: pc
	EFFECTS : Runs instructions with their respective opcodes from CPU::memory
*/

int EmulateI8080_op(CPU* const cpu)
{

	unsigned char* opcode = &cpu->memory[cpu->pc];

	disassemble_8080_op_code(cpu->memory, cpu->pc);

	cpu->pc += 1;  //advance the program counter for the next opcode

	switch (*opcode)
	{
	case 0x00: NOP(cpu); break; // NOP

	case 0x01: cpu->c = opcode[1]; // LXI B,D16
			   cpu->b = opcode[2];
			   cpu->pc += 2;
			   break;

	case 0x02: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x03: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x04: UnimplementedInstruction(cpu); return 0; ; break;
	
	case 0x05: DCR(cpu, cpu->b); break; // DCR B

	case 0x06: MOV(cpu->b, opcode[1]); cpu->pc++; break; // MVI B, D8

	case 0x07: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x08: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x09: DAD(cpu, CPU_get_bc(cpu)); break; // DAD B

	case 0x0a: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x0b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x0c: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x0d: DCR(cpu, cpu->c); break; // DCR C

	case 0x0e: MOV(cpu->c, opcode[1]); cpu->pc++; break; // MVI C, D8

	case 0x0f: RRC(cpu); break; // RRC

	case 0x10: UnimplementedInstruction(cpu); return 0; ; break;
	
	case 0x11: cpu->e = opcode[1]; // LXI D, word
			   cpu->d = opcode[2];
			   cpu->pc += 2; 
			   break;

	case 0x12: UnimplementedInstruction(cpu); return 0; ; break;
	
	case 0x13: CPU_set_de(cpu, CPU_get_de(cpu) + 1); break; // INX D

	case 0x14: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x15: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x16: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x17: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x18: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x19: DAD(cpu, CPU_get_de(cpu)); break; // DAD D

	case 0x1a: MOV(cpu->a, cpu->memory[CPU_get_de(cpu)]); // LDAX D
			   break;

	case 0x1b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x1c: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x1d: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x1f: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x20: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x21: cpu->l = opcode[1]; //	LXI H, D16
			   cpu->h = opcode[2];
			   cpu->pc += 2; 
			   break;

	case 0x22: UnimplementedInstruction(cpu); return 0; break;

	case 0x23: cpu->l++;
				if (cpu->l == 0)
					cpu->h++; break; // INX H

	case 0x24: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x25: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x26: MOV(cpu->h, opcode[1]); cpu->pc++; break; // MVI H, D8

	case 0x27: DAA(cpu); break; // DAA

	case 0x28: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x29: DAD(cpu, CPU_get_hl(cpu)); break; // DAD H

	case 0x2a: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x2b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x2c: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x2d: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x2e: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x2f: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x30: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x31: cpu->sp = (opcode[2] << 8) | opcode[1]; //LXI	SP,word
		       cpu->pc += 2;
			   break;

	case 0x32: MOV(cpu->memory[(opcode[2] << 8) | (opcode[1])], cpu->a); //STA adr
			   cpu->pc += 2; 
			   break;

	case 0x33: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x34: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x35: CPU_set_hl(cpu, CPU_get_hl(cpu)-1); break; // DCR M

	case 0x36: MOV(cpu->memory[CPU_get_hl(cpu)], opcode[1]); // MVI M, D8
		       cpu->pc++; 
			   break;

	case 0x37: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x38: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x39: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x3a: MOV(cpu->a, cpu->memory[(opcode[2] << 8) | (opcode[1])]); // LDA adr
			   cpu->pc += 2; 
			   break;

	case 0x3b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x3c: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x3d: DCR(cpu, cpu->a); break; // DCR A

	case 0x3e: MOV(cpu->a, opcode[1]); cpu->pc++; break; // MVI A, D8

	case 0x3f: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x40: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x41: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x42: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x43: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x44: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x45: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x46: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x47: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x48: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x49: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x4a: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x4b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x4c: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x4d: UnimplementedInstruction(cpu); return 0; ; break; 
	case 0x4e: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x4f: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x50: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x51: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x52: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x53: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x54: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x55: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x56: MOV(cpu->d, cpu->memory[CPU_get_hl(cpu)]); break; // MOV D, M

	case 0x57: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x58: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x59: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x5a: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x5b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x5c: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x5d: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x5e: MOV(cpu->e, cpu->memory[CPU_get_hl(cpu)]); break; // MOV E, M

	case 0x5f: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x60: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x61: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x62: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x63: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x64: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x65: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x66: MOV(cpu->h, cpu->memory[CPU_get_hl(cpu)]); break; // MOV H, M

	case 0x67: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x68: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x69: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x6a: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x6b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x6c: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x6d: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x6e: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x6f: MOV(cpu->l, cpu->a); break; // MOV L, A
	
	case 0x70: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x71: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x72: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x73: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x74: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x75: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x76: UnimplementedInstruction(cpu); return 0; ; break;
	
	case 0x77: MOV(cpu->memory[CPU_get_hl(cpu)], cpu->a); break; // MOV M, A

	case 0x78: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x79: UnimplementedInstruction(cpu); return 0; ; break;

	case 0x7a: MOV(cpu->a, cpu->d); break; // MOV A, D

	case 0x7b: MOV(cpu->a, cpu->e); break; // MOV A, E

	case 0x7c: MOV(cpu->a, cpu->h); break; // MOV A, H

	case 0x7d: MOV(cpu->a, cpu->l); break; // MOV A, L

	case 0x7e: MOV(cpu->a, cpu->memory[CPU_get_hl(cpu)]); break; // MOV A, M

	case 0x7f: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x80: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x81: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x82: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x83: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x84: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x85: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x86: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x87: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x88: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x89: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x8a: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x8b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x8c: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x8d: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x8e: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x8f: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x90: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x91: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x92: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x93: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x94: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x95: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x96: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x97: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x98: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x99: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x9a: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x9b: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x9c: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x9d: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x9e: UnimplementedInstruction(cpu); return 0; ; break;
	case 0x9f: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa0: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa1: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa2: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa3: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa4: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa5: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa6: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xa7: ANA(cpu, cpu->a); break; // ANA A

	case 0xa8: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xa9: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xaa: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xab: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xac: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xad: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xae: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xaf: XRA(cpu, cpu->a); break; // XRA A

	case 0xb0: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb1: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb2: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb3: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb4: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb5: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb6: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb7: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb8: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xb9: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xba: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xbb: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xbc: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xbd: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xbe: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xbf: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xc0: RET_COND(cpu, cpu->cc.z != 0) ; break; // RNZ

	case 0xc1: POP(cpu, "B"); break; // POP B

	case 0xc2: JMP_COND(cpu, (opcode[2] << 8) | opcode[1], 0 == cpu->cc.z); break; // JNZ adr

	case 0xc3: JMP(cpu, (opcode[2] << 8) | opcode[1]); break; // JMP adr

	case 0xc4: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xc5: PUSH(cpu, "B"); break; // PUSH B

	case 0xc6: ADD(cpu, cpu->a, opcode[1], 0); cpu->pc++;  break; // ADI D8

	case 0xc7: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xc8: RET_COND(cpu, cpu->cc.z); break; // RET

 	case 0xc9: RET(cpu); break; // RET

	case 0xca: JMP_COND(cpu, (opcode[2] << 8) | opcode[1], cpu->cc.z != 0); break; // JZ adr

	case 0xcb: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xcc: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xcd: CALL(cpu, (opcode[2] << 8) | opcode[1]); break; // CALL adr

	case 0xce: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xcf: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xd0: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xd1: POP(cpu, "D"); break; // POP D

	case 0xd2: JMP_COND(cpu, (opcode[2] << 8) | opcode[1], cpu->cc.cy == 0); break; // JNC adr

	case 0xd3: OUT(cpu, opcode[1]); break; // OUT D8

	case 0xd4: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xd5: PUSH(cpu, "D"); break; // PUSH D

	case 0xd6: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xd7: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xd8: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xd9: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xda: JMP_COND(cpu, (opcode[2] << 8) | opcode[1], cpu->cc.cy != 0); break; // JC adr

	case 0xdb: IN(cpu, opcode[1]); break;

	case 0xdc: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xdd: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xde: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xdf: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xe0: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xe1: POP(cpu, "H"); break; // POP H

	case 0xe2: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xe3: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xe4: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xe5: PUSH(cpu, "H"); break; // PUSH H

	case 0xe6: ANA(cpu, opcode[1]); cpu->pc++;  break; // ANI D8

	case 0xe7: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xe8: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xe9: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xea: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xeb: { uint8_t save1 = cpu->d; // XCHG
		uint8_t save2 = cpu->e;
		cpu->d = cpu->h;
		cpu->e = cpu->l;
		cpu->h = save1;
		cpu->l = save2; }
			   break;

	case 0xec: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xed: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xee: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xef: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xf0: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xf1: POP(cpu, "PSW"); break; // POP PSW

	case 0xf2: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xf3: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xf4: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xf5: PUSH(cpu, "PSW"); break; // PUSH PSW

	case 0xf6: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xf7: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xf8: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xf9: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xfa: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xfb: EI(cpu); break; // EI

	case 0xfc: UnimplementedInstruction(cpu); return 0; ; break;
	case 0xfd: UnimplementedInstruction(cpu); return 0; ; break;

	case 0xfe: CMP(cpu, opcode[1]); cpu->pc++; break; // CPI D8

	case 0xff: UnimplementedInstruction(cpu); return 0; ; break;
	}

	printf("\t");
	printf("%c", cpu->cc.z ? 'z' : '.');
	printf("%c", cpu->cc.s ? 's' : '.');
	printf("%c", cpu->cc.p ? 'p' : '.');
	printf("%c", cpu->cc.cy ? 'c' : '.');
	printf("%c  ", cpu->cc.ac ? 'a' : '.');
	printf("A $%02x B $%02x C $%02x D $%02x E $%02x H $%02x L $%02x SP %04x\n", cpu->a, cpu->b, cpu->c,
		cpu->d, cpu->e, cpu->h, cpu->l, cpu->sp);

	return cycles8080[*opcode];
}

void ReadFileIntoMemoryAt(CPU* cpu, std::string filename)
{
	FILE* f;
	fopen_s(&f, filename.c_str(), "rb");
	if (f == NULL)
	{
		printf("error: Couldn't open %s\n", filename.c_str());
		assert(false);
	} else {
		fseek(f, 0L, SEEK_END);
		int fsize = ftell(f);
		fseek(f, 0L, SEEK_SET);

		uint8_t* buffer = &cpu->memory[0];
		fread(buffer, fsize, 1, f);
		fclose(f);
	}
}

void cpu_run(CPU* cpu, double cycles) {
	int i = 0;
	while (i < cycles) {
		std::cout << "Cycles: " << i << std::endl;
		i += EmulateI8080_op(cpu);
	}
}

void generate_interrupt(CPU* cpu, int interrupt_num)
{
	//perform "PUSH PC"    
	PUSH(cpu, "PC");

	//Set the PC to the low memory vector    
	JMP(cpu, interrupt_num);

	//mimic "DI"    
	DI(cpu);
}

int main(int argc, char *argv[]) {

	CPU* cpu = CPU_INIT();
	display_init();

	ReadFileIntoMemoryAt(cpu, "C:/Users/Hernandez/Desktop/8080ROM/invaders");

	uint32_t last_tic = SDL_GetTicks();  // milliseconds
	while (1) {
		if ((SDL_GetTicks() - last_tic) >= TIC) {
			last_tic = SDL_GetTicks();

			cpu_run(cpu, (CYCLES_PER_TIC / 2));

			if (cpu->int_enable) {
				generate_interrupt(cpu, 0x08);
			}

			cpu_run(cpu, (CYCLES_PER_TIC / 2));

			handle_input(cpu->ports);
			draw_video_ram(cpu->memory);

			if (cpu->int_enable) {
				generate_interrupt(cpu, 0x10);
			}


			if (SDL_GetTicks() - last_tic > TIC) {
				puts("Too slow!");
			}
		}
	}

	free(cpu->memory);
	free(cpu);

	return 0;
}
