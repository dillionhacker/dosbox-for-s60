/*
 *  Copyright (C) 2002-2008  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


/* ARMv4 (little endian) backend by M-HT (thumb version) */


// temporary "lo" registers
#define templo1 HOST_v3
#define templo2 HOST_v4

// temporary "lo" register - value must be preserved when using it
#define templosav HOST_a3

// temporary "hi" register
#define temphi1 HOST_ip

// register that holds function return values
#define FC_RETOP HOST_v2

// register used for address calculations,
#define FC_ADDR HOST_v1			// has to be saved across calls, see DRC_PROTECT_ADDR_REG

// register that holds the first parameter
#define FC_OP1 HOST_a1

// register that holds the second parameter
#define FC_OP2 HOST_a2

// register that holds byte-accessible temporary values
#define FC_TMP_BA1 HOST_a1

// register that holds byte-accessible temporary values
#define FC_TMP_BA2 HOST_a2

// temporary register for LEA
#define TEMP_REG_DRC HOST_a4

#ifdef DRC_USE_REGS_ADDR
// used to hold the address of "cpu_regs" - preferably filled in function gen_run_code
#define FC_REGS_ADDR HOST_v7
#endif

#ifdef DRC_USE_SEGS_ADDR
// used to hold the address of "Segs" - preferably filled in function gen_run_code
#define FC_SEGS_ADDR HOST_v8
#endif


// move a full register from reg_src to reg_dst
static void gen_mov_regs(HostReg reg_dst,HostReg reg_src) {
	if(reg_src == reg_dst) return;
	cache_addw(0x1c00 + reg_dst + (reg_src << 3));      // mov reg_dst, reg_src
}

// move a 32bit constant value into dest_reg
static void gen_mov_dword_to_reg_imm(HostReg dest_reg,Bit32u imm) {
	if ((imm & 0xffffff00) == 0) {
		cache_addw(0x2000 + (dest_reg << 8) + imm);      // mov dest_reg, #(imm)
	} else if ((imm & 0xffff00ff) == 0) {
		cache_addw(0x2000 + (dest_reg << 8) + (imm >> 8));      // mov dest_reg, #(imm >> 8)
		cache_addw(0x0000 + dest_reg + (dest_reg << 3) + (8 << 6));      // lsl dest_reg, dest_reg, #8
	} else if ((imm & 0xff00ffff) == 0) {
		cache_addw(0x2000 + (dest_reg << 8) + (imm >> 16));      // mov dest_reg, #(imm >> 16)
		cache_addw(0x0000 + dest_reg + (dest_reg << 3) + (16 << 6));      // lsl dest_reg, dest_reg, #16
	} else if ((imm & 0x00ffffff) == 0) {
		cache_addw(0x2000 + (dest_reg << 8) + (imm >> 24));      // mov dest_reg, #(imm >> 24)
		cache_addw(0x0000 + dest_reg + (dest_reg << 3) + (24 << 6));      // lsl dest_reg, dest_reg, #24
	} else {
		Bit32u diff;
		
		diff = imm - ((Bit32u)cache.pos+4);
		
		if ((diff < 1024) && ((imm & 0x03) == 0)) {
			if (((Bit32u)cache.pos & 0x03) == 0) {
				cache_addw(0xa000 + (dest_reg << 8) + (diff >> 2));      // add dest_reg, pc, #(dist >> 2)
			} else {
				cache_addw(0x46c0);      // nop
				cache_addw(0xa000 + (dest_reg << 8) + ((diff - 2) >> 2));      // add dest_reg, pc, #((dist - 2) >> 2)
			}
		} else {
			if (((Bit32u)cache.pos & 0x03) == 0) {
				cache_addw(0x4800 + (dest_reg << 8));      // ldr dest_reg, [pc, #0]
				cache_addw(0xe000 + (2 >> 1));      // b next_code (pc+2)
				cache_addd(imm);      // .int imm
				// next_code:
			} else {
				cache_addw(0x4800 + (dest_reg << 8) + (4 >> 2));      // ldr dest_reg, [pc, #4]
				cache_addw(0xe000 + (4 >> 1));      // b next_code (pc+4)
				cache_addw(0x46c0);      // nop
				cache_addd(imm);      // .int imm
				// next_code:
			}
		}
	}
}

// helper function for gen_mov_word_to_reg
static void gen_mov_word_to_reg_helper(HostReg dest_reg,void* data,bool dword,HostReg data_reg) {
	// alignment....
	if (dword) {
		if ((Bit32u)data & 3) {
			if ( ((Bit32u)data & 3) == 2 ) {
				cache_addw(0x8800 + dest_reg + (data_reg << 3));      // ldrh dest_reg, [data_reg]
				cache_addw(0x8800 + templo1    + (data_reg << 3) + (2 << 5));      // ldrh templo1, [data_reg, #2]
				cache_addw(0x0000 + templo1    + (templo1    << 3) + (16 << 6));      // lsl templo1, templo1, #16
				cache_addw(0x4300 + dest_reg + (templo1    << 3));      // orr dest_reg, templo1
			} else {
				cache_addw(0x7800 + dest_reg + (data_reg << 3));      // ldrb dest_reg, [data_reg]
				cache_addw(0x1c00 + templo1    + (data_reg << 3) + (1 << 6));      // add templo1, data_reg, #1
				cache_addw(0x8800 + templo1    + (templo1    << 3));      // ldrh templo1, [templo1]
				cache_addw(0x0000 + templo1    + (templo1    << 3) + (8 << 6));      // lsl templo1, templo1, #8
				cache_addw(0x4300 + dest_reg + (templo1    << 3));      // orr dest_reg, templo1
				cache_addw(0x7800 + templo1    + (data_reg << 3) + (3 << 6));      // ldrb templo1, [data_reg, #3]
				cache_addw(0x0000 + templo1    + (templo1    << 3) + (24 << 6));      // lsl templo1, templo1, #24
				cache_addw(0x4300 + dest_reg + (templo1    << 3));      // orr dest_reg, templo1
			}
		} else {
			cache_addw(0x6800 + dest_reg + (data_reg << 3));      // ldr dest_reg, [data_reg]
		}
	} else {
		if ((Bit32u)data & 1) {
			cache_addw(0x7800 + dest_reg + (data_reg << 3));      // ldrb dest_reg, [data_reg]
			cache_addw(0x7800 + templo1    + (data_reg << 3) + (1 << 6));      // ldrb templo1, [data_reg, #1]
			cache_addw(0x0000 + templo1    + (templo1    << 3) + (8 << 6));      // lsl templo1, templo1, #8
			cache_addw(0x4300 + dest_reg + (templo1    << 3));      // orr dest_reg, templo1
		} else {
			cache_addw(0x8800 + dest_reg + (data_reg << 3));      // ldrh dest_reg, [data_reg]
		}
	}
}

// move a 32bit (dword==true) or 16bit (dword==false) value from memory into dest_reg
// 16bit moves may destroy the upper 16bit of the destination register
static void gen_mov_word_to_reg(HostReg dest_reg,void* data,bool dword) {
	gen_mov_dword_to_reg_imm(templo2, (Bit32u)data);
	gen_mov_word_to_reg_helper(dest_reg, data, dword, templo2);
}

// move a 16bit constant value into dest_reg
// the upper 16bit of the destination register may be destroyed
static void INLINE gen_mov_word_to_reg_imm(HostReg dest_reg,Bit16u imm) {
	gen_mov_dword_to_reg_imm(dest_reg, (Bit32u)imm);
}

// helper function for gen_mov_word_from_reg
static void gen_mov_word_from_reg_helper(HostReg src_reg,void* dest,bool dword, HostReg data_reg) {
	// alignment....
	if (dword) {
		if ((Bit32u)dest & 3) {
			if ( ((Bit32u)dest & 3) == 2 ) {
				cache_addw(0x8000 + src_reg + (data_reg << 3));      // strh src_reg, [data_reg]
				cache_addw(0x1c00 + templo1   + (src_reg << 3));      // mov templo1, src_reg
				cache_addw(0x0800 + templo1   + (templo1    << 3) + (16 << 6));      // lsr templo1, templo1, #16
				cache_addw(0x8000 + templo1   + (data_reg << 3) + (2 << 5));      // strh templo1, [data_reg, #2]
			} else {
				cache_addw(0x7000 + src_reg + (data_reg << 3));      // strb src_reg, [data_reg]
				cache_addw(0x1c00 + templo1   + (src_reg << 3));      // mov templo1, src_reg
				cache_addw(0x0800 + templo1   + (templo1    << 3) + (8 << 6));      // lsr templo1, templo1, #8
				cache_addw(0x7000 + templo1   + (data_reg << 3) + (1 << 6));      // strb templo1, [data_reg, #1]
				cache_addw(0x1c00 + templo1   + (src_reg << 3));      // mov templo1, src_reg
				cache_addw(0x0800 + templo1   + (templo1    << 3) + (16 << 6));      // lsr templo1, templo1, #16
				cache_addw(0x7000 + templo1   + (data_reg << 3) + (2 << 6));      // strb templo1, [data_reg, #2]
				cache_addw(0x1c00 + templo1   + (src_reg << 3));      // mov templo1, src_reg
				cache_addw(0x0800 + templo1   + (templo1    << 3) + (24 << 6));      // lsr templo1, templo1, #24
				cache_addw(0x7000 + templo1   + (data_reg << 3) + (3 << 6));      // strb templo1, [data_reg, #3]
			}
		} else {
			cache_addw(0x6000 + src_reg + (data_reg << 3));      // str src_reg, [data_reg]
		}
	} else {
		if ((Bit32u)dest & 1) {
			cache_addw(0x7000 + src_reg + (data_reg << 3));      // strb src_reg, [data_reg]
			cache_addw(0x1c00 + templo1   + (src_reg << 3));      // mov templo1, src_reg
			cache_addw(0x0800 + templo1   + (templo1    << 3) + (8 << 6));      // lsr templo1, templo1, #8
			cache_addw(0x7000 + templo1   + (data_reg << 3) + (1 << 6));      // strb templo1, [data_reg, #1]
		} else {
			cache_addw(0x8000 + src_reg + (data_reg << 3));      // strh src_reg, [data_reg]
		}
	}
}

// move 32bit (dword==true) or 16bit (dword==false) of a register into memory
static void gen_mov_word_from_reg(HostReg src_reg,void* dest,bool dword) {
	gen_mov_dword_to_reg_imm(templo2, (Bit32u)dest);
	gen_mov_word_from_reg_helper(src_reg, dest, dword, templo2);
}

// move an 8bit value from memory into dest_reg
// the upper 24bit of the destination register can be destroyed
// this function does not use FC_OP1/FC_OP2 as dest_reg as these
// registers might not be directly byte-accessible on some architectures
static void gen_mov_byte_to_reg_low(HostReg dest_reg,void* data) {
	gen_mov_dword_to_reg_imm(templo1, (Bit32u)data);
	cache_addw(0x7800 + dest_reg + (templo1 << 3));      // ldrb dest_reg, [templo1]
}

// move an 8bit value from memory into dest_reg
// the upper 24bit of the destination register can be destroyed
// this function can use FC_OP1/FC_OP2 as dest_reg which are
// not directly byte-accessible on some architectures
static void INLINE gen_mov_byte_to_reg_low_canuseword(HostReg dest_reg,void* data) {
	gen_mov_byte_to_reg_low(dest_reg, data);
}

// move an 8bit constant value into dest_reg
// the upper 24bit of the destination register can be destroyed
// this function does not use FC_OP1/FC_OP2 as dest_reg as these
// registers might not be directly byte-accessible on some architectures
static void gen_mov_byte_to_reg_low_imm(HostReg dest_reg,Bit8u imm) {
	cache_addw(0x2000 + (dest_reg << 8) + imm);      // mov dest_reg, #(imm)
}

// move an 8bit constant value into dest_reg
// the upper 24bit of the destination register can be destroyed
// this function can use FC_OP1/FC_OP2 as dest_reg which are
// not directly byte-accessible on some architectures
static void INLINE gen_mov_byte_to_reg_low_imm_canuseword(HostReg dest_reg,Bit8u imm) {
	gen_mov_byte_to_reg_low_imm(dest_reg, imm);
}

// move the lowest 8bit of a register into memory
static void gen_mov_byte_from_reg_low(HostReg src_reg,void* dest) {
	gen_mov_dword_to_reg_imm(templo1, (Bit32u)dest);
	cache_addw(0x7000 + src_reg + (templo1 << 3));      // strb src_reg, [templo1]
}



// convert an 8bit word to a 32bit dword
// the register is zero-extended (sign==false) or sign-extended (sign==true)
static void gen_extend_byte(bool sign,HostReg reg) {
	cache_addw(0x0000 + reg + (reg << 3) + (24 << 6));      // lsl reg, reg, #24

	if (sign) {
		cache_addw(0x1000 + reg + (reg << 3) + (24 << 6));      // asr reg, reg, #24
	} else {
		cache_addw(0x0800 + reg + (reg << 3) + (24 << 6));      // lsr reg, reg, #24
	}
}

// convert a 16bit word to a 32bit dword
// the register is zero-extended (sign==false) or sign-extended (sign==true)
static void gen_extend_word(bool sign,HostReg reg) {
	cache_addw(0x0000 + reg + (reg << 3) + (16 << 6));      // lsl reg, reg, #16

	if (sign) {
		cache_addw(0x1000 + reg + (reg << 3) + (16 << 6));      // asr reg, reg, #16
	} else {
		cache_addw(0x0800 + reg + (reg << 3) + (16 << 6));      // lsr reg, reg, #16
	}
}

// add a 32bit value from memory to a full register
static void gen_add(HostReg reg,void* op) {
	cache_addw(0x4680 + (temphi1 - HOST_r8) + (reg << 3));      // mov temphi1, reg
	gen_mov_word_to_reg(reg, op, 1);
	cache_addw(0x4440 + (reg) + ((temphi1 - HOST_r8) << 3));      // add reg, temphi1
}

// add a 32bit constant value to a full register
static void gen_add_imm(HostReg reg,Bit32u imm) {
	if(!imm) return;
	gen_mov_dword_to_reg_imm(templo1, imm);
	cache_addw(0x1800 + reg + (reg << 3) + (templo1 << 6));      // add reg, reg, templo1
}

// and a 32bit constant value with a full register
static void gen_and_imm(HostReg reg,Bit32u imm) {
	if(imm == 0xffffffff) return;
	gen_mov_dword_to_reg_imm(templo1, imm);
	cache_addw(0x4000 + reg + (templo1<< 3));      // and reg, templo1
}


// move a 32bit constant value into memory
static void gen_mov_direct_dword(void* dest,Bit32u imm) {
	cache_addw(0x4680 + (temphi1 - HOST_r8) + (templosav << 3));      // mov temphi1, templosav
	gen_mov_dword_to_reg_imm(templosav, imm);
	gen_mov_word_from_reg(templosav, dest, 1);
	cache_addw(0x4640 + templosav + ((temphi1 - HOST_r8) << 3));      // mov templosav, temphi1
}

// move an address into memory
static void INLINE gen_mov_direct_ptr(void* dest,DRC_PTR_SIZE_IM imm) {
	gen_mov_direct_dword(dest,(Bit32u)imm);
}

// add an 8bit constant value to a dword memory value
static void gen_add_direct_byte(void* dest,Bit8s imm) {
	if(!imm) return;
	cache_addw(0x4680 + (temphi1 - HOST_r8) + (templosav << 3));      // mov temphi1, templosav
	gen_mov_dword_to_reg_imm(templo2, (Bit32u)dest);
	gen_mov_word_to_reg_helper(templosav, dest, 1, templo2);
	if (imm >= 0) {
		cache_addw(0x3000 + (templosav << 8) + ((Bit32s)imm));      // add templosav, #(imm)
	} else {
		cache_addw(0x3800 + (templosav << 8) + (-((Bit32s)imm)));      // sub templosav, #(-imm)
	}
	gen_mov_word_from_reg_helper(templosav, dest, 1, templo2);
	cache_addw(0x4640 + templosav + ((temphi1 - HOST_r8) << 3));      // mov templosav, temphi1
}

// add a 32bit (dword==true) or 16bit (dword==false) constant value to a memory value
static void gen_add_direct_word(void* dest,Bit32u imm,bool dword) {
	if(!imm) return;
	if (dword && ( (imm<128) || (imm>=0xffffff80) ) ) {
		gen_add_direct_byte(dest,(Bit8s)imm);
		return;
	}
	cache_addw(0x4680 + (temphi1 - HOST_r8) + (templosav << 3));      // mov temphi1, templosav
	gen_mov_dword_to_reg_imm(templo2, (Bit32u)dest);
	gen_mov_word_to_reg_helper(templosav, dest, dword, templo2);
	if (dword) {
		gen_mov_dword_to_reg_imm(templo1, imm);
	} else {
		gen_mov_word_to_reg_imm(templo1, (Bit16u)imm);
	}
	cache_addw(0x1800 + templosav + (templosav << 3) + (templo1 << 6));      // add templosav, templosav, templo1
	gen_mov_word_from_reg_helper(templosav, dest, dword, templo2);
	cache_addw(0x4640 + templosav + ((temphi1 - HOST_r8) << 3));      // mov templosav, temphi1
}

// subtract an 8bit constant value from a dword memory value
static void gen_sub_direct_byte(void* dest,Bit8s imm) {
	if(!imm) return;
	cache_addw(0x4680 + (temphi1 - HOST_r8) + (templosav << 3));      // mov temphi1, templosav
	gen_mov_dword_to_reg_imm(templo2, (Bit32u)dest);
	gen_mov_word_to_reg_helper(templosav, dest, 1, templo2);
	if (imm >= 0) {
		cache_addw(0x3800 + (templosav << 8) + ((Bit32s)imm));      // sub templosav, #(imm)
	} else {
		cache_addw(0x3000 + (templosav << 8) + (-((Bit32s)imm)));      // add templosav, #(-imm)
	}
	gen_mov_word_from_reg_helper(templosav, dest, 1, templo2);
	cache_addw(0x4640 + templosav + ((temphi1 - HOST_r8) << 3));      // mov templosav, temphi1
}

// subtract a 32bit (dword==true) or 16bit (dword==false) constant value from a memory value
static void gen_sub_direct_word(void* dest,Bit32u imm,bool dword) {
	if(!imm) return;
	if (dword && ( (imm<128) || (imm>=0xffffff80) ) ) {
		gen_sub_direct_byte(dest,(Bit8s)imm);
		return;
	}
	cache_addw(0x4680 + (temphi1 - HOST_r8) + (templosav << 3));      // mov temphi1, templosav
	gen_mov_dword_to_reg_imm(templo2, (Bit32u)dest);
	gen_mov_word_to_reg_helper(templosav, dest, dword, templo2);
	if (dword) {
		gen_mov_dword_to_reg_imm(templo1, imm);
	} else {
		gen_mov_word_to_reg_imm(templo1, (Bit16u)imm);
	}
	cache_addw(0x1a00 + templosav + (templosav << 3) + (templo1 << 6));      // sub templosav, templosav, templo1
	gen_mov_word_from_reg_helper(templosav, dest, dword, templo2);
	cache_addw(0x4640 + templosav + ((temphi1 - HOST_r8) << 3));      // mov templosav, temphi1
}

// effective address calculation, destination is dest_reg
// scale_reg is scaled by scale (scale_reg*(2^scale)) and
// added to dest_reg, then the immediate value is added
static INLINE void gen_lea(HostReg dest_reg,HostReg scale_reg,Bitu scale,Bits imm) {
	if (scale) {
		cache_addw(0x0000 + templo1 + (scale_reg << 3) + (scale << 6));      // lsl templo1, scale_reg, #(scale)
		cache_addw(0x1800 + dest_reg + (dest_reg << 3) + (templo1 << 6));      // add dest_reg, dest_reg, templo1
	} else {
		cache_addw(0x1800 + dest_reg + (dest_reg << 3) + (scale_reg << 6));      // add dest_reg, dest_reg, scale_reg
	}
	gen_add_imm(dest_reg, imm);
}

// effective address calculation, destination is dest_reg
// dest_reg is scaled by scale (dest_reg*(2^scale)),
// then the immediate value is added
static INLINE void gen_lea(HostReg dest_reg,Bitu scale,Bits imm) {
	if (scale) {
		cache_addw(0x0000 + dest_reg + (dest_reg << 3) + (scale << 6));      // lsl dest_reg, dest_reg, #(scale)
	}
	gen_add_imm(dest_reg, imm);
}

// generate a call to a parameterless function
static void INLINE gen_call_function_raw(void * func) {
	if (((Bit32u)cache.pos & 0x03) == 0) {
		cache_addw(0x4800 + (templo1 << 8) + (4 >> 2));      // ldr templo1, [pc, #4]
		cache_addw(0xa000 + (templo2 << 8) + (8 >> 2));      // adr templo2, after_call (add templo2, pc, #8)
		cache_addw(0x4680 + (HOST_lr - HOST_r8) + (templo2 << 3));      // mov lr, templo2
		cache_addw(0x4700 + (templo1 << 3));      // bx templo1     --- switch to arm state
	} else {
		cache_addw(0x4800 + (templo1 << 8) + (8 >> 2));      // ldr templo1, [pc, #8]
		cache_addw(0xa000 + (templo2 << 8) + (8 >> 2));      // adr templo2, after_call (add templo2, pc, #8)
		cache_addw(0x4680 + (HOST_lr - HOST_r8) + (templo2 << 3));      // mov lr, templo2
		cache_addw(0x4700 + (templo1 << 3));      // bx templo1     --- switch to arm state
		cache_addw(0x46c0);      // nop
	}
	cache_addd((Bit32u)func);      // .int func
	// after_call:

	// switch from arm to thumb state
	cache_addd(0xe2800000 + (templo1 << 12) + (HOST_pc << 16) + (1));      // add templo1, pc, #1
	cache_addd(0xe12fff10 + (templo1));      // bx templo1
	
	// thumb state from now on
	cache_addw(0x1c00 + FC_RETOP + (HOST_a1 << 3));      // mov FC_RETOP, a1
}

// generate a call to a function with paramcount parameters
// note: the parameters are loaded in the architecture specific way
// using the gen_load_param_ functions below
static Bit32u INLINE gen_call_function_setup(void * func,Bitu paramcount,bool fastcall=false) {
	Bit32u proc_addr = (Bit32u)cache.pos;
	gen_call_function_raw(func);
	return proc_addr;
	// if proc_addr is on word  boundary ((proc_addr & 0x03) == 0)
	//   then length of generated code is 22 bytes
	//   otherwise length of generated code is 24 bytes
}

#if (1)
// max of 4 parameters in a1-a4

// load an immediate value as param'th function parameter
static void INLINE gen_load_param_imm(Bitu imm,Bitu param) {
	gen_mov_dword_to_reg_imm(param, imm);
}

// load an address as param'th function parameter
static void INLINE gen_load_param_addr(Bitu addr,Bitu param) {
	gen_mov_dword_to_reg_imm(param, addr);
}

// load a host-register as param'th function parameter
static void INLINE gen_load_param_reg(Bitu reg,Bitu param) {
	gen_mov_regs(param, reg);
}

// load a value from memory as param'th function parameter
static void INLINE gen_load_param_mem(Bitu mem,Bitu param) {
	gen_mov_word_to_reg(param, (void *)mem, 1);
}
#else
	other arm abis
#endif

// jump to an address pointed at by ptr, offset is in imm
static void gen_jmp_ptr(void * ptr,Bits imm=0) {
	cache_addw(0x4680 + (temphi1 - HOST_r8) + (templosav << 3));      // mov temphi1, templosav
	gen_mov_word_to_reg(templosav, ptr, 1);

	if (imm) {
		gen_mov_dword_to_reg_imm(templo2, imm);
		cache_addw(0x1800 + templosav + (templosav << 3) + (templo2 << 6));      // add templosav, templosav, templo2
	}

#if (1)
// (*ptr) should be word aligned 
	if ((imm & 0x03) == 0) {
		cache_addw(0x6800 + templo2 + (templosav << 3));      // ldr templo2, [templosav]
	} else
#endif
	{
		cache_addw(0x7800 + templo2 + (templosav << 3));      // ldrb templo2, [templosav]
		cache_addw(0x7800 + templo1 + (templosav << 3) + (1 << 6));      // ldrb templo1, [templosav, #1]
		cache_addw(0x0000 + templo1 + (templo1 << 3) + (8 << 6));      // lsl templo1, templo1, #8
		cache_addw(0x4300 + templo2 + (templo1 << 3));      // orr templo2, templo1
		cache_addw(0x7800 + templo1 + (templosav << 3) + (2 << 6));      // ldrb templo1, [templosav, #2]
		cache_addw(0x0000 + templo1 + (templo1 << 3) + (16 << 6));      // lsl templo1, templo1, #16
		cache_addw(0x4300 + templo2 + (templo1 << 3));      // orr templo2, templo1
		cache_addw(0x7800 + templo1 + (templosav << 3) + (3 << 6));      // ldrb templo1, [templosav, #3]
		cache_addw(0x0000 + templo1 + (templo1 << 3) + (24 << 6));      // lsl templo1, templo1, #24
		cache_addw(0x4300 + templo2 + (templo1 << 3));      // orr templo2, templo1
	}

	// increase jmp address to keep thumb state
	cache_addw(0x1c00 + templo2 + (templo2 << 3) + (1 << 6));      // add templo2, templo2, #1

	cache_addw(0x4640 + templosav + ((temphi1 - HOST_r8) << 3));      // mov templosav, temphi1

	cache_addw(0x4700 + (templo2 << 3));      // bx templo2
}

// short conditional jump (+-127 bytes) if register is zero
// the destination is set by gen_fill_branch() later
static Bit32u gen_create_branch_on_zero(HostReg reg,bool dword) {
	if (dword) {
		cache_addw(0x2800 + (reg << 8));      // cmp reg, #0
	} else {
		cache_addw(0x0000 + templo1 + (reg << 3) + (16 << 6));      // lsl templo1, reg, #16
	}
	cache_addw(0xd000);      // beq j
	return ((Bit32u)cache.pos-2);
}

// short conditional jump (+-127 bytes) if register is nonzero
// the destination is set by gen_fill_branch() later
static Bit32u gen_create_branch_on_nonzero(HostReg reg,bool dword) {
	if (dword) {
		cache_addw(0x2800 + (reg << 8));      // cmp reg, #0
	} else {
		cache_addw(0x0000 + templo1 + (reg << 3) + (16 << 6));      // lsl templo1, reg, #16
	}
	cache_addw(0xd100);      // bne j
	return ((Bit32u)cache.pos-2);
}

// calculate relative offset and fill it into the location pointed to by data
static void INLINE gen_fill_branch(DRC_PTR_SIZE_IM data) {
#if C_DEBUG
	Bits len=(Bit32u)cache.pos-(data+4);
	if (len<0) len=-len;
	if (len>252) LOG_MSG("Big jump %d",len);
#endif
	*(Bit8u*)data=(Bit8u)( ((Bit32u)cache.pos-(data+4)) >> 1 );
}

// conditional jump if register is nonzero
// for isdword==true the 32bit of the register are tested
// for isdword==false the lowest 8bit of the register are tested
static Bit32u gen_create_branch_long_nonzero(HostReg reg,bool isdword) {
	if (isdword) {
		cache_addw(0x2800 + (reg << 8));      // cmp reg, #0
	} else {
		cache_addw(0x0000 + templo2 + (reg << 3) + (24 << 6));      // lsl templo2, reg, #24
	}
	if (((Bit32u)cache.pos & 0x03) == 0) {
		cache_addw(0xd000 + (8 >> 1));      // beq nobranch (pc+8)
		cache_addw(0x4800 + (templo1 << 8) + (4 >> 2));      // ldr templo1, [pc, #4]
		cache_addw(0x4700 + (templo1 << 3));      // bx templo1
		cache_addw(0x46c0);      // nop
	} else {
		cache_addw(0xd000 + (6 >> 1));      // beq nobranch (pc+6)
		cache_addw(0x4800 + (templo1 << 8));      // ldr templo1, [pc, #0]
		cache_addw(0x4700 + (templo1 << 3));      // bx templo1
	}
	cache_addd(0);      // fill j
	// nobranch:
	return ((Bit32u)cache.pos-4);
}

// compare 32bit-register against zero and jump if value less/equal than zero
static Bit32u gen_create_branch_long_leqzero(HostReg reg) {
	cache_addw(0x2800 + (reg << 8));      // cmp reg, #0
	if (((Bit32u)cache.pos & 0x03) == 0) {
		cache_addw(0xdc00 + (8 >> 1));      // bgt nobranch (pc+8)
		cache_addw(0x4800 + (templo1 << 8) + (4 >> 2));      // ldr templo1, [pc, #4]
		cache_addw(0x4700 + (templo1 << 3));      // bx templo1
		cache_addw(0x46c0);      // nop
	} else {
		cache_addw(0xdc00 + (6 >> 1));      // bgt nobranch (pc+6)
		cache_addw(0x4800 + (templo1 << 8));      // ldr templo1, [pc, #0]
		cache_addw(0x4700 + (templo1 << 3));      // bx templo1
	}
	cache_addd(0);      // fill j
	// nobranch:
	return ((Bit32u)cache.pos-4);
}

// calculate long relative offset and fill it into the location pointed to by data
static void INLINE gen_fill_branch_long(Bit32u data) {
	// this is an absolute branch
	*(Bit32u*)data=((Bit32u)cache.pos) + 1; // add 1 to keep processor in thumb state
}

static void gen_run_code(void) {
	// switch from arm to thumb state
	cache_addd(0xe2800000 + (HOST_r3 << 12) + (HOST_pc << 16) + (1));      // add r3, pc, #1
	cache_addd(0xe12fff10 + (HOST_r3));      // bx r3

	// thumb state from now on
	cache_addw(0xb500);      // push {lr}
	cache_addw(0x4640 + HOST_r3 + ((FC_SEGS_ADDR - HOST_r8) << 3));      // mov r3, FC_SEGS_ADDR
	cache_addw(0x4640 + HOST_r2 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov r2, FC_REGS_ADDR
	cache_addw(0xb4fc);      // push {r2,r3,v1-v4}

	// adr: 16
	cache_addw(0x4800 + (HOST_r3 << 8) + ((64 - (16 + 4)) >> 2));        // ldr r3, [pc, #(&Segs)]
	// adr: 18
	cache_addw(0x4800 + (HOST_r2 << 8) + ((68 - (18 + 2)) >> 2));        // ldr r2, [pc, #(&cpu_regs)]
	cache_addw(0x4680 + (FC_SEGS_ADDR - HOST_r8) + (HOST_r3 << 3));      // mov FC_SEGS_ADDR, r3
	cache_addw(0x4680 + (FC_REGS_ADDR - HOST_r8) + (HOST_r2 << 3));      // mov FC_REGS_ADDR, r2

	// align 4
	cache_addw(0xa302);      // add r3, pc, #8
	cache_addw(0x3001);      // add r0, #1
	cache_addw(0x3301);      // add r3, #1
	cache_addw(0xb408);      // push {r3}
	cache_addw(0x4700);      // bx r0
	cache_addw(0x46c0);      // nop

	// align 4
	cache_addw(0xbcfc);      // pop {r2,r3,v1-v4}
	cache_addw(0x4680 + (FC_SEGS_ADDR - HOST_r8) + (HOST_r3 << 3));      // mov FC_SEGS_ADDR, r3
	cache_addw(0x4680 + (FC_REGS_ADDR - HOST_r8) + (HOST_r2 << 3));      // mov FC_REGS_ADDR, r2

	cache_addw(0xbc08);      // pop {r3}
	cache_addw(0x4718);      // bx r3

	// fill up to 64 bytes
	cache_addw(0x46c0);      // nop
	cache_addd(0x46c046c0);  // nop, nop
	cache_addd(0x46c046c0);  // nop, nop
	cache_addd(0x46c046c0);  // nop, nop
	cache_addd(0x46c046c0);  // nop, nop

	// adr: 64
	cache_addd((Bit32u)&Segs);      // address of "Segs"
	// adr: 68
	cache_addd((Bit32u)&cpu_regs);  // address of "cpu_regs"
}

// return from a function
static void gen_return_function(void) {
	cache_addw(0x1c00 + HOST_a1 + (FC_RETOP << 3));      // mov a1, FC_RETOP
	cache_addw(0xbc08);      // pop {r3}
	cache_addw(0x4718);      // bx r3
}

#ifdef DRC_FLAGS_INVALIDATION

// called when a call to a function can be replaced by a
// call to a simpler function
static void gen_fill_function_ptr(Bit8u * pos,void* fct_ptr,Bitu flags_type) {
#ifdef DRC_FLAGS_INVALIDATION_DCODE
	if (((Bit32u)pos & 0x03) == 0)
	{
		// try to avoid function calls but rather directly fill in code
		switch (flags_type) {
			case t_ADDb:
			case t_ADDw:
			case t_ADDd:
				*(Bit16u*)pos=0x1800 + FC_RETOP + (HOST_a1 << 3) + (HOST_a2 << 6);	// add FC_RETOP, a1, a2
				*(Bit16u*)(pos+2)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_ORb:
			case t_ORw:
			case t_ORd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4300 + FC_RETOP + (HOST_a2 << 3);				// orr FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_ANDb:
			case t_ANDw:
			case t_ANDd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4000 + FC_RETOP + (HOST_a2 << 3);				// and FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_SUBb:
			case t_SUBw:
			case t_SUBd:
				*(Bit16u*)pos=0x1a00 + FC_RETOP + (HOST_a1 << 3) + (HOST_a2 << 6);	// sub FC_RETOP, a1, a2
				*(Bit16u*)(pos+2)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_XORb:
			case t_XORw:
			case t_XORd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4040 + FC_RETOP + (HOST_a2 << 3);				// eor FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_CMPb:
			case t_CMPw:
			case t_CMPd:
			case t_TESTb:
			case t_TESTw:
			case t_TESTd:
				*(Bit16u*)pos=0xe000 + (18 >> 1);									// b after_call (pc+18)
				break;
			case t_INCb:
			case t_INCw:
			case t_INCd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3) + (1 << 6);		// add FC_RETOP, a1, #1
				*(Bit16u*)(pos+2)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_DECb:
			case t_DECw:
			case t_DECd:
				*(Bit16u*)pos=0x1e00 + FC_RETOP + (HOST_a1 << 3) + (1 << 6);		// sub FC_RETOP, a1, #1
				*(Bit16u*)(pos+2)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_SHLb:
			case t_SHLw:
			case t_SHLd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4080 + FC_RETOP + (HOST_a2 << 3);				// lsl FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_SHRb:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (24 << 6);		// lsl FC_RETOP, a1, #24
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (FC_RETOP << 3) + (24 << 6);	// lsr FC_RETOP, FC_RETOP, #24
				*(Bit16u*)(pos+4)=0x40c0 + FC_RETOP + (HOST_a2 << 3);				// lsr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (12 >> 1);								// b after_call (pc+12)
				break;
			case t_SHRw:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);		// lsl FC_RETOP, a1, #16
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (FC_RETOP << 3) + (16 << 6);	// lsr FC_RETOP, FC_RETOP, #16
				*(Bit16u*)(pos+4)=0x40c0 + FC_RETOP + (HOST_a2 << 3);				// lsr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (12 >> 1);								// b after_call (pc+12)
				break;
			case t_SHRd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x40c0 + FC_RETOP + (HOST_a2 << 3);				// lsr FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_SARb:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (24 << 6);		// lsl FC_RETOP, a1, #24
				*(Bit16u*)(pos+2)=0x1000 + FC_RETOP + (FC_RETOP << 3) + (24 << 6);	// asr FC_RETOP, FC_RETOP, #24
				*(Bit16u*)(pos+4)=0x4100 + FC_RETOP + (HOST_a2 << 3);				// asr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (12 >> 1);								// b after_call (pc+12)
				break;
			case t_SARw:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);		// lsl FC_RETOP, a1, #16
				*(Bit16u*)(pos+2)=0x1000 + FC_RETOP + (FC_RETOP << 3) + (16 << 6);	// asr FC_RETOP, FC_RETOP, #16
				*(Bit16u*)(pos+4)=0x4100 + FC_RETOP + (HOST_a2 << 3);				// asr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (12 >> 1);								// b after_call (pc+12)
				break;
			case t_SARd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4100 + FC_RETOP + (HOST_a2 << 3);				// asr FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_RORb:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (24 << 6);		// lsl a1, a1, #24
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (8 << 6);	// lsr FC_RETOP, a1, #8
				*(Bit16u*)(pos+4)=0x4300 + HOST_a1 + (FC_RETOP << 3);				// orr a1, FC_RETOP
				*(Bit16u*)(pos+6)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+8)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+10)=0x41c0 + FC_RETOP + (HOST_a2 << 3);				// ror FC_RETOP, a2
				*(Bit16u*)(pos+12)=0xe000 + (6 >> 1);								// b after_call (pc+6)
				break;
			case t_RORw:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (16 << 6);		// lsl a1, a1, #16
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+4)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+6)=0x41c0 + FC_RETOP + (HOST_a2 << 3);				// ror FC_RETOP, a2
				*(Bit16u*)(pos+8)=0xe000 + (10 >> 1);								// b after_call (pc+10)
				break;
			case t_RORd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x41c0 + FC_RETOP + (HOST_a2 << 3);				// ror FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_ROLb:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (24 << 6);		// lsl a1, a1, #24
				*(Bit16u*)(pos+2)=0x4240 + templo1 + (HOST_a2 << 3);				// neg templo1, a2
				*(Bit16u*)(pos+4)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (8 << 6);	// lsr FC_RETOP, a1, #8
				*(Bit16u*)(pos+6)=0x3000 + (templo1 << 8) + (32);					// add templo1, #32
				*(Bit16u*)(pos+8)=0x4300 + HOST_a1 + (FC_RETOP << 3);				// orr a1, FC_RETOP
				*(Bit16u*)(pos+10)=0x46c0;											// nop
				*(Bit16u*)(pos+12)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+14)=0x46c0;											// nop
				*(Bit16u*)(pos+16)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+18)=0x46c0;											// nop
				*(Bit16u*)(pos+20)=0x41c0 + FC_RETOP + (templo1 << 3);				// ror FC_RETOP, templo1
				break;
			case t_ROLw:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (16 << 6);		// lsl a1, a1, #16
				*(Bit16u*)(pos+2)=0x4240 + templo1 + (HOST_a2 << 3);				// neg templo1, a2
				*(Bit16u*)(pos+4)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+6)=0x3000 + (templo1 << 8) + (32);					// add templo1, #32
				*(Bit16u*)(pos+8)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+10)=0x41c0 + FC_RETOP + (templo1 << 3);				// ror FC_RETOP, templo1
				*(Bit16u*)(pos+12)=0xe000 + (6 >> 1);								// b after_call (pc+6)
				break;
			case t_ROLd:
				*(Bit16u*)pos=0x4240 + templo1 + (HOST_a2 << 3);					// neg templo1, a2
				*(Bit16u*)(pos+2)=0x1c00 + FC_RETOP + (HOST_a1 << 3);				// mov FC_RETOP, a1
				*(Bit16u*)(pos+4)=0x3000 + (templo1 << 8) + (32);					// add templo1, #32
				*(Bit16u*)(pos+6)=0x41c0 + FC_RETOP + (templo1 << 3);				// ror FC_RETOP, templo1
				*(Bit16u*)(pos+8)=0xe000 + (10 >> 1);								// b after_call (pc+10)
				break;
			case t_NEGb:
			case t_NEGw:
			case t_NEGd:
				*(Bit16u*)pos=0x4240 + FC_RETOP + (HOST_a1 << 3);					// neg FC_RETOP, a1
				*(Bit16u*)(pos+2)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			default:
				*(Bit32u*)(pos+8)=(Bit32u)fct_ptr;		// simple_func
				break;
		}
	}
	else
	{
		// try to avoid function calls but rather directly fill in code
		switch (flags_type) {
			case t_ADDb:
			case t_ADDw:
			case t_ADDd:
				*(Bit16u*)pos=0x1800 + FC_RETOP + (HOST_a1 << 3) + (HOST_a2 << 6);	// add FC_RETOP, a1, a2
				*(Bit16u*)(pos+2)=0xe000 + (18 >> 1);								// b after_call (pc+18)
				break;
			case t_ORb:
			case t_ORw:
			case t_ORd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4300 + FC_RETOP + (HOST_a2 << 3);				// orr FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_ANDb:
			case t_ANDw:
			case t_ANDd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4000 + FC_RETOP + (HOST_a2 << 3);				// and FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_SUBb:
			case t_SUBw:
			case t_SUBd:
				*(Bit16u*)pos=0x1a00 + FC_RETOP + (HOST_a1 << 3) + (HOST_a2 << 6);	// sub FC_RETOP, a1, a2
				*(Bit16u*)(pos+2)=0xe000 + (18 >> 1);								// b after_call (pc+18)
				break;
			case t_XORb:
			case t_XORw:
			case t_XORd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4040 + FC_RETOP + (HOST_a2 << 3);				// eor FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_CMPb:
			case t_CMPw:
			case t_CMPd:
			case t_TESTb:
			case t_TESTw:
			case t_TESTd:
				*(Bit16u*)pos=0xe000 + (20 >> 1);									// b after_call (pc+20)
				break;
			case t_INCb:
			case t_INCw:
			case t_INCd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3) + (1 << 6);		// add FC_RETOP, a1, #1
				*(Bit16u*)(pos+2)=0xe000 + (18 >> 1);								// b after_call (pc+18)
				break;
			case t_DECb:
			case t_DECw:
			case t_DECd:
				*(Bit16u*)pos=0x1e00 + FC_RETOP + (HOST_a1 << 3) + (1 << 6);		// sub FC_RETOP, a1, #1
				*(Bit16u*)(pos+2)=0xe000 + (18 >> 1);								// b after_call (pc+18)
				break;
			case t_SHLb:
			case t_SHLw:
			case t_SHLd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4080 + FC_RETOP + (HOST_a2 << 3);				// lsl FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_SHRb:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (24 << 6);		// lsl FC_RETOP, a1, #24
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (FC_RETOP << 3) + (24 << 6);	// lsr FC_RETOP, FC_RETOP, #24
				*(Bit16u*)(pos+4)=0x40c0 + FC_RETOP + (HOST_a2 << 3);				// lsr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_SHRw:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);		// lsl FC_RETOP, a1, #16
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (FC_RETOP << 3) + (16 << 6);	// lsr FC_RETOP, FC_RETOP, #16
				*(Bit16u*)(pos+4)=0x40c0 + FC_RETOP + (HOST_a2 << 3);				// lsr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_SHRd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x40c0 + FC_RETOP + (HOST_a2 << 3);				// lsr FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_SARb:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (24 << 6);		// lsl FC_RETOP, a1, #24
				*(Bit16u*)(pos+2)=0x1000 + FC_RETOP + (FC_RETOP << 3) + (24 << 6);	// asr FC_RETOP, FC_RETOP, #24
				*(Bit16u*)(pos+4)=0x4100 + FC_RETOP + (HOST_a2 << 3);				// asr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_SARw:
				*(Bit16u*)pos=0x0000 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);		// lsl FC_RETOP, a1, #16
				*(Bit16u*)(pos+2)=0x1000 + FC_RETOP + (FC_RETOP << 3) + (16 << 6);	// asr FC_RETOP, FC_RETOP, #16
				*(Bit16u*)(pos+4)=0x4100 + FC_RETOP + (HOST_a2 << 3);				// asr FC_RETOP, a2
				*(Bit16u*)(pos+6)=0xe000 + (14 >> 1);								// b after_call (pc+14)
				break;
			case t_SARd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x4100 + FC_RETOP + (HOST_a2 << 3);				// asr FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_RORb:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (24 << 6);		// lsl a1, a1, #24
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (8 << 6);	// lsr FC_RETOP, a1, #8
				*(Bit16u*)(pos+4)=0x4300 + HOST_a1 + (FC_RETOP << 3);				// orr a1, FC_RETOP
				*(Bit16u*)(pos+6)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+8)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+10)=0x41c0 + FC_RETOP + (HOST_a2 << 3);				// ror FC_RETOP, a2
				*(Bit16u*)(pos+12)=0xe000 + (8 >> 1);								// b after_call (pc+8)
				break;
			case t_RORw:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (16 << 6);		// lsl a1, a1, #16
				*(Bit16u*)(pos+2)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+4)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+6)=0x41c0 + FC_RETOP + (HOST_a2 << 3);				// ror FC_RETOP, a2
				*(Bit16u*)(pos+8)=0xe000 + (12 >> 1);								// b after_call (pc+12)
				break;
			case t_RORd:
				*(Bit16u*)pos=0x1c00 + FC_RETOP + (HOST_a1 << 3);					// mov FC_RETOP, a1
				*(Bit16u*)(pos+2)=0x41c0 + FC_RETOP + (HOST_a2 << 3);				// ror FC_RETOP, a2
				*(Bit16u*)(pos+4)=0xe000 + (16 >> 1);								// b after_call (pc+16)
				break;
			case t_ROLb:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (24 << 6);		// lsl a1, a1, #24
				*(Bit16u*)(pos+2)=0x4240 + templo1 + (HOST_a2 << 3);				// neg templo1, a2
				*(Bit16u*)(pos+4)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (8 << 6);	// lsr FC_RETOP, a1, #8
				*(Bit16u*)(pos+6)=0x3000 + (templo1 << 8) + (32);					// add templo1, #32
				*(Bit16u*)(pos+8)=0x4300 + HOST_a1 + (FC_RETOP << 3);				// orr a1, FC_RETOP
				*(Bit16u*)(pos+10)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+12)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+14)=0x41c0 + FC_RETOP + (templo1 << 3);				// ror FC_RETOP, templo1
				*(Bit16u*)(pos+16)=0xe000 + (4 >> 1);								// b after_call (pc+4)
				break;
			case t_ROLw:
				*(Bit16u*)pos=0x0000 + HOST_a1 + (HOST_a1 << 3) + (16 << 6);		// lsl a1, a1, #16
				*(Bit16u*)(pos+2)=0x4240 + templo1 + (HOST_a2 << 3);				// neg templo1, a2
				*(Bit16u*)(pos+4)=0x0800 + FC_RETOP + (HOST_a1 << 3) + (16 << 6);	// lsr FC_RETOP, a1, #16
				*(Bit16u*)(pos+6)=0x3000 + (templo1 << 8) + (32);					// add templo1, #32
				*(Bit16u*)(pos+8)=0x4300 + FC_RETOP + (HOST_a1 << 3);				// orr FC_RETOP, a1
				*(Bit16u*)(pos+10)=0x41c0 + FC_RETOP + (templo1 << 3);				// ror FC_RETOP, templo1
				*(Bit16u*)(pos+12)=0xe000 + (8 >> 1);								// b after_call (pc+8)
				break;
			case t_ROLd:
				*(Bit16u*)pos=0x4240 + templo1 + (HOST_a2 << 3);					// neg templo1, a2
				*(Bit16u*)(pos+2)=0x1c00 + FC_RETOP + (HOST_a1 << 3);				// mov FC_RETOP, a1
				*(Bit16u*)(pos+4)=0x3000 + (templo1 << 8) + (32);					// add templo1, #32
				*(Bit16u*)(pos+6)=0x41c0 + FC_RETOP + (templo1 << 3);				// ror FC_RETOP, templo1
				*(Bit16u*)(pos+8)=0xe000 + (12 >> 1);								// b after_call (pc+12)
				break;
			case t_NEGb:
			case t_NEGw:
			case t_NEGd:
				*(Bit16u*)pos=0x4240 + FC_RETOP + (HOST_a1 << 3);					// neg FC_RETOP, a1
				*(Bit16u*)(pos+2)=0xe000 + (18 >> 1);								// b after_call (pc+18)
				break;
			default:
				*(Bit32u*)(pos+10)=(Bit32u)fct_ptr;		// simple_func
				break;
		}

	}
#else
	if (((Bit32u)pos & 0x03) == 0)
	{
		*(Bit32u*)(pos+8)=(Bit32u)fct_ptr;		// simple_func
	}
	else
	{
		*(Bit32u*)(pos+10)=(Bit32u)fct_ptr;		// simple_func
	}
#endif
}
#endif

static void cache_block_before_close(void) { }

#ifdef DRC_USE_SEGS_ADDR

// mov 16bit value from Segs[index] into dest_reg using FC_SEGS_ADDR (index modulo 2 must be zero)
// 16bit moves may destroy the upper 16bit of the destination register
static void gen_mov_seg16_to_reg(HostReg dest_reg,Bitu index) {
	cache_addw(0x4640 + templo1 + ((FC_SEGS_ADDR - HOST_r8) << 3));      // mov templo1, FC_SEGS_ADDR
	cache_addw(0x8800 + dest_reg + (templo1 << 3) + (index << 5));      // ldrh dest_reg, [templo1, #index]
}

// mov 32bit value from Segs[index] into dest_reg using FC_SEGS_ADDR (index modulo 4 must be zero)
static void gen_mov_seg32_to_reg(HostReg dest_reg,Bitu index) {
	cache_addw(0x4640 + templo1 + ((FC_SEGS_ADDR - HOST_r8) << 3));      // mov templo1, FC_SEGS_ADDR
	cache_addw(0x6800 + dest_reg + (templo1 << 3) + (index << 4));      // ldr dest_reg, [templo1, #index]
}

// add a 32bit value from Segs[index] to a full register using FC_SEGS_ADDR (index modulo 4 must be zero)
static void gen_add_seg32_to_reg(HostReg reg,Bitu index) {
	cache_addw(0x4640 + templo1 + ((FC_SEGS_ADDR - HOST_r8) << 3));      // mov templo1, FC_SEGS_ADDR
	cache_addw(0x6800 + templo2 + (templo1 << 3) + (index << 4));      // ldr templo2, [templo1, #index]
	cache_addw(0x1800 + reg + (reg << 3) + (templo2 << 6));      // add reg, reg, templo2
}

#endif

#ifdef DRC_USE_REGS_ADDR

// mov 16bit value from cpu_regs[index] into dest_reg using FC_REGS_ADDR (index modulo 2 must be zero)
// 16bit moves may destroy the upper 16bit of the destination register
static void gen_mov_regval16_to_reg(HostReg dest_reg,Bitu index) {
	cache_addw(0x4640 + templo2 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo2, FC_REGS_ADDR
	cache_addw(0x8800 + dest_reg + (templo2 << 3) + (index << 5));      // ldrh dest_reg, [templo2, #index]
}

// mov 32bit value from cpu_regs[index] into dest_reg using FC_REGS_ADDR (index modulo 4 must be zero)
static void gen_mov_regval32_to_reg(HostReg dest_reg,Bitu index) {
	cache_addw(0x4640 + templo2 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo2, FC_REGS_ADDR
	cache_addw(0x6800 + dest_reg + (templo2 << 3) + (index << 4));      // ldr dest_reg, [templo2, #index]
}

// move a 32bit (dword==true) or 16bit (dword==false) value from cpu_regs[index] into dest_reg using FC_REGS_ADDR (if dword==true index modulo 4 must be zero) (if dword==false index modulo 2 must be zero)
// 16bit moves may destroy the upper 16bit of the destination register
static void gen_mov_regword_to_reg(HostReg dest_reg,Bitu index,bool dword) {
	cache_addw(0x4640 + templo2 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo2, FC_REGS_ADDR
	if (dword) {
		cache_addw(0x6800 + dest_reg + (templo2 << 3) + (index << 4));      // ldr dest_reg, [templo2, #index]
	} else {
		cache_addw(0x8800 + dest_reg + (templo2 << 3) + (index << 5));      // ldrh dest_reg, [templo2, #index]
	}
}

// move an 8bit value from cpu_regs[index]  into dest_reg using FC_REGS_ADDR
// the upper 24bit of the destination register can be destroyed
// this function does not use FC_OP1/FC_OP2 as dest_reg as these
// registers might not be directly byte-accessible on some architectures
static void gen_mov_regbyte_to_reg_low(HostReg dest_reg,Bitu index) {
	cache_addw(0x4640 + templo2 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo2, FC_REGS_ADDR
	cache_addw(0x7800 + dest_reg + (templo2 << 3) + (index << 6));      // ldrb dest_reg, [templo2, #index]
}

// move an 8bit value from cpu_regs[index]  into dest_reg using FC_REGS_ADDR
// the upper 24bit of the destination register can be destroyed
// this function can use FC_OP1/FC_OP2 as dest_reg which are
// not directly byte-accessible on some architectures
static void INLINE gen_mov_regbyte_to_reg_low_canuseword(HostReg dest_reg,Bitu index) {
	cache_addw(0x4640 + templo2 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo2, FC_REGS_ADDR
	cache_addw(0x7800 + dest_reg + (templo2 << 3) + (index << 6));      // ldrb dest_reg, [templo2, #index]
}


// add a 32bit value from cpu_regs[index] to a full register using FC_REGS_ADDR (index modulo 4 must be zero)
static void gen_add_regval32_to_reg(HostReg reg,Bitu index) {
	cache_addw(0x4640 + templo2 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo2, FC_REGS_ADDR
	cache_addw(0x6800 + templo1 + (templo2 << 3) + (index << 4));      // ldr templo1, [templo2, #index]
	cache_addw(0x1800 + reg + (reg << 3) + (templo1 << 6));      // add reg, reg, templo1
}


// move 16bit of register into cpu_regs[index] using FC_REGS_ADDR (index modulo 2 must be zero)
static void gen_mov_regval16_from_reg(HostReg src_reg,Bitu index) {
	cache_addw(0x4640 + templo1 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo1, FC_REGS_ADDR
	cache_addw(0x8000 + src_reg + (templo1 << 3) + (index << 5));      // strh src_reg, [templo1, #index]
}

// move 32bit of register into cpu_regs[index] using FC_REGS_ADDR (index modulo 4 must be zero)
static void gen_mov_regval32_from_reg(HostReg src_reg,Bitu index) {
	cache_addw(0x4640 + templo1 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo1, FC_REGS_ADDR
	cache_addw(0x6000 + src_reg + (templo1 << 3) + (index << 4));      // str src_reg, [templo1, #index]
}

// move 32bit (dword==true) or 16bit (dword==false) of a register into cpu_regs[index] using FC_REGS_ADDR (if dword==true index modulo 4 must be zero) (if dword==false index modulo 2 must be zero)
static void gen_mov_regword_from_reg(HostReg src_reg,Bitu index,bool dword) {
	cache_addw(0x4640 + templo1 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo1, FC_REGS_ADDR
	if (dword) {
		cache_addw(0x6000 + src_reg + (templo1 << 3) + (index << 4));      // str src_reg, [templo1, #index]
	} else {
		cache_addw(0x8000 + src_reg + (templo1 << 3) + (index << 5));      // strh src_reg, [templo1, #index]
	}
}

// move the lowest 8bit of a register into cpu_regs[index] using FC_REGS_ADDR
static void gen_mov_regbyte_from_reg_low(HostReg src_reg,Bitu index) {
	cache_addw(0x4640 + templo1 + ((FC_REGS_ADDR - HOST_r8) << 3));      // mov templo1, FC_REGS_ADDR
	cache_addw(0x7000 + src_reg + (templo1 << 3) + (index << 6));      // strb src_reg, [templo1, #index]
}

#endif
