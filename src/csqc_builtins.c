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
#include "quakedef.h"	// client.h (cls: netchan/fteprotocolextensions/state) с нужными типами
#include "pr1vm.h"
#include "csqc_client.h"	// accessor'ы к клиентскому состоянию/выводу (Фаза 5)

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
	pr1vm_t *vm = CSQCVM_Active ();
	char *cmd = CSQCVM_Str (OFS_PARM0);
	if (vm && cmd)
		CSQC_Client_RegisterCommand (cmd);
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
	char buf[2048];
	int n, i, len = 0;

	if (!vm)
		return;
	n = vm->argc;
	if (n <= 0)
		n = 1;
	buf[0] = 0;
	// Параметры PR1 — каждые 3 float-слота на аргумент (как PF_VarString,
	// pr_cmds.c: OFS_PARM0 + i*3); argc = число аргументов.
	for (i = 0; i < n && i < 16; i++)
	{
		char *s = PR1VM_GetString (vm, *(int *)&vm->globals[OFS_PARM0 + i * 3]);
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

/*
float(float property, ...) getproperty = #309
(VF_SCREENVSIZE=204 → vector (vid.width, vid.height, 0); остальные → 0.)
*/
#define CSQC_VF_SCREENVSIZE 204

static void csqc_getproperty (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float prop;
	if (!vm)
		return;
	prop = vm->globals[OFS_PARM0];
	vm->globals[OFS_RETURN] = 0;
	vm->globals[OFS_RETURN + 1] = 0;
	vm->globals[OFS_RETURN + 2] = 0;
	if ((int)prop == CSQC_VF_SCREENVSIZE)
	{
		int w = 0, h = 0;
		CSQC_Client_GetScreenSize (&w, &h);
		vm->globals[OFS_RETURN] = w;
		vm->globals[OFS_RETURN + 1] = h;
	}
}

/*
void() clearscene = #300 / void(float mask) addentities = #301 /
float(float property, ...) setproperty = #303 / void() renderscene = #304
No-op: 3D-рендер модуля не делаем (движок рисует сам), HUD — поверх.
*/
static void csqc_clearscene (void) { }
static void csqc_addentities (void) { }
static void csqc_setproperty (void) { }
static void csqc_renderscene (void) { }

/*
float(vector position, string text, vector size, vector rgb,
      float alpha, float drawflag) drawstring = #326

Рисуем строку в 2D-оверлее ezquake. Параметры PR1 — каждые 3 слова на аргумент:
pos=0..2 (vector), text=3 (string_t), size=6..8 (игнор — шрифт по умолчанию),
rgb=9..11 (0..1 → байты), alpha=12, drawflag=15.
Цвет выставляем явно (Draw_SetColor) — не зависит от scr_coloredText.
*/
static void csqc_drawstring (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	int r, gg, b;
	char *s;
	if (!vm)
		return;
	g = vm->globals;
	s = PR1VM_GetString (vm, *(int *)&g[OFS_PARM0 + 3]);
	if (!s)
		return;
	r = (int)(bound (0, g[OFS_PARM0 + 9], 1) * 255.0f + 0.5f);
	gg = (int)(bound (0, g[OFS_PARM0 + 10], 1) * 255.0f + 0.5f);
	b = (int)(bound (0, g[OFS_PARM0 + 11], 1) * 255.0f + 0.5f);
	CSQC_Client_DrawText (g[OFS_PARM0 + 0], g[OFS_PARM0 + 1], s, r, gg, b, g[OFS_PARM0 + 12]);
}

/*
float(float stnum) getstati = #330 / float(float stnum, ...) getstatf = #331
Стандартные статы 0..31 — из cl.stats; 32..127 (кастомные серверные) — 0 до
подшага «статы 32–127». Бит-выборки getstatf(stnum, firstbit, bitcount) не
используются нашим модулем — не реализованы.
*/
static float csqc_getstat_value (pr1vm_t *vm, int idx)
{
	(void)vm;
	// Стандартные статы 0..31; 32..127 (кастомные серверные) — 0 до подшага
	// «статы 32–127» (реализация доступа — в CSQC_Client_GetStat).
	return CSQC_Client_GetStat (idx);
}

static void csqc_getstati (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (!vm)
		return;
	vm->globals[OFS_RETURN] = csqc_getstat_value (vm, (int)vm->globals[OFS_PARM0]);
}

static void csqc_getstatf (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (!vm)
		return;
	vm->globals[OFS_RETURN] = csqc_getstat_value (vm, (int)vm->globals[OFS_PARM0]);
}

/*
string(string fmt, ...) sprintf = #627
Мини-форматтер (QC): %d/%i (int), %s (string), %f/%g (+ %.Nprec), %v (vector),
%%. Аргументы читаются по порядку из парам-слотов (начиная с OFS_PARM1);
число слотов не ограничиваем длиной формата (vararg-call счётчик движка
ненадёжен для vector-аргументов).
*/
static void csqc_sprintf (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char buf[2048];
	char tmp[512];
	const char *fmt, *p;
	int pn = 1;		// номер аргумента (после fmt); base = OFS_PARM0 + pn*3
	size_t o = 0;

	if (!vm)
		return;
	fmt = PR1VM_GetString (vm, *(int *)&vm->globals[OFS_PARM0]);
	if (!fmt)
		fmt = "";

	for (p = fmt; *p && o < sizeof (buf) - 1; p++)
	{
		char conv;
		int prec = -1;
		double dv;

		if (*p != '%')
		{
			buf[o++] = *p;
			continue;
		}
		p++;
		if (*p == '%')
		{
			buf[o++] = '%';
			continue;
		}
		if (*p == '.')
		{
			prec = 0;
			p++;
			while (*p >= '0' && *p <= '9')
				prec = prec * 10 + (*p++ - '0');
		}
		conv = *p;
		if (!conv)
			break;

		switch (conv)
		{
		case 'd':
		case 'i':
			if (pn < 32)
				snprintf (tmp, sizeof (tmp), "%d", (int)vm->globals[OFS_PARM0 + pn * 3]);
			else
				tmp[0] = 0;
			pn++;
			break;
		case 'f':
			dv = (pn < 32) ? (double)vm->globals[OFS_PARM0 + pn * 3] : 0;
			pn++;
			if (prec >= 0)
				snprintf (tmp, sizeof (tmp), "%.*f", prec, dv);
			else
				snprintf (tmp, sizeof (tmp), "%f", dv);
			break;
		case 'g':
			dv = (pn < 32) ? (double)vm->globals[OFS_PARM0 + pn * 3] : 0;
			pn++;
			if (prec >= 0)
				snprintf (tmp, sizeof (tmp), "%.*g", prec, dv);
			else
				snprintf (tmp, sizeof (tmp), "%g", dv);
			break;
		case 's':
			{
				int off = (pn < 32) ? *(int *)&vm->globals[OFS_PARM0 + pn * 3] : 0;
				char *s = NULL;
				static int warned = 0;
				pn++;
				if (pn - 1 < 32)
				{
					s = PR1VM_GetString (vm, off);
					// Валидация: неотрицательный offset обязан лежать в строковой
					// области модуля; отрицательные — во временных таблицах.
					if (s && off >= 0 && (unsigned)off >= (unsigned)vm->progs->numstrings)
						s = NULL;
					if (!s && !warned)
					{
						int k;
						warned = 1;
						Con_Printf ("csqc_sprintf: bad string arg (fmt=\"%s\" arg=%d off=%d argc=%d)\n",
							fmt, pn - 1, off, vm->argc);
						for (k = 0; k <= 15; k++)
							Con_Printf ("  w%d int=%d float=%g\n", k,
								*(int *)&vm->globals[OFS_PARM0 + k],
								vm->globals[OFS_PARM0 + k]);
					}
				}
				if (s)
					snprintf (tmp, sizeof (tmp), "%s", s);
				else
					tmp[0] = 0;
			}
			break;
		case 'v':
			{
				double x = (pn < 32) ? (double)vm->globals[OFS_PARM0 + pn * 3] : 0;
				double y = (pn < 32) ? (double)vm->globals[OFS_PARM0 + pn * 3 + 1] : 0;
				double z = (pn < 32) ? (double)vm->globals[OFS_PARM0 + pn * 3 + 2] : 0;
				pn++;
				snprintf (tmp, sizeof (tmp), "%g %g %g", x, y, z);
			}
			break;
		default:
			tmp[0] = conv;
			tmp[1] = 0;
			break;
		}
		{
			size_t l = strlen (tmp);
			if (o + l >= sizeof (buf))
				l = sizeof (buf) - 1 - o;
			memcpy (buf + o, tmp, l);
			o += l;
		}
	}
	buf[o] = 0;
	CSQCVM_SetRetStr (buf);
}

/*
void(string evname, string evargs, ...) sendevent = #359
(E2) Реальная запись clcfte_qcrequest(81) — wire-контракт ftew PF_cs_sendevent
(pr_csqc.c:3794) / mvdsv SV_ReadQCRequest (sv_user.c:4616):
  [byte 81] затем до 6 аргументов "[byte type][значение]", затем [byte 0
  (ev_void-терминатор)] и [string evname].
Типы: 's'=1 ev_string+string, 'f'=2 ev_float+float, 'v'=3 ev_vector+3 floats,
'i'=8 ev_integer+long (raw-bits из float-слота, как ftew G_INT). Неизвестный
символ (вкл. '\0') — break (остаток не шлём; 'e'/'u'/'F'/'I'/'p' модуль не
использует). Гварды: активный коннект + договорённый FTE_PEXT_CSQC + cl_pext_csqc
(сервер без CSQC иначе дропает клиента, sv_user.c:5146).
*/
#define CSQC_EV_VOID	0
#define CSQC_EV_STRING	1
#define CSQC_EV_FLOAT	2
#define CSQC_EV_VECTOR	3
#define CSQC_EV_INTEGER	8

static void csqc_sendevent (void)
{
	extern cvar_t cl_pext_csqc;
	pr1vm_t *vm = CSQCVM_Active ();
	const char *evname, *argtypes;
	char c;
	int i;

	if (!vm)
		return;
	if (cls.state != ca_active)
		return;
	if (!cl_pext_csqc.value)
		return;
#ifdef PROTOCOL_VERSION_FTE
	if (!(cls.fteprotocolextensions & FTE_PEXT_CSQC))
		return;
#endif

	evname = CSQCVM_Str (OFS_PARM0);
	argtypes = CSQCVM_Str (OFS_PARM1);
	if (!evname || !argtypes)
		return;

	MSG_WriteByte (&cls.netchan.message, clcfte_qcrequest);

	for (i = 0; i < 6; i++)
	{
		int base = OFS_PARM2 + i * 3;
		c = argtypes[i];
		if (c == 's')
		{
			char *s = PR1VM_GetString (vm, *(int *)&vm->globals[base]);
			MSG_WriteByte (&cls.netchan.message, CSQC_EV_STRING);
			MSG_WriteString (&cls.netchan.message, s ? s : "");
		}
		else if (c == 'f')
		{
			MSG_WriteByte (&cls.netchan.message, CSQC_EV_FLOAT);
			MSG_WriteFloat (&cls.netchan.message, vm->globals[base]);
		}
		else if (c == 'v')
		{
			MSG_WriteByte (&cls.netchan.message, CSQC_EV_VECTOR);
			MSG_WriteFloat (&cls.netchan.message, vm->globals[base + 0]);
			MSG_WriteFloat (&cls.netchan.message, vm->globals[base + 1]);
			MSG_WriteFloat (&cls.netchan.message, vm->globals[base + 2]);
		}
		else if (c == 'i')
		{
			MSG_WriteByte (&cls.netchan.message, CSQC_EV_INTEGER);
			MSG_WriteLong (&cls.netchan.message, *(int *)&vm->globals[base]);
		}
		else
			break;
	}

	MSG_WriteByte (&cls.netchan.message, CSQC_EV_VOID);
	MSG_WriteString (&cls.netchan.message, evname);
}

/*
S1 read*-минимум: читают из текущего сетевого сообщения (как FTE), для Remove
entnum подставляется через CSQC_Client_ReadEntityNum (временный путь).
*/
static void csqc_readbyte (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadByte ();
}

/*
float() readchar = #361
(S2) Байт со знаком из текущего сетевого сообщения.
*/
static void csqc_readchar (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadChar ();
}

static void csqc_readshort (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadShort ();
}

static void csqc_readlong (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadLong ();
}

/*
float() readcoord = #364 / string() readstring = #366
(E1/S2) Координата/строка из текущего сетевого сообщения (нужны cgamepacket-echo
и типизированному 76-payload).
*/
static void csqc_readcoord (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadCoord ();
}

/*
float() readangle = #365
(S2) Угол из текущего сетевого сообщения.
*/
static void csqc_readangle (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadAngle ();
}

static void csqc_readstring (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *s;
	if (!vm)
		return;
	s = MSG_ReadString ();
	PR1VM_SetString (vm, (string_t *)&vm->globals[OFS_RETURN], s);
}

/*
float() readfloat = #367
(S2) Полный float из текущего сетевого сообщения.
*/
static void csqc_readfloat (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadFloat ();
}

static void csqc_readentitynum (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	int e;
	if (!vm)
		return;
	e = CSQC_Client_ReadEntityNum ();
	if (e < 0)
		e = MSG_ReadShort ();
	vm->globals[OFS_RETURN] = e;
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

	// P2.3 — визуальный слой B (2D-оверлей; сетевая часть B — позже).
	PR1VM_RegisterBuiltin (vm, 300, (builtin_t)csqc_clearscene);
	PR1VM_RegisterBuiltin (vm, 301, (builtin_t)csqc_addentities);
	PR1VM_RegisterBuiltin (vm, 303, (builtin_t)csqc_setproperty);
	PR1VM_RegisterBuiltin (vm, 304, (builtin_t)csqc_renderscene);
	PR1VM_RegisterBuiltin (vm, 309, (builtin_t)csqc_getproperty);
	PR1VM_RegisterBuiltin (vm, 326, (builtin_t)csqc_drawstring);
	PR1VM_RegisterBuiltin (vm, 330, (builtin_t)csqc_getstati);
	PR1VM_RegisterBuiltin (vm, 331, (builtin_t)csqc_getstatf);
	PR1VM_RegisterBuiltin (vm, 359, (builtin_t)csqc_sendevent);
	PR1VM_RegisterBuiltin (vm, 627, (builtin_t)csqc_sprintf);

	// S1 read*-минимум (полный набор #360–368 — S2).
	PR1VM_RegisterBuiltin (vm, 360, (builtin_t)csqc_readbyte);
	PR1VM_RegisterBuiltin (vm, 361, (builtin_t)csqc_readchar);
	PR1VM_RegisterBuiltin (vm, 362, (builtin_t)csqc_readshort);
	PR1VM_RegisterBuiltin (vm, 363, (builtin_t)csqc_readlong);
	PR1VM_RegisterBuiltin (vm, 364, (builtin_t)csqc_readcoord);
	PR1VM_RegisterBuiltin (vm, 365, (builtin_t)csqc_readangle);
	PR1VM_RegisterBuiltin (vm, 366, (builtin_t)csqc_readstring);
	PR1VM_RegisterBuiltin (vm, 367, (builtin_t)csqc_readfloat);
	PR1VM_RegisterBuiltin (vm, 368, (builtin_t)csqc_readentitynum);
}

#endif // !CLIENTONLY
