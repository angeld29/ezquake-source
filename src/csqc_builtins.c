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
#include "keys.h"		// Key_KeynumToString/Key_StringToKeynum (Слой D шаг 3)
#include "qsound.h"		// S_LocalSoundWithVol (C3.1 #177)
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
float(string varname) cvar = #45

Возвращает значение cvar движка по имени (нет такого cvar — 0). Нужно модулю
для диагностических переключателей (напр. csqc_inputdebug) и конфига.
*/
static void csqc_cvar (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *name = CSQCVM_Str (OFS_PARM0);
	vm->globals[OFS_RETURN] = (vm && name && name[0]) ? Cvar_Value (name) : 0;
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

Полный read-паритет FTE (PF_R_GetViewFlag, pr_csqc.c): чтение текущего состояния
рендера движка (r_refdef/cl/vid), а не «значения, поставленные модулем #303».
Числа VF_* — из TF2003-qvm/csqc/csdefs.qc (346-375). set-флаги (DRAWWORLD и пр.)
в getter-списке FTE отсутствуют → default 0; у нас тоже 0. 3D-сцена (#303/#304) —
вне скоупа (ADR 0018).
*/
#define CSQC_VF_MIN		1	// viewport top-left (x,y)
#define CSQC_VF_MIN_X		2
#define CSQC_VF_MIN_Y		3
#define CSQC_VF_SIZE		4	// viewport width/height
#define CSQC_VF_SIZE_X		5
#define CSQC_VF_SIZE_Y		6
#define CSQC_VF_VIEWPORT	7	// (width, height)
#define CSQC_VF_FOV		8	// (fov_x, fov_y)
#define CSQC_VF_FOV_X		9
#define CSQC_VF_FOV_Y		10
#define CSQC_VF_ORIGIN		11
#define CSQC_VF_ORIGIN_X	12
#define CSQC_VF_ORIGIN_Y	13
#define CSQC_VF_ORIGIN_Z	14
#define CSQC_VF_ANGLES		15
#define CSQC_VF_ANGLES_X	16
#define CSQC_VF_ANGLES_Y	17
#define CSQC_VF_ANGLES_Z	18
#define CSQC_VF_CL_VIEWANGLES	33
#define CSQC_VF_CL_VIEWANGLES_X	34
#define CSQC_VF_CL_VIEWANGLES_Y	35
#define CSQC_VF_CL_VIEWANGLES_Z	36
#define CSQC_VF_AFOV		203
#define CSQC_VF_SCREENVSIZE	204
#define CSQC_VF_SCREENPSIZE	205

static void csqc_getproperty (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float prop;
	float *r;

	if (!vm)
		return;
	prop = vm->globals[OFS_PARM0];
	r = &vm->globals[OFS_RETURN];
	r[0] = r[1] = r[2] = 0;
	if (cls.state != ca_active)
		return;

	switch ((int)prop)
	{
	case CSQC_VF_SCREENVSIZE:
	case CSQC_VF_SCREENPSIZE:
		// «виртуальный»/«физический» размер; в ezquake без OS-скейла — одно и то же.
		r[0] = vid.width;
		r[1] = vid.height;
		break;
	case CSQC_VF_FOV:
		r[0] = r_refdef.fov_x;
		r[1] = r_refdef.fov_y;
		break;
	case CSQC_VF_FOV_X:
		r[0] = r_refdef.fov_x;
		break;
	case CSQC_VF_FOV_Y:
		r[0] = r_refdef.fov_y;
		break;
	case CSQC_VF_AFOV:
		// FTE: r_refdef.afov; в ezquake его нет — приближённо cvar fov.
		r[0] = Cvar_Value ("fov");
		break;
	case CSQC_VF_ORIGIN:
		VectorCopy (r_refdef.vieworg, r);
		break;
	case CSQC_VF_ORIGIN_X:
		r[0] = r_refdef.vieworg[0];
		break;
	case CSQC_VF_ORIGIN_Y:
		r[0] = r_refdef.vieworg[1];
		break;
	case CSQC_VF_ORIGIN_Z:
		r[0] = r_refdef.vieworg[2];
		break;
	case CSQC_VF_ANGLES:
		VectorCopy (r_refdef.viewangles, r);
		break;
	case CSQC_VF_ANGLES_X:
		r[0] = r_refdef.viewangles[0];
		break;
	case CSQC_VF_ANGLES_Y:
		r[0] = r_refdef.viewangles[1];
		break;
	case CSQC_VF_ANGLES_Z:
		r[0] = r_refdef.viewangles[2];
		break;
	case CSQC_VF_CL_VIEWANGLES:
		VectorCopy (cl.viewangles, r);
		break;
	case CSQC_VF_CL_VIEWANGLES_X:
		r[0] = cl.viewangles[0];
		break;
	case CSQC_VF_CL_VIEWANGLES_Y:
		r[0] = cl.viewangles[1];
		break;
	case CSQC_VF_CL_VIEWANGLES_Z:
		r[0] = cl.viewangles[2];
		break;
	case CSQC_VF_VIEWPORT:	// FTE: grect.width/height
		r[0] = r_refdef.vrect.width;
		r[1] = r_refdef.vrect.height;
		break;
	case CSQC_VF_MIN:
		r[0] = r_refdef.vrect.x;
		r[1] = r_refdef.vrect.y;
		break;
	case CSQC_VF_MIN_X:
		r[0] = r_refdef.vrect.x;
		break;
	case CSQC_VF_MIN_Y:
		r[0] = r_refdef.vrect.y;
		break;
	case CSQC_VF_SIZE:
		r[0] = r_refdef.vrect.width;
		r[1] = r_refdef.vrect.height;
		break;
	case CSQC_VF_SIZE_X:
		r[0] = r_refdef.vrect.width;
		break;
	case CSQC_VF_SIZE_Y:
		r[0] = r_refdef.vrect.height;
		break;
	default:
		// set-флаги (DRAWWORLD/PERSPECTIVE/...) и без аналога/DP-legacy — 0
		// (в FTE getter-списка нет, default возвращает 0).
		break;
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
	float scale;
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
	// Слой D шаг 2: size.x -> scale (8px ячейка FTE); 0 => 1.
	scale = (g[OFS_PARM0 + 6] > 0) ? g[OFS_PARM0 + 6] / 8.0f : 1;
	CSQC_Client_DrawText (g[OFS_PARM0 + 0], g[OFS_PARM0 + 1], s, r, gg, b, g[OFS_PARM0 + 12], scale);
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

// ---------------------------------------------------------------- Слой D, шаг 1
// 2D-графика. Раскладка параметров — 3-словные ячейки от OFS_PARM0 (см.
// docs/ezquake_csqc_client_layerd_2d_plan.md §ABI). Возвраты draw*/drawcharacter = 0.

/*
float(vector position, float character, vector size, vector rgb, float alpha,
     optional float drawflag) drawcharacter = #320
*/
static void csqc_drawcharacter (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	float scale;
	if (!vm)
		return;
	g = vm->globals;
	scale = (g[OFS_PARM0 + 6] > 0) ? g[OFS_PARM0 + 6] / 8.0f : 1;
	CSQC_Client_DrawCharacter (g[OFS_PARM0 + 0], g[OFS_PARM0 + 1], (int)g[OFS_PARM0 + 3],
		(int)(bound (0, g[OFS_PARM0 + 9], 1) * 255.0f + 0.5f),
		(int)(bound (0, g[OFS_PARM0 + 10], 1) * 255.0f + 0.5f),
		(int)(bound (0, g[OFS_PARM0 + 11], 1) * 255.0f + 0.5f),
		g[OFS_PARM0 + 12], scale);
	vm->globals[OFS_RETURN] = 0;
}

/*
float(vector position, string pic, vector size, vector rgb, float alpha,
     optional float drawflag) drawpic = #322
*/
static void csqc_drawpic (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	char *name;
	if (!vm)
		return;
	g = vm->globals;
	name = PR1VM_GetString (vm, *(int *)&g[OFS_PARM0 + 3]);
	if (name)
		CSQC_Client_DrawPic (g[OFS_PARM0 + 0], g[OFS_PARM0 + 1],
			g[OFS_PARM0 + 6], g[OFS_PARM0 + 7], name,
			(int)(bound (0, g[OFS_PARM0 + 9], 1) * 255.0f + 0.5f),
			(int)(bound (0, g[OFS_PARM0 + 10], 1) * 255.0f + 0.5f),
			(int)(bound (0, g[OFS_PARM0 + 11], 1) * 255.0f + 0.5f),
			g[OFS_PARM0 + 12]);
	vm->globals[OFS_RETURN] = 0;
}

/*
void(vector pos, vector sz, string pic, vector srcpos, vector srcsz, vector rgb,
     float alpha, optional float drawflag) drawsubpic = #328
*/
static void csqc_drawsubpic (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	char *name;
	if (!vm)
		return;
	g = vm->globals;
	name = PR1VM_GetString (vm, *(int *)&g[OFS_PARM0 + 6]);
	if (name)
		CSQC_Client_DrawSubPic (g[OFS_PARM0 + 0], g[OFS_PARM0 + 1],
			g[OFS_PARM0 + 3], g[OFS_PARM0 + 4], name,
			g[OFS_PARM0 + 9], g[OFS_PARM0 + 10], g[OFS_PARM0 + 12], g[OFS_PARM0 + 13],
			(int)(bound (0, g[OFS_PARM0 + 15], 1) * 255.0f + 0.5f),
			(int)(bound (0, g[OFS_PARM0 + 16], 1) * 255.0f + 0.5f),
			(int)(bound (0, g[OFS_PARM0 + 17], 1) * 255.0f + 0.5f),
			g[OFS_PARM0 + 18]);
}

/*
float(vector position, vector size, vector rgb, float alpha,
     optional float drawflag) drawfill = #323
*/
static void csqc_drawfill (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	if (!vm)
		return;
	g = vm->globals;
	CSQC_Client_DrawFill (g[OFS_PARM0 + 0], g[OFS_PARM0 + 1],
		g[OFS_PARM0 + 3], g[OFS_PARM0 + 4],
		(int)(bound (0, g[OFS_PARM0 + 6], 1) * 255.0f + 0.5f),
		(int)(bound (0, g[OFS_PARM0 + 7], 1) * 255.0f + 0.5f),
		(int)(bound (0, g[OFS_PARM0 + 8], 1) * 255.0f + 0.5f),
		g[OFS_PARM0 + 9]);
	vm->globals[OFS_RETURN] = 0;
}

/*
void(float width, vector pos1, vector pos2, vector rgb, float alpha,
     optional float drawflag) drawline = #315
*/
static void csqc_drawline (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	if (!vm)
		return;
	g = vm->globals;
	CSQC_Client_DrawLine (g[OFS_PARM0 + 3], g[OFS_PARM0 + 4], g[OFS_PARM0 + 6], g[OFS_PARM0 + 7],
		g[OFS_PARM0 + 0],
		(int)(bound (0, g[OFS_PARM0 + 9], 1) * 255.0f + 0.5f),
		(int)(bound (0, g[OFS_PARM0 + 10], 1) * 255.0f + 0.5f),
		(int)(bound (0, g[OFS_PARM0 + 11], 1) * 255.0f + 0.5f),
		g[OFS_PARM0 + 12]);
}

/*
float(string text, float usecolours, optional vector fontsize) stringwidth = #327
*/
static void csqc_stringwidth (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *text;
	if (!vm)
		return;
	text = PR1VM_GetString (vm, *(int *)&vm->globals[OFS_PARM0]);
	vm->globals[OFS_RETURN] = CSQC_Client_StringWidth (text ? text : "",
		vm->globals[OFS_PARM0 + 3] != 0, vm->globals[OFS_PARM0 + 6]);
}

/*
string(string name, optional float trywad) precache_pic = #317
Возвращает name, если пикча загрузилась (trywad игнорируется), иначе "".
*/
static void csqc_precache_pic (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *name;
	if (!vm)
		return;
	name = CSQCVM_Str (OFS_PARM0);
	if (name && CSQC_Client_PrecachePic (name))
		CSQCVM_SetRetStr (name);
	else
		CSQCVM_SetRetStr ("");
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
S1 read*-минимум: читают из текущего сетевого сообщения (как FTE). Remove
больше не через стрим — identity модуль берёт из self.entnum (ADR 0017 P2).
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
	if (vm)
		vm->globals[OFS_RETURN] = MSG_ReadShort ();
}

/*
Слой D шаг 3 — ввод/интерфейс builtins. Отклонение от FTE: #340/#341 работают во
внутреннем keynum-домене ezquake (K_*), без MP_Translate QC<->внутренние коды —
модуль делает round-trip по именам клавиш, домен консистентен.

string(float keynum) keynumtostring = #340
Возвращает имя клавиши для внутреннего keynum-домена ezquake (как bind/unbind).
Key_KeynumToString возвращает статический буфер или имя из таблицы — CSQCVM_SetRetStr
глубоко копирует в temp-ring инстанса.
*/
static void csqc_keynumtostring (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (!vm)
		return;
	CSQCVM_SetRetStr (Key_KeynumToString ((int)vm->globals[OFS_PARM0]));
}

/*
float(string keyname) stringtokeynum = #341
Возвращает keynum по имени клавиши; пустая строка/нет такого имени -> -1
(Key_StringToKeynum и так возвращает -1).
*/
static void csqc_stringtokeynum (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *name = CSQCVM_Str (OFS_PARM0);
	if (!vm)
		return;
	vm->globals[OFS_RETURN] = name ? Key_StringToKeynum (name) : -1;
}

/*
float() isdemo = #349
0 — не демо; 1 — обычное демо; 2 — MVD/QTV-просмотр (cls.mvdplayback: 1=MVD, 2=QTV).
Семантика совпадает с FTE PF_cl_playingdemo (pr_clcmd.c: DPB_NONE=0, DPB_MVD=2, иначе 1).
*/
static void csqc_isdemo (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (!vm)
		return;
	vm->globals[OFS_RETURN] = cls.mvdplayback ? 2 : (cls.demoplayback ? 1 : 0);
}

/*
string(string key) serverkey = #354
Значение ключа из cl.serverinfo; нет ключа -> "" (Info_ValueForKey уже возвращает "").
*/
static void csqc_serverkey (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *key = CSQCVM_Str (OFS_PARM0);
	if (!vm)
		return;
	CSQCVM_SetRetStr (Info_ValueForKey (cl.serverinfo, key ? key : ""));
}

/*
string(float playernum, string keyname) getplayerkeyvalue = #348
Значения scoreboard/userinfo игрока (cl.players[pnum]). Числовые ключи
frags/ping/userid/spectator — форматированием; name/team/topcolor/bottomcolor и
прочие — из userinfo (как FTE PF_cs_getplayerkey_internal, pr_csqc.c:4344).
Пустой слот / вне [0, MAX_CLIENTS) -> "" (пустая строка). Отклонение: pnum<0
(scoreboard-индекс fragsort) не поддержан -> "" (roadmap A6).
*/
/*
void(float sens) setsensitivityscaler = #346
Временный множитель чувствительности мыши (зум-аналог FTE PF_cs_setsensitivityscaler,
in_sensitivityscale). Значение применяет in_sdl2.c к sensitivity в игровом ветвлении.
*/
static void csqc_setsensitivityscaler (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (!vm)
		return;
	CSQC_Client_SetSensitivityScale (vm->globals[OFS_PARM0]);
}

/*
float(float inputsequencenum) getinputstate = #345
Заполняет input_* глобалы из локальной истории отправленных usercmd (C1.3).
Отличие от FTE: QW не эхает подтверждение движения — история локальная
(последние CSQC_INHIST команд от CL_SendCmd); возврат 0, если seq вне истории.
*/
static void csqc_getinputstate (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	unsigned int seq;
	if (!vm)
		return;
	seq = (unsigned int)vm->globals[OFS_PARM0];
	vm->globals[OFS_RETURN] = CSQC_Client_ApplyInput (seq);
}

/*
void(entity ent) runstandardplayerphysics = #347
Гоняет клиентскую стандартную физику игрока на сущности из её полей + последнего
usercmd (C1.4, минимум-паритет по cl_pred-пути). Полная предикция — C5.
*/
static void csqc_runstandardplayerphysics (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	int entnum;
	if (!vm)
		return;
	entnum = (int)vm->globals[OFS_PARM0];
	if (vm->edict_size > 0)
		entnum /= vm->edict_size;
	CSQC_Client_RunPlayerPhysics (entnum);
}

/*
entity(float entnum) edict_num = #459
C2.1: entity-значение по номеру (N*edict_size), как self в SetEntityContext.
Вне диапазона арены -> 0 (world).
*/
static void csqc_edict_num (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	int entnum;
	if (!vm)
		return;
	entnum = (int)vm->globals[OFS_PARM0];
	if (entnum < 0 || vm->max_edicts <= 0 || entnum >= vm->max_edicts)
	{
		vm->globals[OFS_RETURN] = 0;
		return;
	}
	vm->globals[OFS_RETURN] = entnum * vm->edict_size;
}

/*
C2.2 — string-buffers #460-469 (DP). Хранилище в csqc_client (deep-copy);
builtins — тонкие обёртки (ABI i*3, возвраты строк через CSQCVM_SetRetStr).
*/
static int CSQCVM_ArgInt (int idx)
{
	pr1vm_t *vm = CSQCVM_Active ();
	return (vm && vm->argc > idx) ? (int)vm->globals[OFS_PARM0 + idx * 3] : 0;
}

static char *CSQCVM_ArgStr (int idx)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (!vm || vm->argc <= idx)
		return NULL;
	return PR1VM_GetString (vm, *(int *)&vm->globals[OFS_PARM0 + idx * 3]);
}

// strbuf() buf_create = #460
static void csqc_buf_create (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = CSQC_Client_BufCreate ();
}

// void(strbuf bufhandle) buf_del = #461
static void csqc_buf_del (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		CSQC_Client_BufDel (CSQCVM_ArgInt (0));
}

// float(strbuf bufhandle) buf_getsize = #462
static void csqc_buf_getsize (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		vm->globals[OFS_RETURN] = CSQC_Client_BufGetSize (CSQCVM_ArgInt (0));
}

// void(strbuf bufhandle_from, strbuf bufhandle_to) buf_copy = #463
static void csqc_buf_copy (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		CSQC_Client_BufCopy (CSQCVM_ArgInt (0), CSQCVM_ArgInt (1));
}

// void(strbuf bufhandle, float sortprefixlen, float backward) buf_sort = #464
static void csqc_buf_sort (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		CSQC_Client_BufSort (CSQCVM_ArgInt (0), CSQCVM_ArgInt (1), CSQCVM_ArgInt (2) != 0);
}

// string(strbuf bufhandle, string glue) buf_implode = #465
static void csqc_buf_implode (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char buf[8192];
	char *glue;
	if (!vm)
		return;
	glue = CSQCVM_ArgStr (1);
	if (!CSQC_Client_BufImplode (CSQCVM_ArgInt (0), glue ? glue : "", buf, sizeof (buf)))
		buf[0] = 0;
	CSQCVM_SetRetStr (buf);
}

// string(strbuf bufhandle, float string_index) bufstr_get = #466
static void csqc_bufstr_get (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char buf[2048];
	if (!vm)
		return;
	if (!CSQC_Client_BufGet (CSQCVM_ArgInt (0), CSQCVM_ArgInt (1), buf, sizeof (buf)))
		buf[0] = 0;
	CSQCVM_SetRetStr (buf);
}

// void(strbuf bufhandle, float string_index, string str) bufstr_set = #467
static void csqc_bufstr_set (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *s;
	if (!vm)
		return;
	s = CSQCVM_ArgStr (2);
	CSQC_Client_BufSet (CSQCVM_ArgInt (0), CSQCVM_ArgInt (1), s ? s : "");
}

// float(strbuf bufhandle, string str, float order) bufstr_add = #468
static void csqc_bufstr_add (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	char *s;
	if (!vm)
		return;
	s = CSQCVM_ArgStr (1);
	vm->globals[OFS_RETURN] = CSQC_Client_BufAdd (CSQCVM_ArgInt (0), s ? s : "",
		CSQCVM_ArgInt (2));
}

// void(strbuf bufhandle, float string_index) bufstr_free = #469
static void csqc_bufstr_free (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	if (vm)
		CSQC_Client_BufFree (CSQCVM_ArgInt (0), CSQCVM_ArgInt (1));
}

/*
C3.1.
void(string soundname, optional float channel, optional float volume) localsound = #177
FTE PF_cl_localsound (pr_clcmd.c:1059) = S_LocalSound2(name, chan, vol): local-звук.
ezquake: S_LocalSoundWithVol (snd_main.c:1085, precache по имени, канал local −1).
Отклонение: channel игнорируется; vol 0..1 (default 1).
*/
static void csqc_localsound (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	char *name;
	float vol;
	if (!vm)
		return;
	g = vm->globals;
	name = PR1VM_GetString (vm, *(int *)&g[OFS_PARM0]);
	vol = (vm->argc > 2) ? g[OFS_PARM0 + 6] : 1;
	if (name && name[0])
		S_LocalSoundWithVol (name, vol);
}

/*
float(vector org, float radius, vector lightcolours, optional float style, ...)
dynamiclight_add = #305
ezquake: CL_AllocDlight + поля (lt_custom, color=lightcolours*255, radius, 0.1s).
style/cubemap/pflags — нет аналога (вне скоупа, документировано). Возврат — индекс слота.
*/
static void csqc_dynamiclight_add (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	dlight_t *dl;
	if (!vm)
		return;
	g = vm->globals;
	dl = CL_AllocDlight (0);
	if (!dl)
	{
		vm->globals[OFS_RETURN] = 0;
		return;
	}
	VectorCopy (&g[OFS_PARM0], dl->origin);
	dl->radius = g[OFS_PARM0 + 3];
	dl->die = cl.time + 0.1f;
	dl->type = lt_custom;
	dl->bubble = 0;
	dl->color[0] = (byte)bound (0, g[OFS_PARM0 + 6] * 255.0f, 255);
	dl->color[1] = (byte)bound (0, g[OFS_PARM0 + 7] * 255.0f, 255);
	dl->color[2] = (byte)bound (0, g[OFS_PARM0 + 8] * 255.0f, 255);
	vm->globals[OFS_RETURN] = (float)(int)(dl - cl_dlights) + 1;
}

static void csqc_getplayerkeyvalue (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	int pnum;
	char *key;
	char buf[32];
	player_info_t *pi;
	char *v = NULL;

	if (!vm)
		return;
	pnum = (int)vm->globals[OFS_PARM0];
	key = CSQCVM_Str (OFS_PARM1);
	if (pnum < 0 || pnum >= MAX_CLIENTS || !key || !key[0])
	{
		CSQCVM_SetRetStr ("");
		return;
	}
	pi = &cl.players[pnum];
	if (!pi->name[0])
	{
		CSQCVM_SetRetStr ("");	// пустой слот — игрока нет
		return;
	}
	if (!strcmp (key, "frags"))
		snprintf (buf, sizeof (buf), "%d", pi->frags), v = buf;
	else if (!strcmp (key, "ping"))
		snprintf (buf, sizeof (buf), "%d", pi->ping), v = buf;
	else if (!strcmp (key, "userid"))
		snprintf (buf, sizeof (buf), "%d", pi->userid), v = buf;
	else if (!strcmp (key, "spectator"))
		snprintf (buf, sizeof (buf), "%d", (int)pi->spectator), v = buf;
	else if (!strcmp (key, "name"))
		v = pi->name;
	else
		v = Info_ValueForKey (pi->userinfo, key);	// team/topcolor/bottomcolor/...
	CSQCVM_SetRetStr (v ? v : "");
}

/*
void(float usecursor, optional string cursorimage, optional vector hotspot,
     optional float scale) setcursormode = #343
FTE (pr_clcmd.c PF_cl_setcursormode): освобождает/хватает мышь и настраивает курсор.
ezquake (A3.1): полная реализация — пока usecursor=1 и модуль активен в игре, мышь
не отдаётся OS-курсору и SCR_DrawCursor рисует курсор модуля (image/hotspot/scale);
при 0 мышь возвращается движку. Клики/InputEvent-канал модуля — C1.
ABI/scale — как FTE: hotspot — вектор (w6..8), масштаб читается из hotspot.z (w8),
отдельный float-арг (w9) игнорируется; scale <= 0 -> нативный размер курсора.
*/
static void csqc_setcursormode (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float *g;
	if (!vm)
		return;
	g = vm->globals;
	CSQC_Client_SetCursorMode (g[OFS_PARM0] != 0,
		vm->argc > 1 ? PR1VM_GetString (vm, *(int *)&g[OFS_PARM1]) : NULL,
		vm->argc > 2 ? g[OFS_PARM2]     : 0,
		vm->argc > 2 ? g[OFS_PARM2 + 1] : 0,
		vm->argc > 2 ? g[OFS_PARM2 + 2] : 0);
}

/*
vector() getmousepos = #344
Позиция CSQC-курсора в координатах 2D-оверлея ezquake (см. GetCursorPos); z = 0.
FTE (pr_menu.c PF_cl_getmousepos): при абсолютном курсоре — позиция, иначе дельты
со сбросом. Отклонение (roadmap A3.2): всегда позиция (модуль в абсолютном режиме;
дельты/InputEvent-канал — C1).
*/
static void csqc_getmousepos (void)
{
	pr1vm_t *vm = CSQCVM_Active ();
	float x = 0, y = 0;
	if (!vm)
		return;
	CSQC_Client_GetCursorPos (&x, &y);
	vm->globals[OFS_RETURN] = x;
	vm->globals[OFS_RETURN + 1] = y;
	vm->globals[OFS_RETURN + 2] = 0;
}

void CSQCVM_RegisterBuiltins (pr1vm_t *vm)
{
	PR1VM_RegisterBuiltin (vm, 25, (builtin_t)csqc_dprint);
	PR1VM_RegisterBuiltin (vm, 26, (builtin_t)csqc_ftos);
	PR1VM_RegisterBuiltin (vm, 45, (builtin_t)csqc_cvar);

	// Слой D шаг 3 — ввод/интерфейс: #340 keynumtostring, #341 stringtokeynum.
	PR1VM_RegisterBuiltin (vm, 340, (builtin_t)csqc_keynumtostring);
	PR1VM_RegisterBuiltin (vm, 341, (builtin_t)csqc_stringtokeynum);
	// #349 isdemo, #354 serverkey.
	PR1VM_RegisterBuiltin (vm, 349, (builtin_t)csqc_isdemo);
	PR1VM_RegisterBuiltin (vm, 354, (builtin_t)csqc_serverkey);
	// #343 setcursormode (A3.1: полная — курсор модуля в CSQC-оверлее).
	PR1VM_RegisterBuiltin (vm, 343, (builtin_t)csqc_setcursormode);
	// #344 getmousepos (A3.2: read-путь позиции CSQC-курсора).
	PR1VM_RegisterBuiltin (vm, 344, (builtin_t)csqc_getmousepos);
	// #348 getplayerkeyvalue (A6).
	PR1VM_RegisterBuiltin (vm, 348, (builtin_t)csqc_getplayerkeyvalue);
	// C1.1 — #346 setsensitivityscaler.
	PR1VM_RegisterBuiltin (vm, 346, (builtin_t)csqc_setsensitivityscaler);
	// C1.3 — #345 getinputstate.
	PR1VM_RegisterBuiltin (vm, 345, (builtin_t)csqc_getinputstate);
	// C1.4 — #347 runstandardplayerphysics.
	PR1VM_RegisterBuiltin (vm, 347, (builtin_t)csqc_runstandardplayerphysics);
	// C2.1 — #459 edict_num.
	PR1VM_RegisterBuiltin (vm, 459, (builtin_t)csqc_edict_num);
	// C2.2 — #460-469 string-buffers.
	PR1VM_RegisterBuiltin (vm, 460, (builtin_t)csqc_buf_create);
	PR1VM_RegisterBuiltin (vm, 461, (builtin_t)csqc_buf_del);
	PR1VM_RegisterBuiltin (vm, 462, (builtin_t)csqc_buf_getsize);
	PR1VM_RegisterBuiltin (vm, 463, (builtin_t)csqc_buf_copy);
	PR1VM_RegisterBuiltin (vm, 464, (builtin_t)csqc_buf_sort);
	PR1VM_RegisterBuiltin (vm, 465, (builtin_t)csqc_buf_implode);
	PR1VM_RegisterBuiltin (vm, 466, (builtin_t)csqc_bufstr_get);
	PR1VM_RegisterBuiltin (vm, 467, (builtin_t)csqc_bufstr_set);
	PR1VM_RegisterBuiltin (vm, 468, (builtin_t)csqc_bufstr_add);
	PR1VM_RegisterBuiltin (vm, 469, (builtin_t)csqc_bufstr_free);
	// C3.1 — #177 localsound, #305 dynamiclight_add.
	PR1VM_RegisterBuiltin (vm, 177, (builtin_t)csqc_localsound);
	PR1VM_RegisterBuiltin (vm, 305, (builtin_t)csqc_dynamiclight_add);
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
	PR1VM_RegisterBuiltin (vm, 315, (builtin_t)csqc_drawline);
	PR1VM_RegisterBuiltin (vm, 317, (builtin_t)csqc_precache_pic);
	PR1VM_RegisterBuiltin (vm, 320, (builtin_t)csqc_drawcharacter);
	PR1VM_RegisterBuiltin (vm, 322, (builtin_t)csqc_drawpic);
	PR1VM_RegisterBuiltin (vm, 323, (builtin_t)csqc_drawfill);
	PR1VM_RegisterBuiltin (vm, 327, (builtin_t)csqc_stringwidth);
	PR1VM_RegisterBuiltin (vm, 328, (builtin_t)csqc_drawsubpic);
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
