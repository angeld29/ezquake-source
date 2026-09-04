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

/*
string(string s1, optional string s2, ...) strcat = #115
(P2.2) Конкатенация переданных строк (до vm->argc аргументов).
*/
static void csqc_strcat (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	static char buf[2048];
	int n, i, len = 0;

	if (!vm)
		return;
	n = vm->argc;
	if (n <= 0)
		n = 1;
	buf[0] = 0;
	for (i = 0; i < n && i < 8; i++)
	{
		char *s = PR1VM_GetString (vm, *(int *)&vm->globals[OFS_PARM0 + i]);
		if (s)
			len += snprintf (buf + len, sizeof (buf) - len, "%s", s);
		if (len >= (int)sizeof (buf) - 1)
			break;
	}
	CSQCVM_SetRetStr (buf);
}

/*
float(string s1, string sub, optional float startidx) strstrofs = #221
(P2.2) Возвращает позицию подстроки (0-based) или -1.
*/
static void csqc_strstrofs (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *hay, *needle, *p;
	int start;

	if (!vm)
		return;
	hay = PR1VM_GetString (vm, *(int *)&vm->globals[OFS_PARM0]);
	needle = PR1VM_GetString (vm, *(int *)&vm->globals[OFS_PARM1]);
	start = (int)vm->globals[OFS_PARM2];
	if (!hay || !needle)
	{
		vm->globals[OFS_RETURN] = -1;
		return;
	}
	if (start < 0)
		start = 0;
	if (start > (int)strlen (hay))
		start = (int)strlen (hay);
	p = strstr (hay + start, needle);
	vm->globals[OFS_RETURN] = p ? (p - hay) : -1;
}

void CSQCVM_RegisterBuiltins (pr1vm_t *vm)
{
	PR1VM_RegisterBuiltin (vm, 25, (builtin_t)csqc_dprint);
	PR1VM_RegisterBuiltin (vm, 26, (builtin_t)csqc_ftos);
	PR1VM_RegisterBuiltin (vm, 115, (builtin_t)csqc_strcat);
	PR1VM_RegisterBuiltin (vm, 221, (builtin_t)csqc_strstrofs);
	PR1VM_RegisterBuiltin (vm, 352, (builtin_t)csqc_registercommand);
	PR1VM_RegisterBuiltin (vm, 441, (builtin_t)csqc_tokenize);
	PR1VM_RegisterBuiltin (vm, 442, (builtin_t)csqc_argv);
}

#endif // !CLIENTONLY
