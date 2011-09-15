#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define REG_X0_BIT 	(1 << 0)
#define REG_X1_BIT 	(1 << 1)
#define REG_Y0_BIT 	(1 << 2)
#define REG_Y1_BIT 	(1 << 3)
#define NUM_REGS 	4

#define ALU_ADD		0
#define ALU_XOR		1
#define ALU_AND		2
#define ALU_OR		3
#define ALU_NOPX	4
#define ALU_NOPY	5
#define ALU_MAX		6

#define CODE_ADDR_BITS	6
#define CODE_ROM_SIZE	(1 << CODE_ADDR_BITS)
#define CODE_ADDR_MASK	(CODE_ROM_SIZE - 1)

struct cpu {
#define REG_X 0
#define REG_Y 2
	uint8_t reg[4];

	uint8_t w_en:4;
	uint8_t imm:1;
	uint8_t bank_x:1;
	uint8_t bank_y:1;
	uint8_t hlt:1;

	uint8_t alu_op:3;
	uint8_t carry:1;
	uint8_t zero:1;
	uint8_t pad_:3;

	uint8_t bus;
	uint8_t hold;

	uint8_t ip;
	uint16_t code_rom[CODE_ROM_SIZE];

	unsigned int clk;
};

static const char * const cpu_reg_name(uint8_t reg)
{
	static const char * const r[] = {"x0", "x1", "y0", "y1"};
	if ( reg & ~0x3 )
		abort();
	return r[reg & 0x3];
}

static const char * const cpu_alu_name(uint8_t op)
{
	static const char * const a[] = {"add", "xor", "and",
					"or", "nopx", "nopy"};
	if ( op >= ALU_MAX )
		abort();
	return a[op];
}

static unsigned int cpu_halted(struct cpu *cpu)
{
	return cpu->hlt;
}

static void cpu_halt(struct cpu *cpu)
{
	cpu->hlt = 1;
	printf("HALT: reg x0 is 0x%.2x (%u)\n", cpu->reg[0], cpu->reg[0]);
	printf("HALT: program terminated in %u cycles\n", cpu->clk);
}

static void cpu_update_alu(struct cpu *cpu)
{
	uint8_t in_x, in_y, out;
	uint16_t tmp;
	uint8_t carry = 0;

	/* get inputs */
	in_x = cpu->reg[REG_X + cpu->bank_x];
	in_y = cpu->reg[REG_Y + cpu->bank_y];

	/* do the math */
	switch(cpu->alu_op) {
	case ALU_ADD:
		tmp = in_x + in_y;
		carry = !!(tmp & 0x100);
		out = tmp;
		break;
	case ALU_XOR:
		out = in_x ^ in_y;
		break;
	case ALU_AND:
		out = in_x & in_y;
		break;
	case ALU_OR:
		out = in_x | in_y;
		break;
	case ALU_NOPX:
		out = in_x;
		break;
	case ALU_NOPY:
		out = in_y;
		break;
	default:
		abort();
	}

	/* update output state */
	cpu->carry = carry;
	cpu->zero = (out == 0);
	cpu->hold = out;

	printf(" + alu: %s %s, %s = 0x%.2x (%u) %scarry\n",
		cpu_alu_name(cpu->alu_op),
		cpu_reg_name(REG_X + cpu->bank_x),
		cpu_reg_name(REG_Y + cpu->bank_y),
		cpu->hold, cpu->hold,
		cpu->carry ? "" : "no-");
}

/* Strobe write-enable lines: stores value of bus to registers */
static void cpu_wr_enable(struct cpu *cpu, uint8_t regs)
{
	uint8_t i;
	uint8_t in;

	in = (cpu->imm) ? cpu->code_rom[cpu->ip] : cpu->bus;

	for(i = 0; i < NUM_REGS; i++) {
		if ( regs & (1 << i) ) {
			printf(" + reg: (from %s bus) %s := 0x%.2x (%u)\n",
				cpu->imm ? "code" : "data",
				cpu_reg_name(i), in, in);
			cpu->reg[i] = in;
		}
	}

	/* alu state is reflected immediately on store */
	cpu_update_alu(cpu);
}

/* select ALU operation */
static void cpu_op_select(struct cpu *cpu, uint8_t op)
{
	if ( op >= ALU_MAX )
		abort();
	cpu->alu_op = op;
	printf(" + alu: op select: %s\n", cpu_alu_name(op));
}

static void cpu_reset(struct cpu *cpu)
{
	printf(" cpu: reset\n");

	/* not halted */
	cpu->hlt = 0;

	/* initialize cycle counter */
	cpu->clk = 0;

	/* set insn ptr to reset vector */
	cpu->ip = 0;

	/* select 'add' */
	cpu_op_select(cpu, 0);

	/* select input banks for ALU */
	cpu->bank_x = 0;
	cpu->bank_y = 0;

	/* set the input bus to zero and strobe write-enable on all regs */
	cpu->bus = 0;

	/* strobe write-enable to clear regs */
	cpu_wr_enable(cpu, ~0);
}

/* we have 4 kinds of conditional branchs:
 * jz, jnz, jc, jnc for jump [not] [zero|carry]
*/
static void cpu_op_condbranch(struct cpu *cpu, uint16_t op)
{
	uint8_t invert;
	uint8_t zero;
	uint8_t val;

	invert = (op & (1 << 7));
	zero = (op & (1 << 6));

	printf(" cond-branch: if%s %s to 0x%.2x (%u)\n",
		(op & (1 << 7)) ? " not" : "",
		(op & (1 << 6)) ? "zero" : "carry",
		op & CODE_ADDR_MASK,
		op & CODE_ADDR_MASK);

	val = (zero) ? cpu->zero : cpu->carry;
	if ( val ^ invert ) {
		printf(" cond-branch: TAKEN\n");
		cpu->ip = op & CODE_ADDR_MASK;
	}
}

/* 1 bit for each input bank select, 3bits to encode operation and
 * 2 bits to encode destination register=
*/
static void cpu_op_alu(struct cpu *cpu, uint16_t op)
{
	/* select op + inputs */
	cpu->bank_x = !!(op & (1 << 6));
	cpu->bank_y = !!(op & (1 << 5));
	cpu_op_select(cpu, (op >> 2) & 0x7);

	/* ALU state updated immediately */
	cpu_update_alu(cpu);

	/* once output is stable in hold register, dump
	 * the result on to the bus */
	cpu->bus = cpu->hold;

	/* strobe the write enable line for the output register */
	cpu_wr_enable(cpu, (1 << (op & 0x3)));
}

/* unconditional branch */
static void cpu_op_branch(struct cpu *cpu, uint16_t op)
{
	printf(" branch: to address 0x%.2x (%u)\n",
		op & CODE_ADDR_MASK,
		op & CODE_ADDR_MASK);
	cpu->ip = op & CODE_ADDR_MASK;
}

/* load immediate */
static void cpu_op_ldi(struct cpu *cpu, uint16_t op)
{
	printf(" ldi:\n");

	/* set bus to immediate-mode, this hooks up code-rom output
	 * to input busses of register */
	cpu->imm = 1;

	/* select register and strobe write-enable line */
	cpu_wr_enable(cpu, op & 0xf);

	/* put everything back how it was */
	cpu->imm = 0;

	/* bump the ip so we don't interpret immediate data as code */
	cpu->ip++;
}

/* clear carry flag */
static void cpu_op_clc(struct cpu *cpu)
{
	printf(" clc:\n");
	cpu->carry = 0;
}

static void cpu_fetch_execute(struct cpu *cpu)
{
	uint16_t insn;

	if ( cpu->ip > sizeof(cpu->code_rom)/sizeof(*cpu->code_rom) )
		abort();

	insn = cpu->code_rom[cpu->ip++];
	cpu->clk++;

	printf("clu: Fetched insn 0x%.3x at address 0x%.2x (%u)\n",
		insn, cpu->ip, cpu->ip);
	
	if ( insn & (1 << 8) ) {
		cpu_op_condbranch(cpu, insn & 0xff);
		return;
	}

	if ( insn & (1 << 7) ) {
		cpu_op_alu(cpu, insn & 0x7f);
		return;
	}

	if ( insn & (1 << 6) ) {
		cpu_op_branch(cpu, insn & 0x3f);
		return;
	}

	if ( insn & (1 << 5) ) {
		/* reserved for insn requiring 5 bits of operand */
		printf("INVALID INSN\n");
		cpu_halt(cpu);
		return;
	}
	if ( insn & (1 << 4) ) {
		cpu_op_ldi(cpu, insn & 0xf);
		return;
	}

	switch ( insn & 0x7 ) {
	case 0:
		cpu_halt(cpu);
		break;
	case 1:
		cpu_op_clc(cpu);
		break;
	default:
		/* 6 opcodes available with no operands */
		printf("INVALID INSN\n");
		cpu_halt(cpu);
		break;
	}
}

static int open_code_rom(struct cpu *cpu, FILE *f, const char *cmd)
{
	size_t s;

	s = fread(cpu->code_rom, 1, sizeof(cpu->code_rom), f);
	printf("cpu: read %zu bytes of code\n", s);

	if ( ferror(f) ) {
		fprintf(stderr, "%s: fread: %s\n", cmd, strerror(errno));
		return 0;
	}

	return 1;
}

int main(int argc, char **argv)
{
	struct cpu cpu;
	FILE *f;

	memset(cpu.code_rom, 0, sizeof(cpu.code_rom));

	f = (argc > 1) ? fopen(argv[1], "r") : stdin;
	if ( NULL == f ) {
		fprintf(stderr, "%s: fopen: %s\n", argv[0], strerror(errno));
		return EXIT_FAILURE;
	}

	if ( !open_code_rom(&cpu, f, argv[0]) )
		return 0;

	printf("cpu: state %zu bytes\n", sizeof(struct cpu));
	cpu_reset(&cpu);

	while(!cpu_halted(&cpu))
		cpu_fetch_execute(&cpu);

	return EXIT_SUCCESS;
}
