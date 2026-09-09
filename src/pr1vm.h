/*
pr1vm.h -- PR1 engine instance (ezquake PR1 refactor, S1)

The PR1 interpreter is moved from global state to per-instance state:
execution state (call/local-variable stacks, xfunction/xstatement) lives in
pr1vm_t. The "module" globals (progs/pr_functions/...) are shared between
PR1 and PR2 in ezquake and are read by external code (sv_*.c) — they stay as
they are; pr1vm_t mirrors them through PR1VM_BindServer() and they are fully
moved onto the instance in slices S2-S4.

PR1 runs under the CLIENTONLY gate (server build). The API for a client
instance is added together with the loader/wiring (S3/S5).
*/

#ifndef PR1VM_H
#define PR1VM_H

#include "progs.h"	// dprograms_t/dstatement_t/..., edict_t, globalvars_t

#define PR1VM_MAX_STACK	32
#define PR1VM_LOCALSTACK	2048

// Per-instance temp-string ring of the client instance (slot count and size
// match the server MAX_PR_STRINGS/MAX_PR_STRING_SIZE in pr_cmds.c). String
// builtin results are deep-copied into ring slots (PR1VM_SetString), so each
// call gets its own buffer: no aliasing/self-overlap (cf. FTE
// PR_AllocTempString). A string lives until its slot is overwritten by later
// calls; cross-frame QC text globals are not guaranteed (later: FTE mechanism
// — temp pool + GC, see PR1VM_SetString).
#define PR1VM_TEMP_STRINGS		64
#define PR1VM_TEMP_STRING_SIZE	2048

typedef struct pr1vm_s pr1vm_t;

typedef struct
{
	int			s;			// statement (return address)
	dfunction_t	*f;			// function
} pr1vm_stack_t;

struct pr1vm_s
{
	// Mirrors of the "module" globals (server = shared symbols via BindServer;
	// a client instance gets its own buffers in S3).
	dprograms_t		*progs;
	dfunction_t		*functions;
	ddef_t			*fielddefs;
	ddef_t			*globaldefs;
	dstatement_t	*statements;
	char			*strings;
	globalvars_t	*global_struct;
	float			*globals;
	int				edict_size;		// bytes per entvars block

	// Edict model (server: sv.edicts / sv.game_edicts). S5b: state mirrors sv.state.
	edict_t			*edicts;
	int				num_edicts;
	int				max_edicts;
	int				state;
	void			*game_edicts;	// entvars base for STOREP_*/EDICT_TO_PROG

	// Module dialect field-offset map (ADR 0017 P2): NULL = raw/identity
	// (classic QW, FTE CSQC csprogs), otherwise a remap table (NQ progs).
	// Depends only on the instance/module type; set on (re)load.
	const int		*fieldofs_patch;

	// Execution state (moved from pr_exec.c globals).
	pr1vm_stack_t	stack[PR1VM_MAX_STACK];
	int				depth;
	int				localstack[PR1VM_LOCALSTACK];
	int				localstack_used;
	dfunction_t		*xfunction;
	int				xstatement;

	// Builtin container (S5): dispatch by number -first_statement.
	int				numbuiltins;
	builtin_t		*builtins;
	// Current call (S5): arg count + trace flag.
	int				argc;
	qbool			trace;

	// Client (per-instance) dynamic strings. Unused by the server instance:
	// it delegates to the global tables (PR1_GetString/...), since they are
	// read by PR2 and sv_*.
	char			*strtbl[MAX_PRSTR];
	char			*newstrtbl[MAX_PRSTR];
	int				numstr;

	// Temp-string ring (see PR1VM_TEMP_* above): deep-copy buffers for
	// client-instance string builtin results + write index.
	char			tmpstr[PR1VM_TEMP_STRINGS][PR1VM_TEMP_STRING_SIZE];
	int				tmpstr_cur;

	// Host interface (S4): callbacks receive a ready string.
	void (*host_error)(pr1vm_t *vm, const char *msg);
	void (*host_print)(pr1vm_t *vm, const char *msg);
	void *host_udata;
};

// Active instance (the one PR1 is currently executing inside; NULL outside a call).
pr1vm_t *PR1VM_Active(void);

// ADR 0019: true while a NON-server instance (the client CSQC-VM) runs.
// In this state do not call the global server helpers (PR1_Get/SetString etc.)
// — the client works with its own per-instance strings.
qbool PR1VM_ClientContext(void);

// Server instance (sv_pr1vm) — default target of the PR_* wrappers.
pr1vm_t *PR1VM_Server(void);

// Zero the instance and (for the server) fill mirrors from shared globals and sv.*.
void PR1VM_Reset(pr1vm_t *vm);
void PR1VM_BindServer(pr1vm_t *vm);
// S6: detach the instance from the module — clear lump mirrors and exec state,
// keeping host callbacks (re)set by BindServer/client.
void PR1VM_UnLoad(pr1vm_t *vm);

// Load: byte-swap header+lumps and fill the instance mirrors (without
// version/CRC validation — done by the server wrapper).
void PR1VM_LoadData(pr1vm_t *vm, dprograms_t *hdr);
// Server: instance mirrors -> shared "module" globals (read by PR2/sv_*.c).
void PR1VM_CommitServer(pr1vm_t *vm);

// Resolve by name on the instance (unlike ED_Find* — against vm mirrors).
dfunction_t *PR1VM_FindFunction(pr1vm_t *vm, const char *name);
int PR1VM_FindGlobal(pr1vm_t *vm, const char *name);
char *PR1VM_GetString(pr1vm_t *vm, int num);
void PR1VM_SetString(pr1vm_t *vm, string_t *address, char *s);

// Register a builtin by number (client/any instance table). The table grows to
// num+1 slots; unfilled slots = NULL (dispatcher errors out).
void PR1VM_RegisterBuiltin(pr1vm_t *vm, int num, builtin_t fn);

// S4 debug: provoke PR_RunError on the server instance (pr1vm_test_error).
void PR1VM_TestError_f(void);

int  PR1VM_EnterFunction(pr1vm_t *vm, dfunction_t *f);
int  PR1VM_LeaveFunction(pr1vm_t *vm);
void PR1VM_ExecuteProgram(pr1vm_t *vm, func_t fnum);

#endif /* PR1VM_H */
