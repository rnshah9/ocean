//TODO: implement all opcodes we'll be using so we can keep track of the registers and their values
//TODO: replace our "real" registers with "virtual" registers

#include "codegen.h"
#include "token.h"
#include "rhd/linked_list.h"
#include <assert.h>
#include <stdlib.h>

static int instruction_position(compiler_t *ctx)
{
    return heap_string_size(&ctx->instr);
}

static void dd(compiler_t *ctx, u32 i)
{
    union
    {
        uint32_t i;
        uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
		heap_string_push(&ctx->instr, u.b[i]);
}

static void dw(compiler_t *ctx, u16 i)
{
    union
    {
        uint16_t s;
        uint8_t b[2];
    } u = { .s = i };

    heap_string_push(&ctx->instr, u.b[0]);
    heap_string_push(&ctx->instr, u.b[1]);
}

static void db(compiler_t *ctx, u8 op)
{
    heap_string_push(&ctx->instr, op);
}

static void set8(compiler_t *ctx, int offset, u8 op)
{
    ctx->instr[offset] = op;
}

static void set32(compiler_t *ctx, int offset, u32 value)
{
    u32 *ptr = (u32*)&ctx->instr[offset];
    *ptr = value;
}

static void buf(compiler_t *ctx, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(&ctx->instr, buf[i] & 0xff);
    }
}

static void nop(compiler_t *ctx)
{
	db(ctx, 0x90);
}

static int jump_instruction_opcode(compiler_t *ctx, int type)
{
	switch(type & ~RJ_REVERSE)
	{
		case RJ_JNZ:		
			db(ctx, 0x0f);
			db(ctx, 0x85);
			return 2;
		case RJ_JZ:
			db(ctx, 0x0f);
			db(ctx, 0x84);
			return 2;
		case RJ_JLE:
			db(ctx, 0x0f);
			db(ctx, 0x8e);
			return 2;
		case RJ_JL:
			db(ctx, 0x0f);
			db(ctx, 0x8c);
			return 2;
		case RJ_JG:
			db(ctx, 0x0f);
			db(ctx, 0x8f);
			return 2;
		case RJ_JGE:
			db(ctx, 0x0f);
			db(ctx, 0x8d);
			return 2;
		case RJ_JMP:
			db(ctx, 0xe9);
			return 1;
	}
	perror("unhandled jump_instruction_opcode");
	return 0;
}

static void jmp_begin(compiler_t *ctx, reljmp_t *jmp, int type)
{
	assert(jmp);
	jmp->type = type;
	
	if((type & RJ_REVERSE) == RJ_REVERSE)
	{
		//only save position for now
		jmp->ip = instruction_position(ctx);
		return;
	}
	jump_instruction_opcode(ctx, type);	
	jmp->data_index = instruction_position(ctx);
	dd(ctx, 0x0);
	jmp->ip = instruction_position(ctx);
}

static void jmp_end(compiler_t *ctx, reljmp_t *jmp)
{
	assert(jmp);
	
	if((jmp->type & RJ_REVERSE) != RJ_REVERSE)
	{
		jmp->ip = instruction_position(ctx);
		set32(ctx, jmp->data_index, jmp->ip - instruction_position(ctx)); //set the relative offset for the jmp
		return;
	}
	//only set position
	i32 ip = instruction_position(ctx);
	int n = jump_instruction_opcode(ctx, jmp->type);
	dd(ctx, jmp->ip - ip - (n + 4));
}

static void jmp(compiler_t *ctx, int rel)
{
	//jmp rel
	db(ctx, 0xeb);
	db(ctx, rel - 2);
	ctx->print(ctx, "jmp 0x%x", rel + 2);
}

//returns how many bytes this instruction takes up
static int indirect_call_imm32(compiler_t *ctx, intptr_t loc, int *address_loc)
{
	exit(-1);
	return 0;
}

static void int3(compiler_t *ctx)
{
	db(ctx, 0xcc); //int3
	ctx->print(ctx, "int3");
}

static void push(compiler_t *ctx, reg_t reg)
{
    db(ctx, 0x50 + reg);
	ctx->print(ctx, "push %s", register_x86_names[reg]);
}

static reg_t inc(compiler_t *ctx, reg_t reg)
{
    db(ctx, 0x40 + reg);
	ctx->print(ctx, "inc %s", register_x86_names[reg]);
	return reg;
}

static void pop(compiler_t *ctx, reg_t reg)
{
	if(reg >= R8)
	{
		db(ctx, 0x41);
		db(ctx, 0x50 + (reg - R8));
	} else
	{
		db(ctx, 0x58 + reg);
	}
	ctx->print(ctx, "pop %s", register_x86_names[reg]);
}

//mov rax,imm32
static void mov_r_imm32(compiler_t *ctx, reg_t reg, i32 imm, int *data_loc)
{
	db(ctx, 0x48);
	db(ctx, 0xc7);
    db(ctx, 0xc0 + reg);
	if(data_loc)
		*data_loc = instruction_position( ctx );
    dd(ctx, imm);
	ctx->print(ctx, "mov %s, 0x%x", register_x86_names[reg], imm);
}

static reg_t add(compiler_t *ctx, reg_t a, reg_t b)
{
    db(ctx, 0x48);
    db(ctx, 0x01);
    db(ctx, 0xc0 + b * 8 + a);
	ctx->print(ctx, "add %s, %s", register_x86_names[a], register_x86_names[b]);
	return a;
}

static reg_t mov(compiler_t *ctx, reg_t a, reg_t b)
{
	if(a == b)
		return; //e.g mov eax,eax do nothing
    db(ctx, 0x48);
    db(ctx, 0x89);
    db(ctx, 0xc0 + b * 8 + a);
	ctx->print(ctx, "mov %s, %s", register_x86_names[a], register_x86_names[b]);
	return a;
}

static void test(compiler_t *ctx, reg_t a, reg_t b)
{
    db(ctx, 0x48);
    db(ctx, 0x85);
    db(ctx, 0xc0 + b * 8 + a);
	ctx->print(ctx, "test %s, %s", register_x86_names[a], register_x86_names[b]);
}

static reg_t imul(compiler_t *ctx, reg_t reg)
{
    db(ctx, 0x48);
    db(ctx, 0xf7);
    db(ctx, 0xe8 + reg);
	ctx->print(ctx, "imul %s", register_x86_names[reg]);
	return EAX;
}

static reg_t idiv(compiler_t *ctx, reg_t reg)
{
    db(ctx, 0x48);
    db(ctx, 0xf7);
    db(ctx, 0xf8 + reg);
	ctx->print(ctx, "idiv %s", register_x86_names[reg]);
	return EAX;
}

static void int_imm8(compiler_t *ctx, u8 value)
{
	db(ctx, 0xcd);
	db(ctx, value);
	ctx->print(ctx, "int 0x%x", value & 0xff);
}

static void exit_instr(compiler_t *ctx, reg_t reg)
{
	mov_r_imm32(ctx, EAX, 60, NULL);
	mov(ctx, EDI, reg);
	//syscall
	db(ctx, 0x0f);
	db(ctx, 0x05);
}

static reg_t xor(compiler_t *ctx, reg_t a, reg_t b)
{
    db(ctx, 0x48);
	db( ctx, 0x31 );
	db( ctx, 0xc0 + b * 8 + a );
	ctx->print(ctx, "xor %s, %s", register_x86_names[a], register_x86_names[b]);
	return a;
}

static const reg_t syscall_argument_order[] = {EDI,ESI,EDX,R10,R8,R9};
	
static void invoke_syscall(compiler_t *ctx, struct ast_node **args, int numargs)
{
	assert(numargs > 0);
	for (int i = 0; i < 6 - numargs; ++i)
	{
		xor (ctx, EAX, EAX);
		push(ctx, EAX);
	}
	for (int i = 0; i < numargs; ++i)
	{
		ctx->rvalue(ctx, EAX, args[numargs - i - 1]);
		push(ctx, EAX);
	}
	pop(ctx, EDI);
	pop(ctx, ESI);
	pop(ctx, EDX);
	pop(ctx, R10);
	pop(ctx, R8);
	pop(ctx, R9);
	
	//syscall
	db(ctx, 0x0f);
	db(ctx, 0x05);
}

static void cmp(compiler_t *ctx, reg_t a, reg_t b)
{
    db(ctx, 0x48);
    db(ctx, 0x39);
    db(ctx, 0xc0 + b * 8 + a);
	ctx->print(ctx, "cmp %s, %s", register_x86_names[a], register_x86_names[b]);
}

static int if_beg(compiler_t *ctx, reg_t a, int operator, reg_t b, int *offset)
{
	//debugging
	int3(ctx);
	int3(ctx);
	int3(ctx);
	
	cmp(ctx, a, b);
	//jmp <relative imm32>
	switch(operator)
	{
		case TK_GEQUAL:
			db(ctx, 0x0f);
			db(ctx, 0x8c);
			ctx->print(ctx, "jle ; if_beg");
		break;
		default:
			perror("unhandled if_beg");
			//db(ctx, 0xe9);
			//ctx->print(ctx, "jmp ; if_beg");
		break;
	}
	
	//we can't just do *offset = (int*)&ctx->instr[instruction_position(ctx)], because when emitting new instructions, the pointer will be invalidated.
	//set value first, then save position into offset
	int ip = instruction_position(ctx);
	*offset = ip;
	dd(ctx, ip); //temporary value set the value at instr[ip] to ip, if_end will then just subtract the then current ip
	return 0; //unused for now
}

static int if_else(compiler_t *ctx, int *offset)
{
	//get temp value from <rel imm32>
    i32 *ptr = (i32*)&ctx->instr[*offset];
	
	int ip = instruction_position(ctx);
	int rel = ip - (*ptr + 4); //calculate rel
	ctx->print(ctx, "; setting if_begin to %d", rel);
	*ptr = rel; //set rel32 to real rel + sizeof the next jmp instruction
	//jmp <relative imm32>
	
	ctx->print(ctx, "jmp ; if_else");
	
	db(ctx, 0xe9);
	
	ip = instruction_position(ctx);
	*offset = ip;
	dd(ctx, ip); //to be replaced by if_end
	return 0; //unused for now
}

static int if_end(compiler_t *ctx, int *offset)
{
    i32 *ptr = (i32*)&ctx->instr[*offset];
	
	int ip = instruction_position(ctx);
	int rel = ip - (*ptr + 4);
	*ptr = rel;
	ctx->print(ctx, "; setting if_else to %d", rel);
	*offset = -1;
	
	ctx->print(ctx, "; if_end");
	//debugging
	int3(ctx);
	int3(ctx);
	int3(ctx);
	return 0; //unused for now
}

//TODO: FIXME replace actual loc with index to function table
static void call_imm32(compiler_t *ctx, int loc)
{
	int t = instruction_position(ctx);
	db(ctx, 0xe8);
	dd(ctx, loc - t - 5);
	ctx->print(ctx, "call 0x%x", loc);
}

static void call_r32(compiler_t *ctx, reg_t reg)
{
	db(ctx, 0xff);
	db(ctx, 0xd0 + reg);
	ctx->print(ctx, "call %s", register_x86_names[reg]);
}

static void ret(compiler_t *ctx)
{
	db(ctx, 0xc3);
}

static reg_t add_imm8_to_r32(compiler_t *ctx, reg_t a, u8 value)
{
    db(ctx, 0x48);
    db(ctx, 0x83);
    db(ctx, 0xc4);
    db(ctx, value);
	ctx->print(ctx, "add %s, 0x%x", register_x86_names[a], value & 0xff);
	return a;
}

static reg_t add_imm32_to_r32(compiler_t *ctx, reg_t a, u32 value)
{
    db(ctx, 0x48);
    db(ctx, 0x81);
    db(ctx, 0xc0 + a);
    dd(ctx, value);
	ctx->print(ctx, "add %s, 0x%x", register_x86_names[a], value);
	return a;
}

static reg_t sub(compiler_t *ctx, reg_t a, reg_t b)
{
    db(ctx, 0x48);
    db(ctx, 0x29);
    db(ctx, 0xc0 + b * 8 + a);
	ctx->print(ctx, "sub %s, %s", register_x86_names[a], register_x86_names[b]);
	return a;
}

static void load_regn_base_offset_imm32(compiler_t *ctx, reg_t reg, i32 imm)
{	
	// mov r64,[rbp - offset]
	db(ctx, 0x48);
	db(ctx, 0x8b);
	db(ctx, 0x85 + 8 * reg);
	dd(ctx, imm);
}

static void load_address_regn_base_offset_imm32(compiler_t *ctx, reg_t reg, i32 imm)
{
	// lea r64,[rbp - offset]
    db(ctx, 0x48);
	db(ctx, 0x8d);
	db(ctx, 0x85 + 8 * reg);
	dd(ctx, imm);	
}

static void sub_r64_imm32(compiler_t *ctx, reg_t reg, i32 imm)
{
    db(ctx, 0x48);
    db(ctx, 0x81);
    db(ctx, 0xe8 + reg);
	dd(ctx, imm);
}

static void sub_regn_imm32(compiler_t *ctx, reg_t reg, i32 imm)
{
	sub_r64_imm32(ctx, reg, imm);
}

static void and(compiler_t *ctx, reg_t a, reg_t b)
{
    db(ctx, 0x48);
    db(ctx, 0x21);
    db(ctx, 0xc0 + b * 8 + a);
	ctx->print(ctx, "add %s, %s", register_x86_names[a], register_x86_names[b]);
}

static void or(compiler_t *ctx, reg_t a, reg_t b)
{
    db(ctx, 0x48);
    db(ctx, 0x09);
    db(ctx, 0xc0 + b * 8 + a);
	ctx->print(ctx, "or %s, %s", register_x86_names[a], register_x86_names[b]);
}

static reg_t mod(compiler_t *ctx, reg_t a, reg_t b)
{
	if(a == EAX)
	{
		//a % b
		//EAX % b
		idiv(ctx, b); //places remainder into EDX
		mov(ctx, a, EDX);
		return a;
	}
	push(ctx, a);
	mov(ctx, EAX, a);
	reg_t dst = mod(ctx, EAX, b);
	pop(ctx, a);
	return dst;
}

static void load_reg(compiler_t *ctx, reg_t a, reg_t b)
{
	db( ctx, 0x48 );
	db( ctx, 0x8b );
	db( ctx, b * 8 + a );
}

static void store_reg(compiler_t *ctx, reg_t a, reg_t b)
{
	db( ctx, 0x48 );
	db( ctx, 0x89 );
	db( ctx, b * 8 + a );
}

static int add_data(compiler_t *ctx, void *data, u32 data_size)
{
    int curpos = heap_string_size(&ctx->data);
    heap_string_appendn(&ctx->data, data, data_size);
	return curpos;
}

static void mov_r_string(compiler_t *ctx, reg_t reg, const char *str)
{
	int from;
	mov_r_imm32(ctx, reg, 0xcccccccc, &from); //placeholder
	
    int to, sz;
    if(strlen(str) > 0)
	{
		sz = strlen( str ) + 1;
		to = add_data( ctx, (void*)str, sz );
	} else
	{
		sz = 0;
        to = 0;
	}

	// TODO: FIXME make it cleaner and add just a function call before the placeholder inject/xref something
	// and make it work with any type of data so it can go into the .data segment

	struct relocation reloc = { .from = from, .to = to, .size = sz, .type = RELOC_DATA };
	linked_list_prepend( ctx->relocations, reloc );
}

static reg_t neg(compiler_t *ctx, reg_t reg)
{	
	//neg eax
	//db(ctx, 0xf7);
	//db(ctx, 0xd8);
	push(ctx, ECX);
	xor(ctx, ECX, ECX);
	sub(ctx, ECX, reg);
	mov(ctx, reg, ECX);
	pop(ctx, ECX);
	return reg;
}

void codegen_x64(codegen_t *cg)
{
	cg->add = add;
	cg->sub = sub;
	cg->mod = mod;
	cg->imul = imul;
	cg->idiv = idiv;
	cg->add_imm8_to_r32 = add_imm8_to_r32;
	cg->add_imm32_to_r32 = add_imm32_to_r32;
	cg->inc = inc;
	cg->neg = neg;
	cg->sub_regn_imm32 = sub_regn_imm32;
	cg->xor = xor;
	cg->and = and;
	cg->or = or;
	cg->int3 = int3;
	cg->nop = nop;
	cg->invoke_syscall = invoke_syscall;
	cg->exit_instr = exit_instr;
	cg->push = push;
	cg->pop = pop;
	cg->load_reg = load_reg;
	cg->store_reg = store_reg;
	cg->load_regn_base_offset_imm32 = load_regn_base_offset_imm32;
	cg->ret = ret;
	cg->indirect_call_imm32 = indirect_call_imm32;
	cg->call_imm32 = call_imm32;
	cg->call_r32 = call_r32;
	cg->mov_r_imm32 = mov_r_imm32;
	cg->mov_r_string = mov_r_string;
	cg->mov = mov;
	cg->cmp = cmp;
	cg->test = test;
	cg->if_beg = if_beg;
	cg->if_else = if_else;
	cg->if_end = if_end;
	cg->jmp_begin = jmp_begin;
	cg->jmp_end = jmp_end;
	cg->add_data = add_data;
}
