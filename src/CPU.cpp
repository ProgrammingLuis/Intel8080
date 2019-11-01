#include <cstdint>
#include <cassert>
#include <iostream>

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
	uint8_t int_enable; 
};

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

// Arithmetic //

void ADD(CPU* cpu, uint8_t a, uint8_t val, bool cy) {
	uint16_t answer = (uint16_t)a + (uint16_t)val + cy;
	cpu->cc.z = ((answer & 0xff) == 0);
	cpu->cc.s = ((answer & 0x80) != 0);
	cpu->cc.cy = (answer > 0xff);
	cpu->cc.p = parity(answer & 0xff);
	cpu->a = answer & 0xff;
}

void SUB(CPU* cpu, uint8_t a, uint8_t val, bool cy) {
	// https://stackoverflow.com/a/8037485
	ADD(cpu, a, ~val, !cy);
	cpu->cc.cy = !cpu->cc.cy;
}

// paired registry helpers (setters and getters)
void CPU_set_bc(CPU* cpu, uint16_t val) {
	cpu->b = val >> 8;
	cpu->c = val & 0xFF;
}

void CPU_set_de(CPU* cpu, uint16_t val) {
	cpu->d = val >> 8;
	cpu->e = val & 0xFF;
}

void CPU_set_hl(CPU* cpu, uint16_t val) {
	cpu->h = val >> 8;
	cpu->l = val & 0xFF;
}

uint16_t CPU_get_bc(CPU* cpu) {
	return (cpu->b << 8) | cpu->c;
}

uint16_t CPU_get_de(CPU* cpu) {
	return (cpu->d << 8) | cpu->e;
}

uint16_t CPU_get_hl(CPU* cpu) {
	return (cpu->h << 8) | cpu->l;
}

void DAD(CPU* cpu, uint16_t val) {
	cpu->cc.cy = ((CPU_get_hl(cpu) + val) >> 16) & 1;
	CPU_set_hl(cpu, CPU_get_hl(cpu) + val);
}

void INR(CPU* cpu, uint8_t &reg) {
	uint8_t carry = cpu->cc.cy;
	ADD(cpu, reg, 1, false);
	cpu->cc.cy = carry;
}

void DCR(CPU* cpu, uint8_t& reg) {
	uint8_t carry = cpu->cc.cy;
	SUB(cpu, reg, 1, false);
	cpu->cc.cy = carry;
}

// Data Transfer

// Logical

void CMA(CPU* cpu) {
	cpu->a = ~cpu->a;
}

void STC(CPU* cpu) {
	cpu->cc.cy = 1;
}

void CMC(CPU* cpu) {
	cpu->cc.cy = !cpu->cc.cy;
}

void RLC(CPU* cpu) {
	cpu->cc.cy = cpu->a >> 7;
	cpu->a = (cpu->a << 1) | cpu->cc.cy;
}

void RRC(CPU* cpu) {
	cpu->cc.cy = cpu->a & 1;
	cpu->a = (cpu->a >> 1) | (cpu->cc.cy << 7);
}

void RAL(CPU* cpu) {
	const bool cy = cpu->cc.cy;
	cpu->cc.cy = cpu->a >> 7;
	cpu->a = (cpu->a << 1) | cy;
}

void RAR(CPU* cpu) {
	const bool cy = cpu->cc.cy;
	cpu->cc.cy = cpu->a & 1;
	cpu->a = (cpu->a >> 1) | (cy << 7);
}

void ANA(CPU* cpu, uint8_t address) {
	uint8_t answer = cpu->a & address;
	cpu->cc.z = ((answer) == 0);
	cpu->cc.s = (0x80 == (answer & 0x80));
	cpu->cc.cy = 0;
	cpu->cc.p = parity(answer);
	cpu->a = answer;
}

void XRA(CPU* cpu, uint8_t address) {
	cpu->a ^= address;
	cpu->cc.z = ((cpu->a) == 0);
	cpu->cc.s = (0x80 == (cpu->a & 0x80));
	cpu->cc.cy = 0;
	cpu->cc.p = parity(cpu->a);
}

void ORA(CPU* cpu, uint8_t address) {
	cpu->a |= address;
	cpu->cc.z = ((cpu->a) == 0);
	cpu->cc.s = (0x80 == (cpu->a & 0x80));
	cpu->cc.cy = 0;
	cpu->cc.p = parity(cpu->a);
}

void CMP(CPU* cpu, uint8_t address) {
	int16_t answer = cpu->a - address;
	cpu->cc.z = ((answer & 0xFF) == 0);
	cpu->cc.s = (0x80 == (answer & 0xFF));
	cpu->cc.cy = answer >> 8;
	cpu->cc.p = parity(answer & 0xFF);
	cpu->a = answer;

}

// Branch

void JMP(CPU* cpu, uint16_t address) {
	cpu->pc = address;
}

void JMP_COND(CPU* cpu, uint16_t address, bool condition) {
	if (condition)
		JMP(cpu, address);
	else
		cpu->pc += 2;
}

void CALL(CPU* cpu, uint16_t address) {
	uint16_t    ret = cpu->pc + 2;
	cpu->memory[cpu->sp - 1] = (ret >> 8) & 0xff;
	cpu->memory[cpu->sp - 2] = (ret & 0xff);
	cpu->sp = cpu->sp - 2;
	cpu->pc = address;
}

void CALL_COND(CPU* cpu, uint16_t address, bool condition) {
	if (condition) {
		CALL(cpu, address);
	}
	else
		cpu->pc += 2;
}

void RET(CPU* cpu) {
	cpu->pc = cpu->memory[cpu->sp] | (cpu->memory[cpu->sp + 1] << 8);
	cpu->sp += 2;
}

void RET_COND(CPU* cpu, bool condition) {
	if (condition)
		RET(cpu);
}

// Stack

// I/O

// Special


/*
	REQUIRES: *cpu is a valid pointer to a CPU data type
	Modifies: pc
	EFFECTS : Stops program if cpu is trying to access an undocumented/unimplimented instruction
*/

void UndocumentedInstruction(CPU* cpu) {

	std::cout << "Error: Undocumented instruction at" << &(cpu->memory[cpu->pc]) << "\n";
	cpu->pc--;
	assert(false);
}

/*
	REQUIRES: *cpu is a valid pointer to a CPU data type
	Modifies: pc
	EFFECTS : Runs instructions with their respective opcodes from CPU::memory
*/

void EmulateI8080_op(CPU* cpu)
{
	unsigned char* opcode = &cpu->memory[cpu->pc];

	switch (*opcode)
	{
	case 0x00: UndocumentedInstruction(cpu); break;
	case 0x01: UndocumentedInstruction(cpu); break;
	case 0x02: UndocumentedInstruction(cpu); break;
	case 0x03: UndocumentedInstruction(cpu); break;
	case 0x04: UndocumentedInstruction(cpu); break;
	case 0x05: UndocumentedInstruction(cpu); break;
	case 0x06: UndocumentedInstruction(cpu); break;
	case 0x07: UndocumentedInstruction(cpu); break;
	case 0x08: UndocumentedInstruction(cpu); break;
	case 0x09: UndocumentedInstruction(cpu); break;
	case 0x0a: UndocumentedInstruction(cpu); break;
	case 0x0b: UndocumentedInstruction(cpu); break;
	case 0x0c: UndocumentedInstruction(cpu); break;
	case 0x0d: UndocumentedInstruction(cpu); break;
	case 0x0e: UndocumentedInstruction(cpu); break;
	case 0x0f: UndocumentedInstruction(cpu); break;

	case 0x10: UndocumentedInstruction(cpu); break;
	case 0x11: UndocumentedInstruction(cpu); break;
	case 0x12: UndocumentedInstruction(cpu); break;
	case 0x13: UndocumentedInstruction(cpu); break;
	case 0x14: UndocumentedInstruction(cpu); break;
	case 0x15: UndocumentedInstruction(cpu); break;
	case 0x16: UndocumentedInstruction(cpu); break;
	case 0x17: UndocumentedInstruction(cpu); break;
	case 0x18: UndocumentedInstruction(cpu); break;
	case 0x19: UndocumentedInstruction(cpu); break;
	case 0x1a: UndocumentedInstruction(cpu); break;
	case 0x1b: UndocumentedInstruction(cpu); break;
	case 0x1c: UndocumentedInstruction(cpu); break;
	case 0x1d: UndocumentedInstruction(cpu); break;
	case 0x1f: UndocumentedInstruction(cpu); break;

	case 0x20: UndocumentedInstruction(cpu); break;
	case 0x21: UndocumentedInstruction(cpu); break;
	case 0x22: UndocumentedInstruction(cpu); break;
	case 0x23: UndocumentedInstruction(cpu); break;
	case 0x24: UndocumentedInstruction(cpu); break;
	case 0x25: UndocumentedInstruction(cpu); break;
	case 0x26: UndocumentedInstruction(cpu); break;
	case 0x27: UndocumentedInstruction(cpu); break;
	case 0x28: UndocumentedInstruction(cpu); break;
	case 0x29: UndocumentedInstruction(cpu); break;
	case 0x2a: UndocumentedInstruction(cpu); break;
	case 0x2b: UndocumentedInstruction(cpu); break;
	case 0x2c: UndocumentedInstruction(cpu); break;
	case 0x2d: UndocumentedInstruction(cpu); break;
	case 0x2e: UndocumentedInstruction(cpu); break;
	case 0x2f: UndocumentedInstruction(cpu); break;

	case 0x30: UndocumentedInstruction(cpu); break;
	case 0x31: UndocumentedInstruction(cpu); break;
	case 0x32: UndocumentedInstruction(cpu); break;
	case 0x33: UndocumentedInstruction(cpu); break;
	case 0x34: UndocumentedInstruction(cpu); break;
	case 0x35: UndocumentedInstruction(cpu); break;
	case 0x36: UndocumentedInstruction(cpu); break;
	case 0x37: UndocumentedInstruction(cpu); break;
	case 0x38: UndocumentedInstruction(cpu); break;
	case 0x39: UndocumentedInstruction(cpu); break;
	case 0x3a: UndocumentedInstruction(cpu); break;
	case 0x3b: UndocumentedInstruction(cpu); break;
	case 0x3c: UndocumentedInstruction(cpu); break;
	case 0x3d: UndocumentedInstruction(cpu); break;
	case 0x3e: UndocumentedInstruction(cpu); break;
	case 0x3f: UndocumentedInstruction(cpu); break;

	case 0x40: UndocumentedInstruction(cpu); break;
	case 0x41: UndocumentedInstruction(cpu); break;
	case 0x42: UndocumentedInstruction(cpu); break;
	case 0x43: UndocumentedInstruction(cpu); break;
	case 0x44: UndocumentedInstruction(cpu); break;
	case 0x45: UndocumentedInstruction(cpu); break;
	case 0x46: UndocumentedInstruction(cpu); break;
	case 0x47: UndocumentedInstruction(cpu); break;
	case 0x48: UndocumentedInstruction(cpu); break;
	case 0x49: UndocumentedInstruction(cpu); break;
	case 0x4a: UndocumentedInstruction(cpu); break;
	case 0x4b: UndocumentedInstruction(cpu); break;
	case 0x4c: UndocumentedInstruction(cpu); break;
	case 0x4d: UndocumentedInstruction(cpu); break; 
	case 0x4e: UndocumentedInstruction(cpu); break;
	case 0x4f: UndocumentedInstruction(cpu); break;

	case 0x50: UndocumentedInstruction(cpu); break;
	case 0x51: UndocumentedInstruction(cpu); break;
	case 0x52: UndocumentedInstruction(cpu); break;
	case 0x53: UndocumentedInstruction(cpu); break;
	case 0x54: UndocumentedInstruction(cpu); break;
	case 0x55: UndocumentedInstruction(cpu); break;
	case 0x56: UndocumentedInstruction(cpu); break;
	case 0x57: UndocumentedInstruction(cpu); break;
	case 0x58: UndocumentedInstruction(cpu); break;
	case 0x59: UndocumentedInstruction(cpu); break;
	case 0x5a: UndocumentedInstruction(cpu); break;
	case 0x5b: UndocumentedInstruction(cpu); break;
	case 0x5c: UndocumentedInstruction(cpu); break;
	case 0x5d: UndocumentedInstruction(cpu); break;
	case 0x5e: UndocumentedInstruction(cpu); break;
	case 0x5f: UndocumentedInstruction(cpu); break;

	case 0x60: UndocumentedInstruction(cpu); break;
	case 0x61: UndocumentedInstruction(cpu); break;
	case 0x62: UndocumentedInstruction(cpu); break;
	case 0x63: UndocumentedInstruction(cpu); break;
	case 0x64: UndocumentedInstruction(cpu); break;
	case 0x65: UndocumentedInstruction(cpu); break;
	case 0x66: UndocumentedInstruction(cpu); break;
	case 0x67: UndocumentedInstruction(cpu); break;
	case 0x68: UndocumentedInstruction(cpu); break;
	case 0x69: UndocumentedInstruction(cpu); break;
	case 0x6a: UndocumentedInstruction(cpu); break;
	case 0x6b: UndocumentedInstruction(cpu); break;
	case 0x6c: UndocumentedInstruction(cpu); break;
	case 0x6d: UndocumentedInstruction(cpu); break;
	case 0x6e: UndocumentedInstruction(cpu); break;
	case 0x6f: UndocumentedInstruction(cpu); break;

	case 0x70: UndocumentedInstruction(cpu); break;
	case 0x71: UndocumentedInstruction(cpu); break;
	case 0x72: UndocumentedInstruction(cpu); break;
	case 0x73: UndocumentedInstruction(cpu); break;
	case 0x74: UndocumentedInstruction(cpu); break;
	case 0x75: UndocumentedInstruction(cpu); break;
	case 0x76: UndocumentedInstruction(cpu); break;
	case 0x77: UndocumentedInstruction(cpu); break;
	case 0x78: UndocumentedInstruction(cpu); break;
	case 0x79: UndocumentedInstruction(cpu); break;
	case 0x7a: UndocumentedInstruction(cpu); break;
	case 0x7b: UndocumentedInstruction(cpu); break;
	case 0x7c: UndocumentedInstruction(cpu); break;
	case 0x7d: UndocumentedInstruction(cpu); break;
	case 0x7e: UndocumentedInstruction(cpu); break;
	case 0x7f: UndocumentedInstruction(cpu); break;

	case 0x80: UndocumentedInstruction(cpu); break;
	case 0x81: UndocumentedInstruction(cpu); break;
	case 0x82: UndocumentedInstruction(cpu); break;
	case 0x83: UndocumentedInstruction(cpu); break;
	case 0x84: UndocumentedInstruction(cpu); break;
	case 0x85: UndocumentedInstruction(cpu); break;
	case 0x86: UndocumentedInstruction(cpu); break;
	case 0x87: UndocumentedInstruction(cpu); break;
	case 0x88: UndocumentedInstruction(cpu); break;
	case 0x89: UndocumentedInstruction(cpu); break;
	case 0x8a: UndocumentedInstruction(cpu); break;
	case 0x8b: UndocumentedInstruction(cpu); break;
	case 0x8c: UndocumentedInstruction(cpu); break;
	case 0x8d: UndocumentedInstruction(cpu); break;
	case 0x8e: UndocumentedInstruction(cpu); break;
	case 0x8f: UndocumentedInstruction(cpu); break;

	case 0x90: UndocumentedInstruction(cpu); break;
	case 0x91: UndocumentedInstruction(cpu); break;
	case 0x92: UndocumentedInstruction(cpu); break;
	case 0x93: UndocumentedInstruction(cpu); break;
	case 0x94: UndocumentedInstruction(cpu); break;
	case 0x95: UndocumentedInstruction(cpu); break;
	case 0x96: UndocumentedInstruction(cpu); break;
	case 0x97: UndocumentedInstruction(cpu); break;
	case 0x98: UndocumentedInstruction(cpu); break;
	case 0x99: UndocumentedInstruction(cpu); break;
	case 0x9a: UndocumentedInstruction(cpu); break;
	case 0x9b: UndocumentedInstruction(cpu); break;
	case 0x9c: UndocumentedInstruction(cpu); break;
	case 0x9d: UndocumentedInstruction(cpu); break;
	case 0x9e: UndocumentedInstruction(cpu); break;
	case 0x9f: UndocumentedInstruction(cpu); break;

	case 0xa0: UndocumentedInstruction(cpu); break;
	case 0xa1: UndocumentedInstruction(cpu); break;
	case 0xa2: UndocumentedInstruction(cpu); break;
	case 0xa3: UndocumentedInstruction(cpu); break;
	case 0xa4: UndocumentedInstruction(cpu); break;
	case 0xa5: UndocumentedInstruction(cpu); break;
	case 0xa6: UndocumentedInstruction(cpu); break;
	case 0xa7: UndocumentedInstruction(cpu); break;
	case 0xa8: UndocumentedInstruction(cpu); break;
	case 0xa9: UndocumentedInstruction(cpu); break;
	case 0xaa: UndocumentedInstruction(cpu); break;
	case 0xab: UndocumentedInstruction(cpu); break;
	case 0xac: UndocumentedInstruction(cpu); break;
	case 0xad: UndocumentedInstruction(cpu); break;
	case 0xae: UndocumentedInstruction(cpu); break;
	case 0xaf: UndocumentedInstruction(cpu); break;

	case 0xb0: UndocumentedInstruction(cpu); break;
	case 0xb1: UndocumentedInstruction(cpu); break;
	case 0xb2: UndocumentedInstruction(cpu); break;
	case 0xb3: UndocumentedInstruction(cpu); break;
	case 0xb4: UndocumentedInstruction(cpu); break;
	case 0xb5: UndocumentedInstruction(cpu); break;
	case 0xb6: UndocumentedInstruction(cpu); break;
	case 0xb7: UndocumentedInstruction(cpu); break;
	case 0xb8: UndocumentedInstruction(cpu); break;
	case 0xb9: UndocumentedInstruction(cpu); break;
	case 0xba: UndocumentedInstruction(cpu); break;
	case 0xbb: UndocumentedInstruction(cpu); break;
	case 0xbc: UndocumentedInstruction(cpu); break;
	case 0xbd: UndocumentedInstruction(cpu); break;
	case 0xbe: UndocumentedInstruction(cpu); break;
	case 0xbf: UndocumentedInstruction(cpu); break;

	case 0xc0: UndocumentedInstruction(cpu); break;
	case 0xc1: UndocumentedInstruction(cpu); break;
	case 0xc2: UndocumentedInstruction(cpu); break;
	case 0xc3: UndocumentedInstruction(cpu); break;
	case 0xc4: UndocumentedInstruction(cpu); break;
	case 0xc5: UndocumentedInstruction(cpu); break;
	case 0xc6: UndocumentedInstruction(cpu); break;
	case 0xc7: UndocumentedInstruction(cpu); break;
	case 0xc8: UndocumentedInstruction(cpu); break;
	case 0xc9: UndocumentedInstruction(cpu); break;
	case 0xca: UndocumentedInstruction(cpu); break;
	case 0xcb: UndocumentedInstruction(cpu); break;
	case 0xcc: UndocumentedInstruction(cpu); break;
	case 0xcd: UndocumentedInstruction(cpu); break;
	case 0xce: UndocumentedInstruction(cpu); break;
	case 0xcf: UndocumentedInstruction(cpu); break;

	case 0xd0: UndocumentedInstruction(cpu); break;
	case 0xd1: UndocumentedInstruction(cpu); break;
	case 0xd2: UndocumentedInstruction(cpu); break;
	case 0xd3: UndocumentedInstruction(cpu); break;
	case 0xd4: UndocumentedInstruction(cpu); break;
	case 0xd5: UndocumentedInstruction(cpu); break;
	case 0xd6: UndocumentedInstruction(cpu); break;
	case 0xd7: UndocumentedInstruction(cpu); break;
	case 0xd8: UndocumentedInstruction(cpu); break;
	case 0xd9: UndocumentedInstruction(cpu); break;
	case 0xda: UndocumentedInstruction(cpu); break;
	case 0xdb: UndocumentedInstruction(cpu); break;
	case 0xdc: UndocumentedInstruction(cpu); break;
	case 0xdd: UndocumentedInstruction(cpu); break;
	case 0xde: UndocumentedInstruction(cpu); break;
	case 0xdf: UndocumentedInstruction(cpu); break;

	case 0xe0: UndocumentedInstruction(cpu); break;
	case 0xe1: UndocumentedInstruction(cpu); break;
	case 0xe2: UndocumentedInstruction(cpu); break;
	case 0xe3: UndocumentedInstruction(cpu); break;
	case 0xe4: UndocumentedInstruction(cpu); break;
	case 0xe5: UndocumentedInstruction(cpu); break;
	case 0xe6: UndocumentedInstruction(cpu); break;
	case 0xe7: UndocumentedInstruction(cpu); break;
	case 0xe8: UndocumentedInstruction(cpu); break;
	case 0xe9: UndocumentedInstruction(cpu); break;
	case 0xea: UndocumentedInstruction(cpu); break;
	case 0xeb: UndocumentedInstruction(cpu); break;
	case 0xec: UndocumentedInstruction(cpu); break;
	case 0xed: UndocumentedInstruction(cpu); break;
	case 0xee: UndocumentedInstruction(cpu); break;
	case 0xef: UndocumentedInstruction(cpu); break;

	case 0xf0: UndocumentedInstruction(cpu); break;
	case 0xf1: UndocumentedInstruction(cpu); break;
	case 0xf2: UndocumentedInstruction(cpu); break;
	case 0xf3: UndocumentedInstruction(cpu); break;
	case 0xf4: UndocumentedInstruction(cpu); break;
	case 0xf5: UndocumentedInstruction(cpu); break;
	case 0xf6: UndocumentedInstruction(cpu); break;
	case 0xf7: UndocumentedInstruction(cpu); break;
	case 0xf8: UndocumentedInstruction(cpu); break;
	case 0xf9: UndocumentedInstruction(cpu); break;
	case 0xfa: UndocumentedInstruction(cpu); break;
	case 0xfb: UndocumentedInstruction(cpu); break;
	case 0xfc: UndocumentedInstruction(cpu); break;
	case 0xfd: UndocumentedInstruction(cpu); break;
	case 0xfe: UndocumentedInstruction(cpu); break;
	case 0xff: UndocumentedInstruction(cpu); break;
	}

	cpu->pc += 1;  //advance the program counter for the next opcode    
}
