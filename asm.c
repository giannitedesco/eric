#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define CODE_ADDR_BITS	6
#define CODE_ROM_SIZE	(1 << CODE_ADDR_BITS)
#define CODE_ADDR_MASK	(CODE_ROM_SIZE - 1)

#define ALU_ADD		0
#define ALU_XOR		1
#define ALU_AND		2
#define ALU_OR		3
#define ALU_NOPX	4
#define ALU_NOPY	5
#define ALU_MAX		6

struct label {
	struct label *next;
	char *name;
	uint8_t addr;
};

struct code_rom {
	uint16_t code[CODE_ROM_SIZE];
	struct label *labels;
	const char *fn;
	unsigned int line;
	uint8_t addr;
	uint8_t ip;
};

/* Easy string tokeniser */
static int easy_explode(char *str, char split,
			char **toks, int max_toks)
{
	char *tmp;
	int tok;
	int state;

	for(tmp=str,state=tok=0; *tmp && tok <= max_toks; tmp++) {
		if ( state == 0 ) {
			if ( *tmp == split && (tok < max_toks)) {
				toks[tok++] = NULL;
			}else if ( !isspace(*tmp) ) {
				state = 1;
				toks[tok++] = tmp;
			}
		}else if ( state == 1 ) {
			if ( tok < max_toks ) {
				if ( *tmp == split || isspace(*tmp) ) {
					*tmp = '\0';
					state = 0;
				}
			}else if ( *tmp == '\n' )
				*tmp = '\0';
		}
	}

	return tok;
}

static int label_is_valid(char *ptr)
{
	return 1;
}

static struct label *label_lookup(struct code_rom *rom, char *label)
{
	struct label *l;

	for(l = rom->labels; l; l = l->next)
		if ( !strcmp(l->name, label) )
			return l;

	return NULL;
}

static int add_label(struct code_rom *rom, char *ptr)
{
	struct label *l;

	if ( !label_is_valid(ptr) ) {
		fprintf(stderr, "%s:%u: invalid label name: %s\n",
			rom->fn, rom->line, ptr);
		return 0;
	}

	if ( label_lookup(rom, ptr) ) {
		fprintf(stderr, "%s:%u: duplicate label: %s\n",
			rom->fn, rom->line, ptr);
		return 0;
	}

	l = calloc(1, sizeof(*l));
	if ( NULL == l ) {
		fprintf(stderr, "%s:%u: %s\n",
			rom->fn, rom->line, strerror(errno));
		return 0;
	}

	l->name = strdup(ptr);
	if ( NULL == l->name ) {
		fprintf(stderr, "%s:%u: %s\n",
			rom->fn, rom->line, strerror(errno));
		free(l);
		return 0;
	}

	l->addr = rom->addr;
	l->next = rom->labels;
	rom->labels = l;
	return 1;
}

static int emit_insn(struct code_rom *rom, uint16_t insn)
{
	if ( rom->addr >= CODE_ROM_SIZE ) {
		return 0;
	}

	rom->code[rom->addr++] = insn;
	return 1;
}

static const char * const cpu_alu_name(uint8_t op)
{
	static const char * const a[] = {"add", "xor", "and",
					"or", "nopx", "nopy"};
	if ( op >= ALU_MAX )
		abort();
	return a[op];
}

static int reg_from_name(struct code_rom *rom, const char *name, uint8_t *num)
{
	static const char * const r[] = {"x0", "x1", "y0", "y1"};
	unsigned int i;

	for(i = 0; i < sizeof(r)/sizeof(*r); i++) {
		if ( !strcmp(r[i], name) ) {
			*num = i;
			return 1;
		}
	}

	fprintf(stderr, "%s:%u: unknown register: %s\n",
		rom->fn, rom->line, name);
	return 0;
}

static int nreg_from_name(struct code_rom *rom, const char r,
				const char *name, uint8_t *num)
{
	if ( name[0] != r )
		goto barf;

	switch(name[1]) {
	case '0':
		*num = 0;
		break;
	case '1':
		*num = 1;
		break;
	default:
		goto barf;
	}

	if ( name[2] == '\0' )
		return 1;

barf:
	fprintf(stderr, "%s:%u: bad %c register, %s\n",
		rom->fn, rom->line, r, name);
	return 0;
}

static int xreg_from_name(struct code_rom *rom, const char *name, uint8_t *num)
{
	return nreg_from_name(rom, 'x', name, num);
}

static int yreg_from_name(struct code_rom *rom, const char *name, uint8_t *num)
{
	return nreg_from_name(rom, 'y', name, num);
}

static int imm_from_str(struct code_rom *rom, const char *s, uint8_t *c)
{
	unsigned long int ret;
	char *end = NULL;

	if ( *s != '$' )
		goto err;

	s++;

	ret = strtoul(s, &end, 0);

	if ( end == s || *end != '\0' || ret & ~0xff )
		goto err;

	*c = ret;
	return 1;
err:
	fprintf(stderr, "%s:%u: bad address or integer literal: %s\n",
		rom->fn, rom->line, s);
	return 0;
}

static int op_ldi(struct code_rom *rom, char *operands)
{
	char *tok[2] = {NULL, NULL};
	uint8_t reg;
	uint8_t imm;
	int n;

	n = easy_explode(operands, ',', tok, 2);
	if ( n != 2 ) {
		fprintf(stderr, "%s:%u: ldi: wrong number of arguments\n",
			rom->fn, rom->line);
		return 0;
	}

	if ( !reg_from_name(rom, tok[0], &reg) )
		return 0;
	if ( !imm_from_str(rom, tok[1], &imm) )
		return 0;

	if ( !emit_insn(rom, (1 << 4) | (1 << reg)) )
		return 0;
	if ( !emit_insn(rom, imm) )
		return 0;

	return 1;
}

static int alu_op(struct code_rom *rom, uint8_t op, char *operands)
{
	char *tok[3] = {NULL, NULL, NULL};
	uint8_t reg, x, y;
	int n;

	n = easy_explode(operands, ',', tok, 3);
	if ( n != 3 ) {
		fprintf(stderr, "%s:%u: %s: wrong number of arguments\n",
			rom->fn, rom->line, cpu_alu_name(op));
		return 0;
	}
	
	if ( !xreg_from_name(rom, tok[0], &x) )
		return 0;
	if ( !yreg_from_name(rom, tok[1], &y) )
		return 0;

	if ( !reg_from_name(rom, tok[2], &reg) )
		return 0;

	return emit_insn(rom, (1 << 7) | (x << 6) | (y << 5) | reg);
}

static int op_add(struct code_rom *rom, char *operands)
{
	return alu_op(rom, ALU_ADD, operands);
}

static int op_xor(struct code_rom *rom, char *operands)
{
	return alu_op(rom, ALU_XOR, operands);
}

static int op_and(struct code_rom *rom, char *operands)
{
	return alu_op(rom, ALU_AND, operands);
}

static int op_or(struct code_rom *rom, char *operands)
{
	return alu_op(rom, ALU_OR, operands);
}

static int op_nopx(struct code_rom *rom, char *operands)
{
	return alu_op(rom, ALU_NOPX, operands);
}

static int op_nopy(struct code_rom *rom, char *operands)
{
	return alu_op(rom, ALU_NOPY, operands);
}

static int op_halt(struct code_rom *rom, char *operands)
{
	return emit_insn(rom, 0);
}

static int dispatch_insn(struct code_rom *rom, char *insn, char *operands)
{
	static const struct {
		const char *name;
		int(*dispatch)(struct code_rom *rom, char *operands);
	}tbl[] = {
		{"ldi", op_ldi},
		{"add", op_add},
		{"xor", op_xor},
		{"and", op_and},
		{"or", op_or},
		{"nopx", op_nopx},
		{"nopy", op_nopy},
		{"halt", op_halt},
	};
	unsigned int i;

	for(i = 0; i < sizeof(tbl)/sizeof(*tbl); i++) {
		if ( !strcmp(tbl[i].name, insn) )
			return (tbl[i].dispatch)(rom, operands);
	}

	fprintf(stderr, "%s:%u: unknown instruction: %s\n",
		rom->fn, rom->line, insn);
	return 0;
}

static int add_insn(struct code_rom *rom, char *ptr)
{
	char *tok[2] = {NULL, NULL};
	int n;

	n = easy_explode(ptr, 0, tok, 2);
	if ( n < 1 )
		return 0;

	return dispatch_insn(rom, tok[0], tok[1]);
}

static int assemble(struct code_rom *rom, FILE *f)
{
	char buf[1024];
	char *ptr, *end;

	for(rom->line = 1; fgets(buf, sizeof(buf), f); rom->line++ ) {
		end = strchr(buf, '\r');
		if ( NULL == end )
			end = strchr(buf, '\n');

		if ( NULL == end ) {
			fprintf(stderr, "%s:%u: Line too long\n",
				rom->fn, rom->line);
			return 0;
		}

		*end = '\0';

		/* strip trailing whitespace */
		for(end = end - 1; isspace(*end); end--)
			*end= '\0';

		/* strip leading whitespace */
		for(ptr = buf; isspace(*ptr); ptr++)
			/* nothing */;

		if ( *ptr == '\0' || *ptr == ';' ) {
			/* empty line or comment */
			continue;
		}else if ( *end == ':' ) {
			*end = '\0';
			if ( !add_label(rom, ptr) )
				return 0;
		}else{
			if ( !add_insn(rom, ptr) )
				return 0;
		}
	}

	return 1;
}

static int write_rom(struct code_rom *rom, FILE *f)
{
	size_t sz;

	sz = fwrite(rom->code, sizeof(*rom->code), rom->addr, f);
	if ( sz != rom->addr || ferror(f) ) {
		fprintf(stderr, "%s: %s\n",
			rom->fn, strerror(errno));
		return 0;
	}

	printf("%lu insns successfuly written\n", (unsigned long)sz);
	return 1;
}

int main(int argc, char **argv)
{
	struct code_rom rom;
	FILE *in, *out;

	memset(&rom, 0, sizeof(rom));

	if ( argc != 3 ) {
		fprintf(stderr, "%s: Usage: %s [in] [out]\n", argv[0], argv[0]);
		return EXIT_FAILURE;
	}

	if (strcmp(argv[1], "-")) {
		rom.fn = argv[1];
		in = fopen(argv[1], "r");
	}else{
		in = stdin;
		rom.fn = "(stdin)";
	}

	if ( NULL == in ) {
		fprintf(stderr, "%s: %s: %s\n",
			argv[0], rom.fn, strerror(errno));
		return EXIT_FAILURE;
	}

	if ( !assemble(&rom, in) )
		return EXIT_FAILURE;

	if (strcmp(argv[2], "-")) {
		rom.fn = argv[2];
		out = fopen(argv[2], "w");
	}else{
		out = stdout;
		rom.fn = "(stdout)";
	}

	if ( !write_rom(&rom, out) )
		return EXIT_FAILURE;

	fclose(out);
	return EXIT_SUCCESS;
}
