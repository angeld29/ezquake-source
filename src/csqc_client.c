/*
csqc_client.c -- клиентская обвязка PR1VM (наш csprogs.dat), Фаза 5 (мини-каркас).

Что делает (спайк, «оверлей + статы 0-31»):
  1. При получении полного serverinfo с *csprogs / *csprogssize и битом
     FTE_PEXT_CSQC — грузит локальный csprogs.dat в статический клиентский
     инстанс PR1VM (v7-secondary16), регистрирует клиентские builtins и
     вызывает CSQC_Init.
  2. В первом 2D-кадре (ca_active) — CSQC_WorldLoaded, каждый кадр —
     CSQC_UpdateView(w,h,menushown); перед вызовом обновляет глобал time.
  3. registercommand -> Cmd_AddCommand; выполнение команды -> CSQC_ConsoleCommand.
  4. При разрыве — CSQC_Shutdown, PR1VM_UnLoad, снятие команд.

Вне скоупа мини-каркаса (следующие подшаги): парсинг 76/83/90/92, статы
32-127, реальный sendevent (clcfte_qcrequest), read*-builtins, скачивание
csprogs.dat.
*/

#ifndef CLIENTONLY
#include "quakedef.h"	// client.h (cl.stats), draw.h, vid.h, common.h (Cmd_*)
#include "keys.h"		// key_dest / key_menu
#include "pr1vm.h"
#include "csqc_client.h"

typedef struct csqc_client_state_s
{
	pr1vm_t		vm;
	qbool		loaded;		// модуль загружен в инстанс
	qbool		inited;		// CSQC_Init вызван
	qbool		errored;	// PR_RunError на клиентском инстансе (кадры отключены)
	qbool		world_done;	// CSQC_WorldLoaded вызван
	qbool		enable_sent;	// enablecsqc уже отправлен серверу
	qbool		remove_pending;	// временный Remove: entnum для readentitynum
	int			remove_ent;
	qbool		seen[2048];		// известные CSQC-сущности (isnew для Ent_Update)
	int			func_init, func_world, func_update, func_console, func_shutdown;
	int			func_entupdate, func_entremove, func_parseevent;
	int			global_time;	// смещение глобала time (или -1)
	int			numcmds;
	char		cmds[16][64];
} csqc_client_state_t;

static csqc_client_state_t s_csqc;

// Extended CSQC-статы 32..127 (clientstat/pointerstat от mvdsv). Стандартные
// 0..31 живут в cl.stats[] (клиентская структура); расширенные хранятся здесь
// (см. CSQC_Client_GetStat/SetStat).
static int s_csqc_stat[128];

/*
=================
CSQC_Client_GetStat / SetStat / GetScreenSize / DrawText / RegisterCommand
Accessor'ы для csqc_builtins.c и cl_parse.c (см. csqc_client.h).
=================
*/
float CSQC_Client_GetStat (int idx)
{
	if (idx >= 0 && idx < 32)
		return (float)cl.stats[idx];
	if (idx >= 32 && idx < 128)
		return (float)s_csqc_stat[idx];
	return 0;
}

void CSQC_Client_SetStat (int idx, int value)
{
	if (idx >= 32 && idx < 128)
		s_csqc_stat[idx] = value;
}

/*
=================
CSQC_Client_SetRemoveEnt / ReadEntityNum

Временный путь Remove (без edict-модели): entnum передаётся builtin-стримом —
readentitynum() в модуле вернёт отложенный номер (и снимет pending).
=================
*/
void CSQC_Client_SetRemoveEnt (int entnum)
{
	s_csqc.remove_pending = true;
	s_csqc.remove_ent = entnum;
}

int CSQC_Client_ReadEntityNum (void)
{
	if (s_csqc.remove_pending)
	{
		s_csqc.remove_pending = false;
		return s_csqc.remove_ent;
	}
	return -1;
}

void CSQC_Client_GetScreenSize (int *w, int *h)
{
	if (w)
		*w = vid.width;
	if (h)
		*h = vid.height;
}

void CSQC_Client_DrawText (float x, float y, const char *text, int r, int g, int b, float alpha)
{
	extern cvar_t scr_coloredText;
	static char buf[4096];
	float saved;
	(void)alpha;
	if (!text)
		return;
	// Цвет модуля передаём &cRRGGBB-кодом движка. Чтобы он не зависел от
	// scr_coloredText пользователя, временно включаем его на время отрисовки.
	saved = scr_coloredText.value;
	Cvar_SetValue (&scr_coloredText, 1);
	// Цвет &cRGB — 3 hex-разряда (канал×16), а не &cRRGGBB.
	snprintf (buf, sizeof (buf), "&c%X%X%X%s",
		(bound (0, r, 255)) / 16, (bound (0, g, 255)) / 16, (bound (0, b, 255)) / 16, text);
	Draw_SColoredStringBasic (x, y, buf, 0, 1, true);
	Cvar_SetValue (&scr_coloredText, saved);
}

static void CSQC_Client_ConsoleCommand_f (void);

void CSQC_Client_RegisterCommand (const char *cmd)
{
	int i;
	if (!cmd || !cmd[0])
		return;
	for (i = 0; i < s_csqc.numcmds; i++)
		if (!strcmp (s_csqc.cmds[i], cmd))
			return;					// уже зарегистрирована
	if (s_csqc.numcmds >= (int)(sizeof (s_csqc.cmds) / sizeof (s_csqc.cmds[0])))
		return;
	strlcpy (s_csqc.cmds[s_csqc.numcmds], cmd, sizeof (s_csqc.cmds[0]));
	// Cmd_AddRemCommand копирует имя в Q_malloc-блок (в отличие от
	// Cmd_AddCommand, который держит указатель на имя и аллоцит узел в hunk).
	// Узел/имя переживают Host_ClearMemory и корректно удаляются RemoveCommand.
	if (Cmd_AddRemCommand (s_csqc.cmds[s_csqc.numcmds], CSQC_Client_ConsoleCommand_f))
		s_csqc.numcmds++;
}

/*
=================
host-колбэки клиентского инстанса
=================
*/
static void CSQC_Client_HostPrint (pr1vm_t *vm, const char *msg)
{
	(void)vm;
	Con_Printf ("%s\n", msg);
}

static void CSQC_Client_HostError (pr1vm_t *vm, const char *msg)
{
	(void)vm;
	Con_Printf ("CSQC (PR1VM) program error: %s\n", msg);
	s_csqc.errored = true;
	// Дальше спайк живёт: кадры отключаются (errored), перезагрузка при
	// следующем ConnectCheck (новая карта/коннект).
}

/*
=================
Внутренние помощники
=================
*/
static void CSQC_Client_SetTime (void)
{
	pr1vm_t *vm = &s_csqc.vm;
	if (s_csqc.global_time >= 0)
		vm->globals[s_csqc.global_time] = (float)Sys_DoubleTime ();
}

static qbool CSQC_Client_Exec (int fidx)
{
	pr1vm_t *vm = &s_csqc.vm;
	if (fidx <= 0 || fidx >= vm->progs->numfunctions)
		return false;
	CSQC_Client_SetTime ();
	PR1VM_ExecuteProgram (vm, (func_t)fidx);
	return !s_csqc.errored;
}

static void CSQC_Client_ClearCommands (void)
{
	int i;
	for (i = 0; i < s_csqc.numcmds; i++)
		Cmd_RemoveCommand (s_csqc.cmds[i]);
	s_csqc.numcmds = 0;
}

/*
=================
CSQC_Client_ConsoleCommand_f

Команда, зарегистрированная модулем через registercommand. Восстанавливаем
полную строку («name arg1 arg2 …») и зовём CSQC_ConsoleCommand(string cmd).
=================
*/
static void CSQC_Client_ConsoleCommand_f (void)
{
	pr1vm_t *vm = &s_csqc.vm;
	const char *line;

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;
	if (s_csqc.func_console <= 0)
		return;

	if (Cmd_Argc () > 1)
		line = va ("%s %s", Cmd_Argv (0), Cmd_Args ());
	else
		line = Cmd_Argv (0);

	CSQC_Client_SetTime ();
	PR1VM_SetString (vm, (string_t *)&vm->globals[OFS_PARM0], (char *)line);
	vm->globals[OFS_RETURN] = 0;
	PR1VM_ExecuteProgram (vm, (func_t)s_csqc.func_console);
}

/*
=================
CSQC_Client_Active
=================
*/
int CSQC_Client_Active (void)
{
	return (s_csqc.loaded && !s_csqc.errored) ? 1 : 0;
}

/*
=================
CSQC_Client_Load

Пытается загрузить csprogs.dat (локальный файл из gamedir; download — вне
скоупа мини-каркаса) в клиентский инстанс и вызвать CSQC_Init. Возвращает
true при успехе. При неудаче печатает причину и допускает повторную попытку
(файл может появиться позже: download / путь к gamedir ещё не в FS).
=================
*/
static qbool CSQC_Client_Load (void)
{
	byte *data;
	int filesize;
	pr1vm_t *vm;
	dfunction_t *f;

	data = (byte *)FS_LoadHunkFile ("csprogs.dat", &filesize);
	if (!data)
	{
		Con_Printf ("CSQC: server offers csprogs but csprogs.dat not found locally\n");
		return false;
	}

	memset (&s_csqc, 0, sizeof (s_csqc));
	s_csqc.func_init = s_csqc.func_world = s_csqc.func_update =
		s_csqc.func_console = s_csqc.func_shutdown = -1;
	s_csqc.func_entupdate = s_csqc.func_entremove = s_csqc.func_parseevent = -1;
	s_csqc.global_time = -1;

	vm = &s_csqc.vm;
	vm->host_error = CSQC_Client_HostError;
	vm->host_print = CSQC_Client_HostPrint;

	if (!PR1VM_LoadClientV7 (vm, data, filesize))
	{
		Con_Printf ("CSQC: csprogs.dat load failed (v7)\n");
		return false;
	}

	CSQCVM_RegisterBuiltins (vm);

	f = PR1VM_FindFunction (vm, "CSQC_Init");
	if (f)
		s_csqc.func_init = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_WorldLoaded");
	if (f)
		s_csqc.func_world = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_UpdateView");
	if (f)
		s_csqc.func_update = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_ConsoleCommand");
	if (f)
		s_csqc.func_console = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_Shutdown");
	if (f)
		s_csqc.func_shutdown = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_Ent_Update");
	if (f)
		s_csqc.func_entupdate = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_Ent_Remove");
	if (f)
		s_csqc.func_entremove = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_Parse_Event");
	if (f)
		s_csqc.func_parseevent = (int)(f - vm->functions);

	s_csqc.global_time = PR1VM_FindGlobal (vm, "time");

	s_csqc.loaded = true;

	Con_Printf ("CSQC: loaded csprogs.dat (%d statements, crc=0x%x), funcs i=%d w=%d u=%d "
		"c=%d s=%d eu=%d er=%d pe=%d time=%d\n",
		vm->progs->numstatements, (unsigned int)vm->progs->crc, s_csqc.func_init,
		s_csqc.func_world, s_csqc.func_update, s_csqc.func_console, s_csqc.func_shutdown,
		s_csqc.func_entupdate, s_csqc.func_entremove, s_csqc.func_parseevent,
		s_csqc.global_time);

	// CSQC_Init(apiver, enginename, enginever) — сигнатура нашего модуля.
	if (s_csqc.func_init > 0)
	{
		vm->globals[OFS_PARM0] = 0;	// apiver (float)
		PR1VM_SetString (vm, (string_t *)&vm->globals[OFS_PARM1], "ezquake-orig");
		vm->globals[OFS_PARM2] = 0;	// enginever (float в нашем модуле)
		CSQC_Client_Exec (s_csqc.func_init);
		s_csqc.inited = !s_csqc.errored;
	}
	return true;
}

/*
=================
CSQC_Client_ConnectCheck

Вызывается при входе в мир (CL_MakeActive, до ca_active) — момент, когда весь
контент (включая csprogs.dat) уже доступен в FS (аналог преспауна FTE).
Если сервер предлагает CSQC (*csprogssize) и модуль ещё не загружен —
грузим и вызываем CSQC_Init.
=================
*/
void CSQC_Client_ConnectCheck (void)
{
	extern cvar_t cl_pext_csqc;
	int sizep;

	// Мастер-выключатель (аналог FTE cl_nocsqc): 0 — весь CSQC отключён,
	// модуль не грузится, клиент ведёт себя как раньше.
	if (!cl_pext_csqc.value)
		return;

	sizep = (int)strtoul (Info_ValueForKey (cl.serverinfo, "*csprogssize"), NULL, 0);
	if (sizep <= 0)
		return;		// обычный сервер без CSQC (или PR1-гейт сервера)

	// Модуль загружается «с нуля» на КАЖДЫЙ вход в мир (первый коннект и каждая
	// смена карты): выгрузка происходит при выходе из мира (CL_ClearState, до
	// Host_ClearMemory), здесь — загрузка свежего csprogs.dat. Защитный unload
	// на случай путей без CL_ClearState (двойной вызов безопасен — no-op).
	if (s_csqc.loaded)
		CSQC_Client_Disconnect ();

	if (!CSQC_Client_Load ())
		return;
	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;

	// Вход в новую карту: per-карта состояние чистое (WorldLoaded/enablecsqc
	// будут этой карты; модуль уже новый).
	memset (s_csqc.seen, 0, sizeof (s_csqc.seen));
	s_csqc.remove_pending = false;
	s_csqc.world_done = false;
	s_csqc.enable_sent = false;
}

/*
=================
CSQC_Client_Update

Вызывается каждый 2D-кадр (HUD-фаза, cl_screen.c). WorldLoaded — один раз
после входа в мир; далее CSQC_UpdateView(vid.width, vid.height, menushown).
=================
*/
void CSQC_Client_Update (void)
{
	pr1vm_t *vm = &s_csqc.vm;

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;
	if (cls.state != ca_active)
		return;

	if (!s_csqc.world_done)
	{
		s_csqc.world_done = true;
		if (!CSQC_Client_Exec (s_csqc.func_world))
			return;
		// FTE: enablecsqc — после CSQC_WorldLoaded каждой карты (module ready).
		if (!s_csqc.enable_sent)
		{
#ifdef FTE_PEXT_CSQC
			if (cls.fteprotocolextensions & FTE_PEXT_CSQC)
#endif
			{
				MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
				MSG_WriteString (&cls.netchan.message, "enablecsqc");
				s_csqc.enable_sent = true;
				Con_Printf ("CSQC: enablecsqc sent (map)\n");
			}
		}
	}

	if (s_csqc.func_update > 0)
	{
		vm->globals[OFS_PARM0] = vid.width;
		vm->globals[OFS_PARM1] = vid.height;
		vm->globals[OFS_PARM2] = (key_dest == key_menu) ? 1 : 0;
		CSQC_Client_Exec (s_csqc.func_update);
	}
}

/*
=================
CSQC_Client_ParseEntities

Парсинг svc_fte_csqcentities(76), простая версия (S1):
для каждой сущности — short entnum, бит 0x8000 = remove, 0 = конец.
Update: CSQC_Ent_Update(isnew) — модуль читает payload из текущего сообщения
(read*). Remove: временно entnum через builtin-стрим (SetRemoveEnt), без edict.
=================
*/
void CSQC_Client_ParseEntities (void)
{
	pr1vm_t *vm = &s_csqc.vm;
	unsigned int entnum;
	qbool removeflag;
	static int dbg_upd = 0, dbg_rem = 0, dbg_bad = 0;
	static int dbg_early = 0;

	// [DEBUG Bug 2] временный след ранних выходов (76 пришёл, а разбор нет).
	if (dbg_early == 0 || dbg_early == 50 || dbg_early == 500)
		Con_Printf ("CSQC: ParseEntities entered (loaded=%d inited=%d errored=%d "
			"eu=%d er=%d read=%d)\n", s_csqc.loaded, s_csqc.inited,
			s_csqc.errored, s_csqc.func_entupdate, s_csqc.func_entremove, msg_readcount);
	dbg_early++;

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;
	if (s_csqc.func_entupdate <= 0 && s_csqc.func_entremove <= 0)
		return;

	for (;;)
	{
		entnum = (unsigned short)MSG_ReadShort ();
		removeflag = !!(entnum & 0x8000);
		entnum &= ~0x8000u;
		if ((!entnum && !removeflag) || msg_badread)
		{
			if (msg_badread && !dbg_bad)
			{
				dbg_bad = 1;
				Con_Printf ("CSQC: csqcentities badread\n");
			}
			break;
		}
		if (entnum >= (unsigned int)(sizeof (s_csqc.seen) / sizeof (s_csqc.seen[0])))
			break;

		if (removeflag)
		{
			if (s_csqc.func_entremove > 0)
			{
				if (!dbg_rem)
				{
					dbg_rem = 1;
					Con_Printf ("CSQC: first entity remove entnum=%u\n", entnum);
				}
				CSQC_Client_SetRemoveEnt ((int)entnum);
				CSQC_Client_Exec (s_csqc.func_entremove);
			}
			s_csqc.seen[entnum] = false;
			continue;
		}

		if (s_csqc.func_entupdate > 0)
		{
			if (!dbg_upd)
			{
				dbg_upd = 1;
				Con_Printf ("CSQC: first entity update entnum=%u isnew=%d\n",
					entnum, s_csqc.seen[entnum] ? 0 : 1);
			}
			vm->globals[OFS_PARM0] = s_csqc.seen[entnum] ? 0 : 1;
			s_csqc.seen[entnum] = true;
			CSQC_Client_Exec (s_csqc.func_entupdate);
			if (s_csqc.errored)
				return;
		}
	}

	// [DEBUG Bug 2] временный след конца разбора (срабатывает, если цикл шёл,
	// но обновлений/remove не было — пустой 76 или только терминатор).
	if (dbg_early == 0 || dbg_early == 50 || dbg_early == 500)
		Con_Printf ("CSQC: ParseEntities end (read=%d badread=%d)\n",
			msg_readcount, msg_badread);
}

/*
=================
CSQC_Client_ParseEvent

Парсинг svc_fte_cgamepacket(83) (E1): имя события и payload читает сам модуль
(CSQC_Parse_Event) через read*-builtins из текущего сообщения. Guard как в
ParseEntities — без модуля чужой CSQC-multicast (echo) не роняет клиент.
=================
*/
void CSQC_Client_ParseEvent (void)
{
	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;
	if (s_csqc.func_parseevent <= 0)
		return;
	CSQC_Client_Exec (s_csqc.func_parseevent);
}

/*
=================
CSQC_Client_Disconnect
=================
*/
void CSQC_Client_Disconnect (void)
{
	if (s_csqc.loaded)
	{
		if (s_csqc.inited && !s_csqc.errored)
			CSQC_Client_Exec (s_csqc.func_shutdown);
		PR1VM_UnLoad (&s_csqc.vm);
	}
	CSQC_Client_ClearCommands ();
	memset (&s_csqc, 0, sizeof (s_csqc));
	memset (s_csqc_stat, 0, sizeof (s_csqc_stat));
	s_csqc.func_init = s_csqc.func_world = s_csqc.func_update =
		s_csqc.func_console = s_csqc.func_shutdown = -1;
	s_csqc.func_entupdate = s_csqc.func_entremove = s_csqc.func_parseevent = -1;
	s_csqc.global_time = -1;
}

#endif // !CLIENTONLY
