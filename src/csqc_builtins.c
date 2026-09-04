/*
csqc_builtins.c -- клиентские builtins PR1VM (наш csprogs.dat, слой C, P2.x).

Builtins для клиентского инстанса: номера — baked из TF2003 csdefs.qc (= #N),
аргументы читаются из vm->globals[OFS_PARM0..], возврат в OFS_RETURN,
строки — через PR1VM_GetString/PR1VM_SetString (S4) на активном инстансе.

P2.1: dprint/ftos/registercommand/tokenize/argv. Layers A/B are added here as
implemented (drawstring/getstatf/read builtins/sprintf are P2.2/P2.3).
*/

#ifndef CLIENTONLY
#include "qwsvdef.h"
#include "pr1vm.h"

static pr1vm_t *CSQCVM_Active (void)
{
	return PR1VM_Active ();
}

static char *CSQCVM_Str (int ofs)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (!vm)
		return NULL;
	return PR1VM_GetString (vm, *(int *)&vm->globals[ofs]);
}

static void CSQCVM_SetRetStr (char *s)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		PR1VM_SetString (vm, (string_t *)&vm->globals[OFS_RETURN], s);
}

/*
void(string s, ...) dprint = #25
*/
static void csqc_dprint (void)
{
	char *s = CSQCVM_Str (OFS_PARM0);
	if (s)
		Con_Printf ("%s", s);
}

/*
string(float val) ftos = #26
*/
static void csqc_ftos (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char buf[32];
	if (!vm)
		return;
	snprintf (buf, sizeof (buf), "%d", (int)vm->globals[OFS_PARM0]);
	CSQCVM_SetRetStr (buf);
}

/*
void(string cmdname) registercommand = #352
*/
static void csqc_registercommand (void)
{
	// Пока no-op: реальная привязка команд клиента — в мини-каркасе Фазы 5.
}

/*
float(string s) tokenize = #441
*/
static void csqc_tokenize (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *s = CSQCVM_Str (OFS_PARM0);
	if (!vm)
		return;
	if (s)
		Cmd_TokenizeString (s);
	vm->globals[OFS_RETURN] = Cmd_Argc ();
}

/*
string(float n) argv = #442
*/
static void csqc_argv (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	int n;
	if (!vm)
		return;
	n = (int)vm->globals[OFS_PARM0];
	CSQCVM_SetRetStr (Cmd_Argv (n));
}

void CSQCVM_RegisterBuiltins (pr1vm_t *vm)
{
	PR1VM_RegisterBuiltin (vm, 25, (builtin_t)csqc_dprint);
	PR1VM_RegisterBuiltin (vm, 26, (builtin_t)csqc_ftos);
	PR1VM_RegisterBuiltin (vm, 352, (builtin_t)csqc_registercommand);
	PR1VM_RegisterBuiltin (vm, 441, (builtin_t)csqc_tokenize);
	PR1VM_RegisterBuiltin (vm, 442, (builtin_t)csqc_argv);
}

#endif // !CLIENTONLY
