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
#include "pmove.h"		// playermove_t/pmove/movevars/PM_PlayerMove (C1.4 #347)
#include "common_draw.h"	// CachePic_Find/Remove, Draw_EnableScissorRectangle/DisableScissor

// FTE-пул (слот ≠ серверный номер; план docs/ezquake_csqc_client_corebuiltins_plan.md):
// CSQC_MAX_NUM — верх серверных номеров (карта номер→слот), CSQC_MAX_EDICTS — размер пула
// edict-слотов арены (слот 0 = world, не управляется). .entnum (поле модуля) = серверный
// номер; модульные spawn-сущности номера не имеют (.entnum=0).
#define CSQC_MAX_NUM	4096
#define CSQC_MAX_EDICTS	4096

typedef struct csqc_client_state_s
{
	pr1vm_t		vm;
	qbool		loaded;		// модуль загружен в инстанс
	qbool		inited;		// CSQC_Init вызван
	qbool		errored;	// PR_RunError на клиентском инстансе (кадры отключены)
	qbool		world_done;	// CSQC_WorldLoaded вызван
	qbool		enable_sent;	// enablecsqc уже отправлен серверу
	qbool		seen[CSQC_MAX_NUM];	// известные CSQC-сущности (isnew для Ent_Update)
	int			func_init, func_world, func_update, func_console, func_shutdown;
	int			func_entupdate, func_entremove, func_parseevent;
	int			func_input;		// CSQC_Input_Frame (или -1)
	int			func_inputevent;	// CSQC_InputEvent (или -1; C1.2)
	int			global_time;	// смещение глобала time (или -1)
	int			global_self;	// смещение глобала self (или -1; ADR 0017 P2/D3)
	int			field_entnum;	// float-слово поля .entnum в entvars (или -1)
	// C1.4 #347: field-offset'ы стандартной физики (или -1).
	int			f_origin, f_velocity, f_mins, f_maxs;
	int			f_movetype, f_flags, f_gravity, f_pmove_flags;
	// FTE-пул Шаг 7 (часть 2): поля классификации трасс и зеркала игроков —
	// удалены вместе с зеркалом (окружение = FTE: без серверной эмиссии игроков
	// ezquake сущности игроков не фабрикует). Публикация player_localentnum (FTE).
	int			g_localentnum;	// глобал модуля player_localentnum (или -1)
	// input_* глобалы для CSQC_Input_Frame (или -1, если модуль их не объявил).
	int			in_timelength, in_angles, in_movevalues, in_buttons, in_impulse;
	int			in_sequence;	// input_sequence (C1.3 #345) или -1
	// C5-A: глобалы окна предикции (csdefs.qc:50-51) или -1.
	int			g_ccframe;		// clientcommandframe
	int			g_scframe;		// servercommandframe
	// C5-A/B: deprec-глобалы pmove_org/pmove_vel/pmove_onground (или -1;
	// пишет #347 в B; в A только резолв).
	int			p_org, p_vel, p_onground;
	// #1 makevectors (C6.1): глобалы v_forward/v_right/v_up модуля (или -1).
	int			g_vfwd, g_vright, g_vup;
	// Скачивание csprogs (локально нет валидного файла): качаем *csprogsname с
	// сервера и сохраняем в csprogsvers/<crc>.dat (как FTE); загружаем после
	// появления валидного файла (см. CSQC_Client_Update).
	qbool		csprogs_dl_pending;
	double		csprogs_dl_start;
	unsigned	csprogs_crc;	// *csprogs (md4 Com_BlockChecksum) / 0 если нет
	int			csprogs_size;	// *csprogssize
	char		csprogs_dl_path[MAX_QPATH];	// локальный файл после скачивания
	int			numcmds;
	char		cmds[16][64];
	// Арена edicts клиентского инстанса (ADR 0017 P1/D2). Q_malloc, free в
	// Disconnect/Load-start; bind в vm->edicts/game_edicts (entity-опкоды).
	edict_t		*edicts;
	byte		*game_edicts;
} csqc_client_state_t;

static csqc_client_state_t s_csqc;

// Client PR1VM helpers (rule "client parts live outside shared core files"):
// LoadClientV6 + CSQCSmoke are implemented here (used to be in pr_edict.c/pr1vm.h).
static qbool PR1VM_LoadClientV6 (pr1vm_t *vm, const byte *data, int filesize);
static void PR1VM_CSQCSmoke_f (void);

// C5-A #345: кольцевой буфер отправленных usercmd (запись из CL_SendCmd).
// seq = зеркало cls.netchan.outgoing_sequence (номер клиентского сообщения на
// момент записи; Netchan_Transmit инкрементирует ПОСЛЕ записи заголовка —
// net_chan.c:316-319, поэтому во время CL_SendCmd outgoing_sequence ещё равен
// номеру текущего cmd). C1.3 ввёл локальный счётчик — заменён зеркалом (C5-A).
// Размер 64 = UPDATE_BACKUP (окно предикции).
#define CSQC_INHIST	64
typedef struct { unsigned int seq; usercmd_t cmd; } csqc_inrec_t;
static csqc_inrec_t s_inhist[CSQC_INHIST];
static unsigned int s_last_seq;	// seq последней записи (0 — записей нет)

// C2.2 #460-469: пул string-buffers (DP). Строки deep-copy (переживают кадры).
#define CSQC_MAX_BUFS	64
typedef struct
{
	qbool	inuse;
	int		num;
	int		cap;
	char	**str;
} csqc_buf_t;
static csqc_buf_t s_bufs[CSQC_MAX_BUFS];

// C1.1 — #346 setsensitivityscaler: временный множитель чувствительности мыши
// (зум-аналог FTE in_sensitivityscale). Хранит модуль; применяет in_sdl2.c.
static float s_sens_scale = 1;

// Слой D шаг 3 — #343 setcursormode (A3.1): состояние курсора модуля. Пока
// usecursor=1 и модуль активен в игре (CSQC_Client_CSQCCursor), мышь свободна
// (vid_sdl2 не отдаёт её OS-курсору), а SCR_DrawCursor рисует курсор модуля.
typedef struct
{
	qbool	usecursor;
	char	cursorimage[MAX_QPATH];
	float	hotspot[2];
	float	scale;
} csqc_cursormode_t;
static csqc_cursormode_t s_cursormode;

// Клиентская арена edicts (ADR 0017 P1/D2 + FTE-пул): пул слотов произвольный,
// серверный номер хранится в .entnum (карта s_numslot: номер→слот). Слот 0 — world.
// s_own — сущность создана модулем (spawn); remove разрешён только для своих.
static qbool s_used[CSQC_MAX_EDICTS];
static qbool s_own[CSQC_MAX_EDICTS];
static int s_numslot[CSQC_MAX_NUM];

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

void CSQC_Client_GetScreenSize (int *w, int *h)
{
	if (w)
		*w = vid.width;
	if (h)
		*h = vid.height;
}

void CSQC_Client_DrawText (float x, float y, const char *text, int r, int g, int b, float alpha, float scale)
{
	extern cvar_t scr_coloredText;
	static char buf[4096];
	float saved;
	(void)alpha;
	if (!text)
		return;
	// Слой D шаг 2: масштаб шрифта из size.x (scale=size.x/8; 0 => 1). Цвет
	// модуля передаём &cRRGGBB-кодом движка. Чтобы он не зависел от
	// scr_coloredText пользователя, временно включаем его на время отрисовки.
	saved = scr_coloredText.value;
	Cvar_SetValue (&scr_coloredText, 1);
	// Цвет &cRGB — 3 hex-разряда (канал×16), а не &cRRGGBB.
	snprintf (buf, sizeof (buf), "&c%X%X%X%s",
		(bound (0, r, 255)) / 16, (bound (0, g, 255)) / 16, (bound (0, b, 255)) / 16, text);
	Draw_SColoredStringBasic (x, y, buf, 0, (scale > 0) ? scale : 1, true);
	Cvar_SetValue (&scr_coloredText, saved);
}

// Цвет для draw-помощников Слоя D (rgb 0..255 байты, alpha 0..1).
static color_t CSQC_Client_Color (int r, int g, int b, float alpha)
{
	return RGBA_TO_COLOR ((byte)bound (0, r, 255), (byte)bound (0, g, 255),
		(byte)bound (0, b, 255), (byte)bound (0, (int)(alpha * 255.0f + 0.5f), 255));
}

/*
#324 drawsetcliparea / #325 drawresetcliparea — геометрическое отсечение (решение
2026-09-07; аппаратный GL-scissor на отложенном 2D-пайплайне ezq не применим).
Состояние clip-прямоугольника в координатах CSQC-рисования; прямоугольные
примитивы (pic/subpic/fill) пересекаются с ним, текст/линии только не рисуются,
если целиком вне (строки внутри не режутся) — отклонение в parity.
*/
static qbool s_clip_on = false;
static float s_clip_x, s_clip_y, s_clip_w, s_clip_h;

// Пересекает dest-rect (x,y,w,h) с активным clip. Возврат false = пусто/вне.
static qbool CSQC_Client_ClipDest (float *x, float *y, float *w, float *h)
{
	float x0, y0, x1, y1;
	if (!s_clip_on)
		return true;
	x0 = *x; y0 = *y;
	x1 = *x + *w; y1 = *y + *h;
	if (x1 <= s_clip_x || x0 >= s_clip_x + s_clip_w ||
		y1 <= s_clip_y || y0 >= s_clip_y + s_clip_h)
		return false;
	x0 = (x0 > s_clip_x) ? x0 : s_clip_x;
	y0 = (y0 > s_clip_y) ? y0 : s_clip_y;
	x1 = (x1 < s_clip_x + s_clip_w) ? x1 : s_clip_x + s_clip_w;
	y1 = (y1 < s_clip_y + s_clip_h) ? y1 : s_clip_y + s_clip_h;
	*x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
	return true;
}

/*
=================
Слой D, шаг 1 — 2D-графика (docs/ezquake_csqc_client_layerd_2d_plan.md).
Координаты/размеры — сырые пиксели видео (как DrawText). drawpic: rgb-tint
игнорируется (только alpha; решение R2), масштаб = size / нативный размер.
=================
*/
void CSQC_Client_DrawFill (float x, float y, float w, float h, int r, int g, int b, float alpha)
{
	if (w <= 0 || h <= 0)
		return;
	if (!CSQC_Client_ClipDest (&x, &y, &w, &h))
		return;
	Draw_AlphaRectangleRGB (x, y, w, h, 1, true, CSQC_Client_Color (r, g, b, alpha));
}

void CSQC_Client_DrawPic (float x, float y, float w, float h, const char *name, int r, int g, int b, float alpha)
{
	mpic_t *pic;
	float sx, sy, a = bound (0, alpha, 1);
	float dx, dy, dw, dh, srcx, srcy, srcw, srch;
	if (!name || !name[0] || w < 0 || h < 0)
		return;
	pic = Draw_CachePicSafe (name, false, false);
	if (!pic)
		return;
	// Клип: пересечение dest с активной областью, источник пересчитывается
	// (свойство «весь pic → dest» сохраняется).
	dx = x; dy = y; dw = w; dh = h;
	if (!CSQC_Client_ClipDest (&dx, &dy, &dw, &dh))
		return;
	sx = (w > 0) ? w / (float)pic->width : 1;
	sy = (h > 0) ? h / (float)pic->height : 1;
	srcx = (dx - x) / sx;
	srcy = (dy - y) / sy;
	srcw = dw / sx;
	srch = dh / sy;
	if (r == 255 && g == 255 && b == 255)
		Draw_SAlphaSubPic2 (dx, dy, pic, (int)srcx, (int)srcy, (int)srcw, (int)srch, sx, sy, a);
	else
		Draw_SColoredSubPic2 (dx, dy, pic, (int)srcx, (int)srcy, (int)srcw, (int)srch, sx, sy,
			bound (0, r, 255), bound (0, g, 255), bound (0, b, 255), a);
}

void CSQC_Client_DrawSubPic (float x, float y, float w, float h, const char *name, float srcx, float srcy, float srcw, float srch, int r, int g, int b, float alpha)
{
	mpic_t *pic;
	float a = bound (0, alpha, 1);
	float dx, dy, dw, dh, nsx, nsy, nsw, nsh, ssx, ssy;
	if (!name || !name[0] || w <= 0 || h <= 0 || srcw <= 0 || srch <= 0)
		return;
	pic = Draw_CachePicSafe (name, false, false);
	if (!pic)
		return;
	// Клип как в DrawPic: dest пересекается, источник — по аффинному маппингу.
	dx = x; dy = y; dw = w; dh = h;
	if (!CSQC_Client_ClipDest (&dx, &dy, &dw, &dh))
		return;
	ssx = w / srcw;
	ssy = h / srch;
	nsx = srcx + ((dx - x) / w) * srcw;
	nsy = srcy + ((dy - y) / h) * srch;
	nsw = (dw / w) * srcw;
	nsh = (dh / h) * srch;
	if (r == 255 && g == 255 && b == 255)
		Draw_SAlphaSubPic2 (dx, dy, pic, (int)nsx, (int)nsy, (int)nsw, (int)nsh, ssx, ssy, a);
	else
		Draw_SColoredSubPic2 (dx, dy, pic, (int)nsx, (int)nsy, (int)nsw, (int)nsh, ssx, ssy,
			bound (0, r, 255), bound (0, g, 255), bound (0, b, 255), a);
}

void CSQC_Client_DrawCharacter (float x, float y, int ch, int r, int g, int b, float alpha, float scale)
{
	extern cvar_t scr_coloredText;
	static char buf[8];
	float saved;
	int c = ch & 0xff;
	if (c <= 0)
		return;
	// Один символ default-шрифта с цветом &cRGB (как DrawText).
	saved = scr_coloredText.value;
	Cvar_SetValue (&scr_coloredText, 1);
	snprintf (buf, sizeof (buf), "&c%X%X%X%c",
		(bound (0, r, 255)) / 16, (bound (0, g, 255)) / 16, (bound (0, b, 255)) / 16, c);
	Draw_SColoredStringBasic (x, y, buf, 0, (scale > 0) ? scale : 1, true);
	Cvar_SetValue (&scr_coloredText, saved);
}

void CSQC_Client_DrawLine (float x1, float y1, float x2, float y2, float width, int r, int g, int b, float alpha)
{
	if (width <= 0)
		return;
	Draw_AlphaLineRGB (x1, y1, x2, y2, width, CSQC_Client_Color (r, g, b, alpha));
}

float CSQC_Client_StringWidth (const char *text, qbool usecolours, float fontsize_x)
{
	float scale;
	if (!text)
		return 0;
	// Масштаб из size.x (как DrawText; 0 => 1) — та же метрика, что рисует
	// drawstring: r_draw_charset.c Draw_StringLength/Colors.
	scale = (fontsize_x > 0) ? fontsize_x / 8.0f : 1;
	return usecolours ? Draw_StringLengthColors (text, -1, scale, true)
		: Draw_StringLength (text, -1, scale, true);
}

qbool CSQC_Client_PrecachePic (const char *name)
{
	if (!name || !name[0])
		return false;
	return Draw_CachePicSafe (name, false, false) != NULL;
}

// Слой L2 — «2D-графика доп» (2026-09-07; #316/#318/#319/#321/#324/#325/#329).

qbool CSQC_Client_IsCachedPic (const char *name)
{
	if (!name || !name[0])
		return false;
	return CachePic_Find (name, false) != NULL;
}

qbool CSQC_Client_PicSize (const char *name, float *w, float *h)
{
	mpic_t *pic;
	if (!name || !name[0])
		return false;
	pic = Draw_CachePicSafe (name, false, false);
	if (!pic)
		return false;
	if (w)
		*w = (float)pic->width;
	if (h)
		*h = (float)pic->height;
	return true;
}

void CSQC_Client_DrawRawText (float x, float y, const char *text, int r, int g, int b, float alpha, float scale)
{
	char one[2];
	const char *p;
	float xx;
	(void)alpha;
	if (!text)
		return;
	// «Сырой» вывод: каждый символ рисуется одиночным цветным глифом — внутри
	// одной строки нет места для сборки &cRGB, поэтому & в тексте модуля
	// выводится литерально (как FTE drawrawstring). Цвет применяется.
	xx = x;
	for (p = text; *p; p++)
	{
		one[0] = *p;
		one[1] = 0;
		CSQC_Client_DrawCharacter (xx, y, (int)(unsigned char)*p, r, g, b, 1, scale);
		xx += Draw_StringLength (one, 1, (scale > 0) ? scale : 1, true);
	}
}

/*
#324 drawsetcliparea / #325 drawresetcliparea — геометрический clip (состояние),
без аппаратного scissor/flush (см. комментарий к CSQC_Client_ClipDest).
*/
void CSQC_Client_SetClipArea (float x, float y, float w, float h)
{
	s_clip_x = x;
	s_clip_y = y;
	s_clip_w = (w > 0) ? w : 0;
	s_clip_h = (h > 0) ? h : 0;
	s_clip_on = true;
}

void CSQC_Client_ResetClipArea (void)
{
	s_clip_on = false;
}

void CSQC_Client_SetCursorMode (qbool usecursor, const char *image,
	float hotspot_x, float hotspot_y, float scale)
{
	// Полная реализация (roadmap A3.1): запоминаем параметры; эффект включается
	// самим состоянием CSQC_Client_CSQCCursor() — пока usecursor=1 и модуль активен
	// в игре, mouse-механика ezquake не отдаёт мышь OS-курсору (vid_sdl2.c), а
	// SCR_DrawCursor рисует курсор модуля. Клики/InputEvent-канал — C1.
	s_cursormode.usecursor = usecursor;
	s_cursormode.cursorimage[0] = 0;
	if (image)
		strlcpy (s_cursormode.cursorimage, image, sizeof (s_cursormode.cursorimage));
	s_cursormode.hotspot[0] = hotspot_x;
	s_cursormode.hotspot[1] = hotspot_y;
	s_cursormode.scale = scale;
}

qbool CSQC_Client_CSQCCursor (void)
{
	// Курсор модуля действует только в игровом кадре (key_game): при открытом
	// консоль/меню движка их собственный курсор/мышь имеют приоритет.
	return s_cursormode.usecursor && s_csqc.loaded && !s_csqc.errored
		&& key_dest == key_game;
}

void CSQC_Client_GetCursorPos (float *x, float *y)
{
	extern double cursor_x, cursor_y;	// cl_screen.c:161 (сырые координаты указателя)
	if (x)
		*x = (float)cursor_x;
	if (y)
		*y = (float)cursor_y;
}

void CSQC_Client_SetSensitivityScale (float scale)
{
	// C1.1 #346: множитель чувствительности (может быть 0); дефолт 1.
	s_sens_scale = scale;
}

float CSQC_Client_SensitivityScale (void)
{
	// Неактивный модуль — без влияния (default 1).
	if (!s_csqc.loaded || s_csqc.errored)
		return 1;
	return s_sens_scale;
}

void CSQC_Client_DrawCursor (void)
{
	extern double cursor_x, cursor_y;
	mpic_t *pic;
	float scale, x, y;

	if (!CSQC_Client_CSQCCursor ())
		return;
	// FTE: scale <= 0 -> 1; hotspot — «остриё» курсора в пикселях картинки
	// (умножается на масштаб), т.е. позиция указывает на точку клика.
	scale = (s_cursormode.scale > 0) ? s_cursormode.scale : 1;
	x = (float)cursor_x - s_cursormode.hotspot[0] * scale;
	y = (float)cursor_y - s_cursormode.hotspot[1] * scale;

	if (s_cursormode.cursorimage[0])
	{
		pic = Draw_CachePicSafe (s_cursormode.cursorimage, false, false);
		if (!pic)
			pic = Draw_CachePicSafe (s_cursormode.cursorimage, false, true);	// tga/png
		if (pic)
		{
			Draw_SColoredSubPic2 (x, y, pic, 0, 0, pic->width, pic->height,
				scale, scale, 255, 255, 255, 1);
			return;
		}
	}
	// Без картинки — дефолтное перекрестие (визуально как ezquake-курсор).
	{
		color_t c = RGBA_TO_COLOR (0, 255, 0, 255);
		float s = scale;
		Draw_AlphaLineRGB (x + 4 * s, y + 4 * s, x + 16 * s, y + 16 * s, 2 * s, c);
		Draw_AlphaLineRGB (x, y, x + 8 * s, y, 2 * s, c);
		Draw_AlphaLineRGB (x, y, x, y + 8 * s, 2 * s, c);
		Draw_AlphaLineRGB (x + 8 * s, y, x, y + 8 * s, 2 * s, c);
	}
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
CSQC_Client_RegisterCommands

Registers client debug commands for PR1VM (csqc_smoke, etc.). Called from
CL_InitLocal (cl_main.c) — commands available in the client console. csqc_smoke
used to be registered in PR2_Init (server); moved here per the rule "client parts
live outside shared core files" (docs/ezquake_csqc_client_pr1vm_plan.md).
=================
*/
void CSQC_Client_RegisterCommands (void)
{
	Cmd_AddCommand ("csqc_smoke", PR1VM_CSQCSmoke_f);	// PR1VM S3 debug
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
CSQC_Client_Abort

Фатальная ошибка модуля (паритет FTE CSQC_Abort → Host_EndGame): печатаем
причину и отключаем клиента от сервера (дисконнект, возврат в меню), затем
Host_Abort (longjmp в Host_Frame) — не возвращаемся в исполняемую VM.
errored ставим ДО CL_Disconnect, чтобы CSQC_Client_Disconnect не звал
func_shutdown реентерабельно (мы сами внутри исполняемой VM).
=================
*/
void CSQC_Client_Abort (const char *msg)
{
	Con_Printf ("CSQC (PR1VM) fatal: %s\n", msg ? msg : "fatal");
	s_csqc.errored = true;
	CL_Disconnect ();
	Host_Abort ();
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
CSQC_Client_ValidateFile

Проверяет локальный файл csprogs по серверным ключам: размер == *csprogssize
и (если задан *csprogs) Com_BlockChecksum == crc (тот же md4, что у mvdsv
Com_BlockChecksum, md4.c). Аналог FTE CSQC_ValidateMainCSProgs (pr_csqc.c).
=================
*/
static qbool CSQC_Client_ValidateFile (const char *path, int size, unsigned crc)
{
	byte *data;
	int filesize;

	if (!path || !path[0])
		return false;
	data = (byte *)FS_LoadHunkFile ((char *)path, &filesize);
	if (!data)
		return false;
	if (size > 0 && filesize != size)
		return false;
	if (crc && Com_BlockChecksum (data, filesize) != crc)
		return false;
	return true;
}

/*
=================
CSQC_Client_FindMainProgs

Поиск валидного локального csprogs по FTE-семантике (CSQC_FindMainProgs,
fteqw/engine/client/pr_csqc.c): 1) кэш csprogsvers/<crc>.dat, 2) *csprogsname
(+ фолбэк на csprogs.dat). При валидном name-файле и заданном crc пишем копию
в кэш csprogsvers/<crc>.dat (write-back, как FTE COM_WriteFile в pr_csqc.c) —
следующие коннекты берут кэш, а не перекачивают. Возвращает true и заполняет
pathbuf путём для CSQC_Client_Load.
=================
*/
static qbool CSQC_Client_FindMainProgs (char *pathbuf, size_t bufsz,
	const char *name, int sizep, unsigned crc)
{
	extern void Sys_mkdir (const char *path);
	char buf[MAX_QPATH];
	const char *cands[3];
	int nc = 0;
	int i;

	if (crc)
	{
		snprintf (buf, sizeof (buf), "csprogsvers/%x.dat", crc);
		if (CSQC_Client_ValidateFile (buf, sizep, crc))
		{
			strlcpy (pathbuf, buf, bufsz);
			return true;
		}
	}

	if (name && name[0])
		cands[nc++] = name;
	if (!name || !name[0] || strcmp (name, "csprogs.dat"))
		cands[nc++] = "csprogs.dat";

	for (i = 0; i < nc; i++)
	{
		if (CSQC_Client_ValidateFile (cands[i], sizep, crc))
		{
			strlcpy (pathbuf, cands[i], bufsz);
			// FTE write-back: валидный name-файл копируем в кэш на будущее.
			if (crc && !cls.demoplayback)
			{
				byte *data;
				int len;
				char dest[MAX_OSPATH], dir[MAX_OSPATH];
				char *slash;
				FILE *f;
				data = (byte *)FS_LoadHunkFile ((char *)cands[i], &len);
				if (data)
				{
					snprintf (dest, sizeof (dest), "%s/csprogsvers/%x.dat",
						cls.gamedir, crc);
					strlcpy (dir, dest, sizeof (dir));
					slash = strrchr (dir, '/');
					if (slash && slash != dir)
					{
						*slash = 0;
						Sys_mkdir (dir);
					}
					f = fopen (dest, "wb");
					if (f)
					{
						fwrite (data, 1, len, f);
						fclose (f);
						Con_Printf ("CSQC: cached csprogsvers/%x.dat\n", crc);
					}
				}
			}
			return true;
		}
	}
	return false;
}

/*
=================
CSQC_Client_StartDownload

Запрашивает у сервера скачивание csprogs. Сервер отдаёт файл под *csprogsname
(mvdsv SV_LoadCSQC), но мы сохраняем его в отдельную папку-кэш
csprogsvers/<crc>.dat (как ftew, cl_parse.c:1640-1641), чтобы разные серверы не
перезатирали друг друга. ezquake CL_CheckOrDownloadFile не умеет разделять
remote/local имя — повторяем его стартовые шаги с другим локальным путём.
=================
*/
static void CSQC_Client_StartDownload (const char *remote, const char *localrel)
{
	extern void Sys_mkdir (const char *path);
	char dir[MAX_OSPATH];
	char *slash;

	if (cls.state < ca_connected || cls.demoplayback)
		return;

	snprintf (cls.downloadname, sizeof (cls.downloadname), "%s/%s", cls.gamedir, localrel);
	cls.downloadmethod = DL_QW;
	cls.downloadstarttime = Sys_DoubleTime ();
	COM_StripExtension (cls.downloadname, cls.downloadtempname, sizeof (cls.downloadtempname));
	strlcat (cls.downloadtempname, ".tmp", sizeof (cls.downloadtempname));

	// каталог назначения (напр. csprogsvers/) должен существовать
	strlcpy (dir, cls.downloadname, sizeof (dir));
	slash = strrchr (dir, '/');
	if (slash && slash != dir)
	{
		*slash = 0;
		Sys_mkdir (dir);
	}

	Com_Printf ("CSQC: downloading %s -> %s\n", remote, localrel);
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message, va ("download \"%s\"", remote));
	cls.downloadnumber++;
	s_csqc.csprogs_dl_start = Sys_DoubleTime ();
}

/*
=================
CSQC_Client_FreeArena / AllocArena

Клиентская арена edicts (ADR 0017 P1/D2): прямая карта entnum -> слот
(entity-значение PR1 = N*edict_size). Q_malloc (не hunk — урок Bug1);
free в Disconnect и в начале Load (защита от повторного вызова).
=================
*/
static void CSQC_Client_FreeArena (void)
{
	if (s_csqc.edicts)
	{
		Q_free (s_csqc.edicts);
		s_csqc.edicts = NULL;
	}
	if (s_csqc.game_edicts)
	{
		Q_free (s_csqc.game_edicts);
		s_csqc.game_edicts = NULL;
	}
}

static void CSQC_Client_AllocArena (pr1vm_t *vm)
{
	int i;

	CSQC_Client_FreeArena ();
	if (!vm || vm->edict_size <= 0)
		return;

	// FTE-пул: сброс занятости/номера-карты при (пере)выделении арены.
	memset (s_used, 0, sizeof (s_used));
	memset (s_own, 0, sizeof (s_own));
	memset (s_numslot, 0, sizeof (s_numslot));

	s_csqc.game_edicts = (byte *)Q_malloc ((size_t)CSQC_MAX_EDICTS * vm->edict_size);
	s_csqc.edicts = (edict_t *)Q_malloc (sizeof (edict_t) * CSQC_MAX_EDICTS);
	memset (s_csqc.game_edicts, 0, (size_t)CSQC_MAX_EDICTS * vm->edict_size);
	memset (s_csqc.edicts, 0, sizeof (edict_t) * CSQC_MAX_EDICTS);
	for (i = 0; i < CSQC_MAX_EDICTS; i++)
		s_csqc.edicts[i].v = (entvars_t *)(s_csqc.game_edicts + (size_t)i * vm->edict_size);

	vm->edicts = s_csqc.edicts;
	vm->game_edicts = s_csqc.game_edicts;
	vm->num_edicts = CSQC_MAX_EDICTS;
	vm->max_edicts = CSQC_MAX_EDICTS;
	vm->state = 0;	// клиентский инстанс; OP_ADDRESS-гард «world» не активен (world не пишем)
	vm->fieldofs_patch = NULL;	// FTE csprogs: raw field-оффсеты (ADR 0017 P2)
}

/*
=================
CSQC_Client_FindField

Ищет поле модуля по имени в fielddefs (см. PR1VM_FindFunction). Возвращает
смещение поля в float-словах от начала entvars (ddef_t.ofs) или -1.
=================
*/
int CSQC_Client_FindField (pr1vm_t *vm, const char *name)
{
	int i;

	if (!vm || !vm->fielddefs || !name)
		return -1;
	for (i = 0; i < vm->progs->numfielddefs; i++)
	{
		const char *s = PR1VM_GetString (vm, vm->fielddefs[i].s_name);
		if (s && s[0] && !strcmp (s, name))
			return vm->fielddefs[i].ofs;
	}
	return -1;
}

/*
=================
CSQC_Client_SetEntityContext

Ставит контекст сущности для CSQC_Ent_Update/Remove (ADR 0017 P2/D3):
self = entnum*edict_size (entity-значение PR1) и пишет float entnum в поле
.entnum (слот 7) арены. Модуль дальше читает self.entnum.
=================
*/
static void CSQC_Client_SetContextSlot (pr1vm_t *vm, unsigned slot, unsigned number)
{
	float *s;

	if (!vm || !vm->game_edicts)
		return;
	// self = slot*edict_size (entity-значение PR1, int-биты); .entnum (поле модуля)
	// = серверный номер (у своих spawn-сущностей номер не пишется — остаётся 0).
	if (s_csqc.global_self >= 0)
		*(int *)&vm->globals[s_csqc.global_self] = (int)slot * vm->edict_size;
	if (s_csqc.field_entnum >= 0 && slot < CSQC_MAX_EDICTS)
	{
		s = (float *)((byte *)vm->game_edicts + (size_t)slot * vm->edict_size + s_csqc.field_entnum * 4);
		s[0] = (float)number;
	}
}

/*
=================
CSQC_Client_EntAlloc / EntFree (FTE-пул)

Модульные сущности (builtin spawn) берут произвольный свободный слот пула
(первый свободный от 1) и помечаются s_own (remove разрешён только своим).
Сетевые слоты выделяются тем же пулом (без s_own) и держатся картой
номер→слот в ParseEntities. entity-значение PR1 = slot*edict_size.
=================
*/
static int CSQC_Client_AllocSlot (void)
{
	int i;
	for (i = 1; i < CSQC_MAX_EDICTS; i++)
		if (!s_used[i])
		{
			s_used[i] = true;
			s_own[i] = false;
			return i;
		}
	Con_Printf ("CSQC_Client_AllocSlot: pool full (%d)\n", CSQC_MAX_EDICTS - 1);
	return 0;
}

int CSQC_Client_EntAlloc (struct pr1vm_s *v)
{
	pr1vm_t *vm = (pr1vm_t *)v;
	int slot;
	(void)vm;
	slot = CSQC_Client_AllocSlot ();
	if (slot)
		s_own[slot] = true;	// spawn-сущность: .entnum не пишем (0)
	return slot;
}

void CSQC_Client_EntFree (struct pr1vm_s *v, int entnum)
{
	pr1vm_t *vm = (pr1vm_t *)v;
	float *s;

	if (!vm || !vm->game_edicts)
		return;
	if (entnum <= 0 || entnum >= CSQC_MAX_EDICTS || !s_used[entnum])
		return;
	if (!s_own[entnum])
		return;	// сетевая сущность — не трогаем (ADR 0017)
	s_used[entnum] = false;
	s_own[entnum] = false;
	s = (float *)((byte *)vm->game_edicts + (size_t)entnum * vm->edict_size);
	memset (s, 0, vm->edict_size);
}

/* внутренний сетевой путь (ParseEntities): слот без s_own */
int CSQC_Client_NetAllocSlot (void)
{
	return CSQC_Client_AllocSlot ();
}

void CSQC_Client_NetFreeSlot (int slot, int number)
{
	if (slot > 0 && slot < CSQC_MAX_EDICTS && s_used[slot])
	{
		s_used[slot] = false;
		s_own[slot] = false;
	}
	if (number > 0 && number < CSQC_MAX_NUM && s_numslot[number] == slot)
		s_numslot[number] = 0;
}

/* доступ/диагностика (P1d C1): обход пула и полей */
qbool CSQC_Client_EntUsed (int entnum)
{
	return (entnum > 0 && entnum < CSQC_MAX_EDICTS) ? s_used[entnum] : false;
}

int CSQC_Client_EntSpawnBase (void)
{
	return 1;	// первый используемый слот пула (0 — world)
}

int CSQC_Client_EntUsedCount (void)
{
	int i, n = 0;
	for (i = 1; i < CSQC_MAX_EDICTS; i++)
		if (s_used[i])
			n++;
	return n;
}

int CSQC_Client_NumToSlot (int number)
{
	return (number > 0 && number < CSQC_MAX_NUM) ? s_numslot[number] : 0;
}

int CSQC_Client_MapNumber (int number, int slot)
{
	if (number > 0 && number < CSQC_MAX_NUM)
		s_numslot[number] = slot;
	return slot;
}

/*
=================
player_localentnum (FTE pr_csqc.c:136-145)
=================

Публикация глобала модуля player_localentnum (номер наблюдаемого игрока) каждый
кадр перед CSQC_UpdateView. Это часть окружения builtins «как в FTE»: FTE публикует
глобал всегда; НО сущности игроков ezquake НЕ фабрикует (окружение сущностей = то,
что прислал сервер svc76 + свои spawn, как у FTE в отсутствие серверной эмиссии
игроков / player-delta). Зеркало игроков (бывш. Шаг 7.2) удалено — C7 self/play
N/A до серверной эмиссии игроков модом.
*/
void CSQC_Client_UpdateLocalEntnum (void)
{
	pr1vm_t *vm = &s_csqc.vm;

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored || s_csqc.g_localentnum < 0)
		return;
	vm->globals[s_csqc.g_localentnum] = (cl.viewplayernum >= 0) ? cl.viewplayernum + 1 : 0;
}

/*
=================
PR1VM_LoadClientV6

Client v6-loader (our csprogs.dat, classic QW version 6; v6 migration).
No CRC check; errors -> false + Con_Printf (no SV_Error). Implemented in the
client file (rule "client parts live outside shared core files").
=================
*/
static qbool PR1VM_LoadClientV6 (pr1vm_t *vm, const byte *data, int filesize)
{
	int version;

	if (!data || filesize < (int)sizeof(dprograms_t))
	{
		Con_Printf ("PR1VM_LoadClientV6: file too small (%d bytes)\n", filesize);
		return false;
	}

	// peek the raw LE version before byte-swapping
	version = LittleLong (((int *)(void *)data)[0]);
	if (version != PROG_VERSION)
	{
		Con_Printf ("PR1VM_LoadClientV6: not a QW v6 progs (version=%d)\n", version);
		return false;
	}

	PR1VM_LoadData (vm, (dprograms_t *)data);
	return true;
}

/*
=================
PR1VM_CSQCSmoke_f

PR1VM (S3, debug): loads csprogs.dat (classic v6, migration P1) from the current
gamedir into a static client instance, resolves CSQC functions and runs
CSQC_WorldLoaded (empty body — client builtins not wired yet, S5).
Debug command lives in the client file (rule "client parts live outside shared");
registered from CSQC_Client_RegisterCommands (cl_main.c: CL_InitLocal).
=================
*/
static pr1vm_t csqc_smoke_vm;

static void PR1VM_CSQCSmoke_f (void)
{
	byte *data;
	int filesize;
	pr1vm_t *vm = &csqc_smoke_vm;
	dfunction_t *f;
	func_t idx;

	data = (byte *)FS_LoadHunkFile ("csprogs.dat", &filesize);
	if (!data)
	{
		Con_Printf ("csqc_smoke: couldn't load csprogs.dat from gamedir\n");
		return;
	}

	// S6/P2.1: cleanup (incl. Q_free of builtin table), then reload
	PR1VM_UnLoad (vm);
	if (!PR1VM_LoadClientV6 (vm, data, filesize))
	{
		Con_Printf ("csqc_smoke: v6 load failed\n");
		return;
	}

	Con_Printf ("csqc_smoke: client (v6): statements=%d functions=%d globals=%d"
		" (server PR1: statements=%d functions=%d)\n",
		vm->progs->numstatements, vm->progs->numfunctions, vm->progs->numglobals,
		progs ? progs->numstatements : -1, progs ? progs->numfunctions : -1);

	// P2.1: client builtin table (layer C)
	CSQCVM_RegisterBuiltins (vm);

	f = PR1VM_FindFunction (vm, "CSQC_Init");
	Con_Printf ("csqc_smoke: CSQC_Init %s\n", f ? "found" : "MISSING");
	if (f)
	{
		idx = (func_t)(f - vm->functions);
		vm->globals[OFS_PARM0] = 0;
		vm->globals[OFS_PARM1] = 0;
		vm->globals[OFS_PARM2] = 0;
		PR1VM_ExecuteProgram (vm, idx);
		Con_Printf ("csqc_smoke: CSQC_Init executed ok (registercommand builtins)\n");
	}
	f = PR1VM_FindFunction (vm, "CSQC_WorldLoaded");
	Con_Printf ("csqc_smoke: CSQC_WorldLoaded %s\n", f ? "found" : "MISSING");
	if (f)
	{
		idx = (func_t)(f - vm->functions);
		PR1VM_ExecuteProgram (vm, idx);
		Con_Printf ("csqc_smoke: CSQC_WorldLoaded executed ok (server PR1 still alive)\n");
	}
	f = PR1VM_FindFunction (vm, "CSQC_ConsoleCommand");
	Con_Printf ("csqc_smoke: CSQC_ConsoleCommand %s\n", f ? "found" : "MISSING");
	if (f)
	{
		idx = (func_t)(f - vm->functions);
		vm->globals[OFS_PARM0] = 0;	// empty command
		vm->globals[OFS_RETURN] = -1;
		PR1VM_ExecuteProgram (vm, idx);
		Con_Printf ("csqc_smoke: CSQC_ConsoleCommand ok (ret=%.0f, tokenize/argv builtins)\n",
			vm->globals[OFS_RETURN]);
	}
	// P2.2: weapon_name(0) -> ftos(0)="0" (builtin ftos + string return)
	f = PR1VM_FindFunction (vm, "weapon_name");
	if (f)
	{
		idx = (func_t)(f - vm->functions);
		vm->globals[OFS_PARM0] = 0;
		vm->globals[OFS_RETURN] = 0;
		PR1VM_ExecuteProgram (vm, idx);
		Con_Printf ("csqc_smoke: weapon_name(0) -> \"%s\" (ftos builtin)\n",
			PR1VM_GetString (vm, *(int *)&vm->globals[OFS_RETURN]));
	}
}

/*
=================
CSQC_Client_Load

Загружает csprogs (path из gamedir; локальный файл или только что скачанный
csprogsvers/<crc>.dat) в клиентский инстанс и вызывает CSQC_Init. Возвращает
true при успехе. При неудаче печатает причину.
=================
*/
static qbool CSQC_Client_Load (const char *path)
{
	byte *data;
	int filesize;
	pr1vm_t *vm;
	dfunction_t *f;

	data = (byte *)FS_LoadHunkFile ((char *)path, &filesize);
	if (!data)
	{
		Con_Printf ("CSQC: server offers csprogs but %s not found locally\n", path);
		return false;
	}

	// Защита от повторного Load (арена из прошлой загрузки) до memset.
	CSQC_Client_FreeArena ();
	// C2.2: string-buffers чистить при новой загрузке модуля.
	CSQC_Client_BufReset ();

	memset (&s_csqc, 0, sizeof (s_csqc));
	s_csqc.func_init = s_csqc.func_world = s_csqc.func_update =
		s_csqc.func_console = s_csqc.func_shutdown = -1;
	s_csqc.func_entupdate = s_csqc.func_entremove = s_csqc.func_parseevent = -1;
	s_csqc.func_input = -1;
	s_csqc.func_inputevent = -1;
	s_csqc.global_time = -1;
	s_csqc.global_self = -1;
	s_csqc.field_entnum = -1;
	s_csqc.f_origin = s_csqc.f_velocity = s_csqc.f_mins = s_csqc.f_maxs = -1;
	s_csqc.f_movetype = s_csqc.f_flags = s_csqc.f_gravity = s_csqc.f_pmove_flags = -1;
	s_csqc.g_localentnum = -1;
	s_csqc.in_timelength = s_csqc.in_angles = s_csqc.in_movevalues = -1;
	s_csqc.in_buttons = s_csqc.in_impulse = -1;
	s_csqc.in_sequence = -1;
	s_csqc.g_ccframe = s_csqc.g_scframe = -1;
	s_csqc.p_org = s_csqc.p_vel = s_csqc.p_onground = -1;
	s_csqc.g_vfwd = s_csqc.g_vright = s_csqc.g_vup = -1;
	s_last_seq = 0;

	vm = &s_csqc.vm;
	vm->host_error = CSQC_Client_HostError;
	vm->host_print = CSQC_Client_HostPrint;

	if (!PR1VM_LoadClientV6 (vm, data, filesize))
	{
		Con_Printf ("CSQC: %s load failed (v6)\n", path);
		return false;
	}

	CSQCVM_RegisterBuiltins (vm);

	// P1/D2: арена edicts клиентского инстанса (edict_size известен после load).
	CSQC_Client_AllocArena (vm);

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
	f = PR1VM_FindFunction (vm, "CSQC_Input_Frame");
	if (f)
		s_csqc.func_input = (int)(f - vm->functions);
	f = PR1VM_FindFunction (vm, "CSQC_InputEvent");
	if (f)
		s_csqc.func_inputevent = (int)(f - vm->functions);

	s_csqc.global_time = PR1VM_FindGlobal (vm, "time");
	// P2/D3: self-глобал и поле .entnum (движок пишет их при entity-вызовах).
	s_csqc.global_self = PR1VM_FindGlobal (vm, "self");
	s_csqc.field_entnum = CSQC_Client_FindField (vm, "entnum");
	// C1.4 #347: поля стандартной физики (если есть в схеме модуля).
	s_csqc.f_origin = CSQC_Client_FindField (vm, "origin");
	s_csqc.f_velocity = CSQC_Client_FindField (vm, "velocity");
	s_csqc.f_mins = CSQC_Client_FindField (vm, "mins");
	s_csqc.f_maxs = CSQC_Client_FindField (vm, "maxs");
	s_csqc.f_movetype = CSQC_Client_FindField (vm, "movetype");
	s_csqc.f_flags = CSQC_Client_FindField (vm, "flags");
	s_csqc.f_gravity = CSQC_Client_FindField (vm, "gravity");
	s_csqc.f_pmove_flags = CSQC_Client_FindField (vm, "pmove_flags");
	s_csqc.g_localentnum = PR1VM_FindGlobal (vm, "player_localentnum");

	// input_* глобалы для CSQC_Input_Frame (csdefs.qc: input_timelength/angles/
	// movevalues/buttons/impulse). Резолвим только объявленные модулем.
	s_csqc.in_timelength = PR1VM_FindGlobal (vm, "input_timelength");
	s_csqc.in_angles = PR1VM_FindGlobal (vm, "input_angles");
	s_csqc.in_movevalues = PR1VM_FindGlobal (vm, "input_movevalues");
	s_csqc.in_buttons = PR1VM_FindGlobal (vm, "input_buttons");
	s_csqc.in_impulse = PR1VM_FindGlobal (vm, "input_impulse");
	s_csqc.in_sequence = PR1VM_FindGlobal (vm, "input_sequence");
	// C5-A: глобалы окна предикции + deprec pmove_* (csdefs.qc:50-51,69-71).
	s_csqc.g_ccframe = PR1VM_FindGlobal (vm, "clientcommandframe");
	s_csqc.g_scframe = PR1VM_FindGlobal (vm, "servercommandframe");
	s_csqc.p_org = PR1VM_FindGlobal (vm, "pmove_org");
	s_csqc.p_vel = PR1VM_FindGlobal (vm, "pmove_vel");
	s_csqc.p_onground = PR1VM_FindGlobal (vm, "pmove_onground");
	// #1 makevectors (C6.1): цели записи v_forward/v_right/v_up (FTE-паритет).
	s_csqc.g_vfwd = PR1VM_FindGlobal (vm, "v_forward");
	s_csqc.g_vright = PR1VM_FindGlobal (vm, "v_right");
	s_csqc.g_vup = PR1VM_FindGlobal (vm, "v_up");

	s_csqc.loaded = true;

	Con_Printf ("CSQC: loaded %s (%d statements, crc=0x%x), funcs i=%d w=%d u=%d "
		"c=%d s=%d eu=%d er=%d pe=%d if=%d ie=%d time=%d\n",
		path, vm->progs->numstatements, (unsigned int)vm->progs->crc, s_csqc.func_init,
		s_csqc.func_world, s_csqc.func_update, s_csqc.func_console, s_csqc.func_shutdown,
		s_csqc.func_entupdate, s_csqc.func_entremove, s_csqc.func_parseevent,
		s_csqc.func_input, s_csqc.func_inputevent, s_csqc.global_time);
	Con_Printf ("CSQC: P2 self=%d entnum_fld=%d edict_size=%d\n",
		s_csqc.global_self, s_csqc.field_entnum, vm->edict_size);

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
	const char *name, *crcs;
	unsigned crc;
	int sizep;
	char path[MAX_QPATH];

	// Мастер-выключатель (аналог FTE cl_nocsqc): 0 — весь CSQC отключён,
	// модуль не грузится, клиент ведёт себя как раньше.
	if (!cl_pext_csqc.value)
		return;

	sizep = (int)strtoul (Info_ValueForKey (cl.serverinfo, "*csprogssize"), NULL, 0);
	if (sizep <= 0)
		return;		// обычный сервер без CSQC (или PR1-гейт сервера)

	crcs = Info_ValueForKey (cl.serverinfo, "*csprogs");
	crc = (unsigned)strtoul (crcs, NULL, 0);
	name = Info_ValueForKey (cl.serverinfo, "*csprogsname");
	if (!name || !name[0])
		name = "csprogs.dat";

	// Модуль загружается «с нуля» на КАЖДЫЙ вход в мир (первый коннект и каждая
	// смена карты): выгрузка происходит при выходе из мира (CL_ClearState, до
	// Host_ClearMemory), здесь — загрузка свежего csprogs. Защитный unload на
	// случай путей без CL_ClearState (двойной вызов безопасен — no-op).
	if (s_csqc.loaded)
		CSQC_Client_Disconnect ();

	// Локальные кандидаты по FTE-семантике (CSQC_FindMainProgs, pr_csqc.c):
	// 1) кэш csprogsvers/<crc>.dat, 2) *csprogsname (+ фолбэк csprogs.dat);
	// при валидном name-файле делается write-back копии в кэш.
	if (CSQC_Client_FindMainProgs (path, sizeof (path), name, sizep, crc))
	{
		if (!CSQC_Client_Load (path))
			return;
		if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
			return;
		// Вход в новую карту: per-карта состояние чистое (WorldLoaded/enablecsqc
		// будут этой карты; модуль уже новый).
		memset (s_csqc.seen, 0, sizeof (s_csqc.seen));
		s_csqc.world_done = false;
		s_csqc.enable_sent = false;
		return;
	}

	// Валидного локального нет — качаем с сервера: сервер отдаёт *csprogsname,
	// сохраняем в отдельную папку csprogsvers/<crc>.dat (не перезатираем чужие).
	// Загрузка модуля произойдёт в CSQC_Client_Update, когда файл появится.
	s_csqc.csprogs_crc = crc;
	s_csqc.csprogs_size = sizep;
	if (crc)
		snprintf (s_csqc.csprogs_dl_path, sizeof (s_csqc.csprogs_dl_path), "csprogsvers/%x.dat", crc);
	else
		snprintf (s_csqc.csprogs_dl_path, sizeof (s_csqc.csprogs_dl_path), "%s", name);
	CSQC_Client_StartDownload (name, s_csqc.csprogs_dl_path);
	s_csqc.csprogs_dl_pending = true;
}

/*
=================
C5-A: окно предикции EXT_CSQC_1 — значения глобалов модуля
clientcommandframe/servercommandframe (csdefs.qc:50-51).

- clientcommandframe = «следующий формируемый» клиентский кадр =
  cls.netchan.outgoing_sequence (Netchan_Transmit инкрементирует ПОСЛЕ записи
  заголовка — net_chan.c:316-319, поэтому во время CL_SendCmd outgoing_sequence
  ещё равен номеру текущего cmd; после — следующего).
- servercommandframe = последний подтверждённый сервером клиентский кадр =
  cl.parsecount (CL_ParseClientdata ставит его в cls.netchan.incoming_acknowledged,
  cl_parse.c:2050-2054) — аналог FTE QW ackedmovesequence.
- Предикция недоступна (0): демо/MVD, не ca_active, до первого принятого
  серверного кадра (cl.validsequence == 0; client.h:647-650).
- Окно (servercommandframe, clientcommandframe] — контракт модуля (движок его
  не проверяет; спека ext_csqc_1.txt:262) — см. CSQC_Client_ApplyInput.
=================
*/
static float CSQC_Client_ClientCmdFrame (void)
{
	if (!s_csqc.loaded || s_csqc.errored)
		return 0;
	if (cls.state != ca_active || cls.demoplayback || cls.mvdplayback)
		return 0;
	return (float)cls.netchan.outgoing_sequence;
}

static float CSQC_Client_ServerCmdFrame (void)
{
	if (!s_csqc.loaded || s_csqc.errored)
		return 0;
	if (cls.state != ca_active || cls.demoplayback || cls.mvdplayback)
		return 0;
	if (!cl.validsequence)
		return 0;	// ни одного принятого серверного кадра (преспаун)
	return (float)cl.parsecount;
}

static void CSQC_Client_PatchFrames (void)
{
	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;
	if (s_csqc.g_ccframe >= 0)
		s_csqc.vm.globals[s_csqc.g_ccframe] = CSQC_Client_ClientCmdFrame ();
	if (s_csqc.g_scframe >= 0)
		s_csqc.vm.globals[s_csqc.g_scframe] = CSQC_Client_ServerCmdFrame ();
}

/*
=================
CSQC_Client_Update

Вызывается каждый 2D-кадр (HUD-фаза, cl_screen.c). WorldLoaded — один раз
после входа в мир; далее CSQC_UpdateView(vid.width, vid.height, menushown).
Если модуль ждёт скачивания csprogs — при появлении валидного файла грузит
его и продолжает как при входе в мир.
=================
*/
void CSQC_Client_Update (void)
{
	pr1vm_t *vm = &s_csqc.vm;

	if (cls.state != ca_active)
		return;

	// Clip-состояние (#324/325) — пер-кадр (модуль ставит/снимает в своём кадре).
	s_clip_on = false;

	// Ожидание скачанного csprogs (валидный файл появился -> грузим).
	if (s_csqc.csprogs_dl_pending)
	{
		char path[MAX_QPATH];

		if (CSQC_Client_ValidateFile (s_csqc.csprogs_dl_path,
			s_csqc.csprogs_size, s_csqc.csprogs_crc))
		{
			strlcpy (path, s_csqc.csprogs_dl_path, sizeof (path));
			s_csqc.csprogs_dl_pending = false;
			if (!CSQC_Client_Load (path))
				return;
			if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
				return;
			// как при входе в мир: per-карта состояние чистое
			memset (s_csqc.seen, 0, sizeof (s_csqc.seen));
			s_csqc.world_done = false;
			s_csqc.enable_sent = false;
		}
		else
		{
			// файла всё ещё нет: если скачивание не идёт и прошло >20 c — сдаёмся
			if (Sys_DoubleTime () - s_csqc.csprogs_dl_start > 20)
			{
				s_csqc.csprogs_dl_pending = false;
				Con_Printf ("CSQC: csprogs download failed/timed out\n");
			}
			return;
		}
	}

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
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

	// player_localentnum — публикуем до модуля (окружение builtins как FTE;
	// сущности игроков не фабрикуем — см. CSQC_Client_UpdateLocalEntnum).
	CSQC_Client_UpdateLocalEntnum ();
	// C5-A: окно предикции модулю (перед CSQC_UpdateView; FTE pr_csqc.c:8837-8844).
	CSQC_Client_PatchFrames ();

	if (s_csqc.func_update > 0)
	{
		vm->globals[OFS_PARM0] = vid.width;
		vm->globals[OFS_PARM1] = vid.height;
		vm->globals[OFS_PARM2] = (key_dest == key_menu) ? 1 : 0;
		CSQC_Client_Exec (s_csqc.func_update);
	}

	// C1.2: при активном CSQC-курсоре — абсолютная позиция мыши модулю, только
	// когда она изменилась с прошлого кадра (как FTE: события на перемещение).
	{
		static float ie_abs_lastx = -1, ie_abs_lasty = -1;
		float cmx = 0, cmy = 0;

		CSQC_Client_GetCursorPos (&cmx, &cmy);
		if (CSQC_Client_CSQCCursor ())
		{
			if (cmx != ie_abs_lastx || cmy != ie_abs_lasty)
			{
				ie_abs_lastx = cmx;
				ie_abs_lasty = cmy;
				CSQC_Client_InputEvent (IE_MOUSEABS, cmx, cmy, 0);
			}
		}
		else
		{
			ie_abs_lastx = ie_abs_lasty = -1;	// курсор снят — сброс
		}
	}
}

/*
=================
CSQC_Client_ParseEntities

Парсинг svc_fte_csqcentities(76)/sized(92):
для каждой сущности — short entnum, бит 0x8000 = remove, 0 = конец.
Update: CSQC_Ent_Update(isnew) — модуль читает payload из текущего сообщения
(read*); контекст сущности (self/.entnum) движок ставит перед вызовом
(ADR 0017 P2/D3). Remove: CSQC_Ent_Remove с self/.entnum (без builtin-стрима).
Sized (92, только mvdsv под sv_csqcdebug): перед payload каждой update-сущности
идёт short-длина — skip-защита от рассинхрона (E3).
=================
*/
void CSQC_Client_ParseEntities (qbool sized)
{
	pr1vm_t *vm = &s_csqc.vm;
	unsigned int entnum;
	qbool removeflag;

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
			break;
		if (entnum >= (unsigned int)(sizeof (s_csqc.seen) / sizeof (s_csqc.seen[0])))
			break;

		if (removeflag)
		{
			if (s_csqc.func_entremove > 0)
			{
				// P2/D3: контекст (self=slot, .entnum=номер), без builtin-стрима.
				{
					int slot = CSQC_Client_NumToSlot ((int)entnum);
					if (slot)
					{
						CSQC_Client_SetContextSlot (vm, (unsigned)slot, entnum);
						CSQC_Client_Exec (s_csqc.func_entremove);
						CSQC_Client_NetFreeSlot (slot, (int)entnum);
					}
				}
			}
			s_csqc.seen[entnum] = false;
			continue;
		}

		if (s_csqc.func_entupdate > 0)
		{
			int payload_start;
			int payload_len = -1;

			vm->globals[OFS_PARM0] = s_csqc.seen[entnum] ? 0 : 1;
			s_csqc.seen[entnum] = true;

			// P2/D3 + FTE-пул: номер→слот; новый номер — выделить слот пула,
			// контекст (self=slot, .entnum=номер).
			{
				int slot = CSQC_Client_NumToSlot ((int)entnum);
				if (!slot)
				{
					slot = CSQC_Client_NetAllocSlot ();
					if (!slot)
					{
						Con_Printf ("CSQC: pool full, entity %u dropped\n", entnum);
						break;	// патологично (пул 4095); рассинхрон невозможен при чтении
					}
					CSQC_Client_MapNumber ((int)entnum, slot);
					// FTE-пул Шаг 5 (диагностика): номер → слот пула; печать
					// ограничена, чтобы серверный churn remove/update не залил
					// консоль (≤32 строк на сессию csqc_dbg>=3).
					{
						static int s_dbg_lines = 0;
						cvar_t *dbg = Cvar_Find ("csqc_dbg");
						if (dbg && dbg->value >= 3)
						{
							if (s_dbg_lines < 32)
							{
								Con_Printf ("CSQC ent num %u -> slot %d\n", entnum, slot);
								s_dbg_lines++;
							}
						}
						else
							s_dbg_lines = 0;
					}
				}
				CSQC_Client_SetContextSlot (vm, (unsigned)slot, entnum);
			}

			// Sized: перед payload — short-длина (mvdsv sv_ents.c:700).
			payload_start = msg_readcount;
			if (sized)
				payload_len = MSG_ReadShort ();

			CSQC_Client_Exec (s_csqc.func_entupdate);
			if (s_csqc.errored)
				return;

			// Skip-защита: если модуль прочитал меньше payload_len — дочитать.
			if (payload_len >= 0)
			{
				int used = msg_readcount - payload_start;
				if (used < payload_len)
					MSG_ReadSkip (payload_len - used);
			}
		}
	}
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
CSQC_Client_InputFrame

CSQC_Input_Frame: вызывается перед отправкой каждого usercmd (CL_SendCmd,
cl_input.c). Механика FTE (pr_csqc.c:9418 CSQC_Input_Frame + cs_set/get_input_state,
:3875-4010) на подмножестве input_*-глобалов, объявленных модулем (csdefs.qc:
input_timelength/angles/movevalues/buttons/impulse): движок заполняет их из cmd,
исполняет CSQC_Input_Frame, затем пишет изменения обратно в cmd.

Отличия от FTE:
- usercmd.angles в ezquake — float-градусы (не short), конвертацию делает
  MSG_WriteAngle16 в MSG_WriteDeltaUsercmd (com_msg.c:237) — здесь копируем напрямую;
- input_timelength = msec/1000 (cl.gamespeed в ezquake QW нет — FTE множит на него).
=================
*/
void CSQC_Client_InputFrame (usercmd_t *cmd)
{
	pr1vm_t *vm = &s_csqc.vm;

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored || s_csqc.func_input <= 0)
		return;

	// C5-A: окно предикции до CSQC_Input_Frame (clientcommandframe = текущий cmd;
	// FTE pr_csqc.c:9430-9431).
	CSQC_Client_PatchFrames ();

	CSQC_Client_SetTime ();

	// cmd -> input_* глобалы (только объявленные модулем).
	if (s_csqc.in_timelength >= 0)
		vm->globals[s_csqc.in_timelength] = cmd->msec / 1000.0f;
	if (s_csqc.in_angles >= 0)
	{
		vm->globals[s_csqc.in_angles + 0] = cmd->angles[0];
		vm->globals[s_csqc.in_angles + 1] = cmd->angles[1];
		vm->globals[s_csqc.in_angles + 2] = cmd->angles[2];
	}
	if (s_csqc.in_movevalues >= 0)
	{
		vm->globals[s_csqc.in_movevalues + 0] = cmd->forwardmove;
		vm->globals[s_csqc.in_movevalues + 1] = cmd->sidemove;
		vm->globals[s_csqc.in_movevalues + 2] = cmd->upmove;
	}
	if (s_csqc.in_buttons >= 0)
		vm->globals[s_csqc.in_buttons] = cmd->buttons;
	if (s_csqc.in_impulse >= 0)
		vm->globals[s_csqc.in_impulse] = cmd->impulse;

	if (!CSQC_Client_Exec (s_csqc.func_input))
		return;		// errored — кадры отключены, cmd не трогаем

	// input_* глобалы -> cmd (записываем только то, что изменил модуль).
	if (s_csqc.in_timelength >= 0)
	{
		int msec = (int)(vm->globals[s_csqc.in_timelength] * 1000.0f);
		if (msec < 1)
			msec = 1;
		else if (msec > 255)
			msec = 255;
		cmd->msec = (byte)msec;
	}
	if (s_csqc.in_angles >= 0)
	{
		cmd->angles[0] = vm->globals[s_csqc.in_angles + 0];
		cmd->angles[1] = vm->globals[s_csqc.in_angles + 1];
		cmd->angles[2] = vm->globals[s_csqc.in_angles + 2];
	}
	if (s_csqc.in_movevalues >= 0)
	{
		cmd->forwardmove = (short)vm->globals[s_csqc.in_movevalues + 0];
		cmd->sidemove = (short)vm->globals[s_csqc.in_movevalues + 1];
		cmd->upmove = (short)vm->globals[s_csqc.in_movevalues + 2];
	}
	if (s_csqc.in_buttons >= 0)
		cmd->buttons = (byte)vm->globals[s_csqc.in_buttons];
	if (s_csqc.in_impulse >= 0)
		cmd->impulse = (byte)vm->globals[s_csqc.in_impulse];
}

/*
=================
CSQC_Client_RecordInput / CSQC_Client_ApplyInput

C5-A #345: история отправленных usercmd. CL_SendCmd записывает каждый
отправленный cmd (CSQC_Client_RecordInput); builtin #345(seq) запрашивает его и
заполняет input_* глобалы (CSQC_Client_ApplyInput).

seq = зеркало cls.netchan.outgoing_sequence (номер клиентского сообщения на
момент записи; Netchan_Transmit инкрементирует после записи заголовка). Это и
есть тот номер, который подтверждает сервер (servercommandframe = incoming_
acknowledged = cl.parsecount) — окно (servercommandframe, clientcommandframe]
согласовано в одной нумерации. Отличие от FTE: у нас ring-история (64) + запись
только живого пути CL_SendCmd (демо/MVD не записываются); живой pending-кадр
#345(clientcommandframe) вне CSQC_Input_Frame недоступен — модуль берёт текущий
cmd из input_* (движок выставляет их до CSQC_Input_Frame). NQ-механизм
ackedmovesequence (PEXT2_PREDINFO) недостижим (не для QW).
=================
*/
void CSQC_Client_RecordInput (usercmd_t *cmd)
{
	unsigned int seq;

	seq = (unsigned int)cls.netchan.outgoing_sequence;
	s_last_seq = seq;
	s_inhist[seq % CSQC_INHIST].seq = seq;
	s_inhist[seq % CSQC_INHIST].cmd = *cmd;
	if (s_csqc.loaded && !s_csqc.errored)
	{
		if (s_csqc.in_sequence >= 0)
			s_csqc.vm.globals[s_csqc.in_sequence] = seq;
		if (s_csqc.g_ccframe >= 0)
			s_csqc.vm.globals[s_csqc.g_ccframe] = seq;
	}
}

static void CSQC_Client_FillInputFromCmd (usercmd_t *cmd)
{
	pr1vm_t *vm = &s_csqc.vm;

	if (s_csqc.in_timelength >= 0)
		vm->globals[s_csqc.in_timelength] = cmd->msec / 1000.0f;
	if (s_csqc.in_angles >= 0)
	{
		vm->globals[s_csqc.in_angles + 0] = cmd->angles[0];
		vm->globals[s_csqc.in_angles + 1] = cmd->angles[1];
		vm->globals[s_csqc.in_angles + 2] = cmd->angles[2];
	}
	if (s_csqc.in_movevalues >= 0)
	{
		vm->globals[s_csqc.in_movevalues + 0] = cmd->forwardmove;
		vm->globals[s_csqc.in_movevalues + 1] = cmd->sidemove;
		vm->globals[s_csqc.in_movevalues + 2] = cmd->upmove;
	}
	if (s_csqc.in_buttons >= 0)
		vm->globals[s_csqc.in_buttons] = cmd->buttons;
	if (s_csqc.in_impulse >= 0)
		vm->globals[s_csqc.in_impulse] = cmd->impulse;
}

int CSQC_Client_ApplyInput (unsigned int seq)
{
	unsigned int i;
	csqc_inrec_t *r;

	if (!s_csqc.loaded || s_csqc.errored)
		return 0;
	if (!seq)
		return 0;
	// C5-A: paused-guard как FTE (pr_csqc.c:4142) — на серверной паузе кадры
	// окна не применяются. Диапазон (servercommandframe, clientcommandframe]
	// движок не проверяет (контракт модуля; спека ext_csqc_1.txt:262) — здесь
	// только живучесть кольца.
	if ((cl.paused & PAUSED_SERVER) && seq >= (unsigned)CSQC_Client_ServerCmdFrame ())
		return 0;
	for (i = 0; i < CSQC_INHIST; i++)
	{
		r = &s_inhist[i];
		if (r->seq == seq)
		{
			CSQC_Client_FillInputFromCmd (&r->cmd);
			if (s_csqc.in_sequence >= 0)
				s_csqc.vm.globals[s_csqc.in_sequence] = seq;
			return 1;
		}
	}
	return 0;
}

/*
=================
CSQC_Client_MakeVectors

#1 makevectors (C6.1; FTE-паритет PF_cs_makevectors, pr_csqc.c:669): по вектору
углов пишет v_forward/v_right/v_up модуля (глобалы, резолв в Load). Если модуль
их не объявил — no-op (offset -1).
=================
*/
void CSQC_Client_MakeVectors (float *ang)
{
	pr1vm_t *vm = &s_csqc.vm;
	float *f, *r, *u;

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;
	if (s_csqc.g_vfwd < 0 || s_csqc.g_vright < 0 || s_csqc.g_vup < 0)
		return;		// модуль не объявил v_forward/v_right/v_up
	f = &vm->globals[s_csqc.g_vfwd];
	r = &vm->globals[s_csqc.g_vright];
	u = &vm->globals[s_csqc.g_vup];
	AngleVectors (ang, f, r, u);
}

/*
=================
CSQC_Client_HasInputEvent / CSQC_Client_InputEvent

C1.2: доставка событий ввода модулю (CSQC_InputEvent, csdefs.qc:159). Вызывается
из keys.c (клавиши/клики/колесо при key_dest == key_game) и in_sdl2.c (мышь:
MOUSEDELTA в обычном режиме; MOUSEABS — из CSQC_Client_Update при CSQCCursor).
Возврат модуля != 0 означает «событие обработано» (движок не применяет его).
=================
*/
qbool CSQC_Client_HasInputEvent (void)
{
	return s_csqc.loaded && s_csqc.inited && !s_csqc.errored
		&& s_csqc.func_inputevent > 0;
}

int CSQC_Client_InputEvent (int evtype, float a, float b, float c)
{
	pr1vm_t *vm = &s_csqc.vm;

	if (!CSQC_Client_HasInputEvent ())
		return 0;
	// Параметры модульной функции (4 float) — как CSQC_UpdateView.
	vm->globals[OFS_PARM0] = evtype;
	vm->globals[OFS_PARM1] = a;
	vm->globals[OFS_PARM2] = b;
	vm->globals[OFS_PARM3] = c;
	if (!CSQC_Client_Exec (s_csqc.func_inputevent))
		return 0;
	return (int)vm->globals[OFS_RETURN];
}

/*
=================
CSQC_Client_RunPlayerPhysics

C1.4 #347 runstandardplayerphysics(ent): минимум-паритет — гоняет клиентский
PM_PlayerMove на сущности модуля из её полей + последнего отправленного usercmd
(как FTE PF_cs_runplayerphysics, но по cl_pred-пути ezquake). Поля сущности,
которых нет в схеме, берутся дефолтами. Полная физика/предикция — C5.
=================
*/
#define CSQC_MV_WALK	3	// csdefs.qc MOVETYPE_* (FTE-нумерация)
#define CSQC_MV_FLY		5
#define CSQC_MV_NOCLIP	8

void CSQC_Client_RunPlayerPhysics (int entnum)
{
	pr1vm_t *vm = &s_csqc.vm;
	float *base, *o, *v;
	int msecs, mt;
	usercmd_t *uc;

	if (!s_csqc.loaded || !s_csqc.inited || s_csqc.errored)
		return;
	if (entnum < 0 || entnum >= CSQC_MAX_EDICTS || cls.state != ca_active)
		return;
	if (cls.demoplayback || cls.mvdplayback)
		return;		// только живая игра (physents из cl)

	base = (float *)((byte *)vm->game_edicts + (size_t)entnum * vm->edict_size);

	// Последний записанный usercmd (RecordInput) — команда для физики.
	if (!s_last_seq)
		return;
	uc = &s_inhist[s_last_seq % CSQC_INHIST].cmd;

	// Собираем pmove (глобальный playermove_t) как FTE: поля ent + input cmd.
	// Размеры игрока — глобальные player_mins/player_maxs pmove.c (стандарт).
	memset (&pmove, 0, sizeof (pmove));
	pmove.cmd = *uc;
	if (s_csqc.f_origin >= 0)
		memcpy (pmove.origin, base + s_csqc.f_origin, sizeof (pmove.origin));
	else
		VectorClear (pmove.origin);
	if (s_csqc.f_velocity >= 0)
		memcpy (pmove.velocity, base + s_csqc.f_velocity, sizeof (pmove.velocity));
	else
		VectorClear (pmove.velocity);

	mt = (s_csqc.f_movetype >= 0) ? (int)base[s_csqc.f_movetype] : CSQC_MV_WALK;
	switch (mt)
	{
	case CSQC_MV_FLY:
		pmove.pm_type = PM_FLY;
		break;
	case CSQC_MV_NOCLIP:
		pmove.pm_type = PM_SPECTATOR;
		break;
	default:
		pmove.pm_type = PM_NORMAL;
		break;
	}
	pmove.jump_held = (s_csqc.f_pmove_flags >= 0) ? (base[s_csqc.f_pmove_flags] != 0) : false;

	movevars.entgravity = cl.entgravity;
	movevars.maxspeed = cl.maxspeed;
	movevars.bunnyspeedcap = cl.bunnyspeedcap;

	// physents-минимум: мир + игроки (клиентский путь cl_pred).
	CL_SetSolidPlayers (cl.playernum);

	msecs = uc->msec;
	if (msecs <= 0)
		msecs = 1;
	while (msecs > 0)
	{
		int step = (msecs > 255) ? 255 : msecs;
		pmove.cmd.msec = step;
		PM_PlayerMove ();
		msecs -= step;
	}

	// Результат обратно в ent (доступные поля).
	o = (s_csqc.f_origin >= 0) ? base + s_csqc.f_origin : NULL;
	v = (s_csqc.f_velocity >= 0) ? base + s_csqc.f_velocity : NULL;
	if (o)
		memcpy (o, pmove.origin, 3 * sizeof (float));
	if (v)
		memcpy (v, pmove.velocity, 3 * sizeof (float));
}

/*
=================
C2.2 — string-buffers (#460-469). Хранилище — s_bufs (handle = idx+1).
=================
*/
static csqc_buf_t *CSQC_Client_BufAt (int handle)
{
	if (handle < 1 || handle > CSQC_MAX_BUFS || !s_bufs[handle - 1].inuse)
		return NULL;
	return &s_bufs[handle - 1];
}

static void CSQC_Client_BufClear (csqc_buf_t *b)
{
	int i;
	for (i = 0; i < b->num; i++)
	{
		if (b->str[i])
			Q_free (b->str[i]);
	}
	Q_free (b->str);
	b->str = NULL;
	b->num = b->cap = 0;
}

int CSQC_Client_BufCreate (void)
{
	int i;
	for (i = 0; i < CSQC_MAX_BUFS; i++)
	{
		if (!s_bufs[i].inuse)
		{
			memset (&s_bufs[i], 0, sizeof (s_bufs[i]));
			s_bufs[i].inuse = true;
			return i + 1;
		}
	}
	return 0;
}

void CSQC_Client_BufDel (int handle)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	if (b)
	{
		CSQC_Client_BufClear (b);
		b->inuse = false;
	}
}

int CSQC_Client_BufGetSize (int handle)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	return b ? b->num : 0;
}

static int CSQC_Client_BufPush (csqc_buf_t *b, const char *s)
{
	char **ns;
	if (b->num >= b->cap)
	{
		int ncap = b->cap ? b->cap * 2 : 8;
		ns = (char **)Q_realloc (b->str, sizeof (char *) * ncap);
		if (!ns)
			return -1;
		b->str = ns;
		b->cap = ncap;
	}
	b->str[b->num] = Q_strdup (s ? s : "");
	return b->num;
}

int CSQC_Client_BufAdd (int handle, const char *s, int order)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	int idx, i;
	if (!b)
		return -1;
	idx = CSQC_Client_BufPush (b, s);
	if (idx < 0)
		return -1;
	// order > 0 — вставка на позицию (не дальше конца списка).
	if (order > 0 && order < idx)
	{
		char *tmp = b->str[idx];
		for (i = idx; i > order; i--)
			b->str[i] = b->str[i - 1];
		b->str[order] = tmp;
	}
	return idx;
}

int CSQC_Client_BufGet (int handle, int idx, char *out, size_t max)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	if (!b || idx < 0 || idx >= b->num || !out || max < 1)
		return 0;
	strlcpy (out, b->str[idx], max);
	return 1;
}

int CSQC_Client_BufSet (int handle, int idx, const char *s)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	char *c;
	if (!b || idx < 0 || idx >= b->num)
		return 0;
	c = Q_strdup (s ? s : "");
	if (!c)
		return 0;
	Q_free (b->str[idx]);
	b->str[idx] = c;
	return 1;
}

int CSQC_Client_BufFree (int handle, int idx)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	int i;
	if (!b || idx < 0 || idx >= b->num)
		return 0;
	Q_free (b->str[idx]);
	for (i = idx; i < b->num - 1; i++)
		b->str[i] = b->str[i + 1];
	b->num--;
	return 1;
}

int CSQC_Client_BufCopy (int from, int to)
{
	csqc_buf_t *f = CSQC_Client_BufAt (from);
	csqc_buf_t *t = CSQC_Client_BufAt (to);
	int i;
	if (!f || !t)
		return 0;
	CSQC_Client_BufClear (t);
	for (i = 0; i < f->num; i++)
		CSQC_Client_BufPush (t, f->str[i]);
	return 1;
}

int CSQC_Client_BufSort (int handle, int prefixlen, int backward)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	int i, j;
	if (!b)
		return 0;
	(void)prefixlen;	// сортировка по всей строке (prefix-семантику не эмулируем)
	for (i = 0; i < b->num; i++)
	{
		for (j = i + 1; j < b->num; j++)
		{
			int cmp = strcmp (b->str[i], b->str[j]);
			if ((!backward && cmp > 0) || (backward && cmp < 0))
			{
				char *t = b->str[i];
				b->str[i] = b->str[j];
				b->str[j] = t;
			}
		}
	}
	return 1;
}

int CSQC_Client_BufImplode (int handle, const char *glue, char *out, size_t max)
{
	csqc_buf_t *b = CSQC_Client_BufAt (handle);
	size_t o = 0;
	int i;
	if (!b || !out || max < 1)
		return 0;
	out[0] = 0;
	for (i = 0; i < b->num && o + 1 < max; i++)
	{
		if (i && glue)
		{
			size_t gl = strlen (glue);
			if (o + gl < max - 1)
			{
				memcpy (out + o, glue, gl);
				o += gl;
				out[o] = 0;
			}
		}
		{
			size_t l = strlen (b->str[i]);
			if (o + l >= max)
				l = max - 1 - o;
			memcpy (out + o, b->str[i], l);
			o += l;
			out[o] = 0;
		}
	}
	return 1;
}

void CSQC_Client_BufReset (void)
{
	int i;
	for (i = 0; i < CSQC_MAX_BUFS; i++)
	{
		if (s_bufs[i].inuse)
		{
			CSQC_Client_BufClear (&s_bufs[i]);
			s_bufs[i].inuse = false;
		}
	}
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
	// P1/D2: арена edicts до memset (указатели ещё на месте).
	CSQC_Client_FreeArena ();
	// Сброс курсора модуля (#343 A3.1): при новом коннекте состояние чистое.
	s_cursormode.usecursor = false;
	s_cursormode.cursorimage[0] = 0;
	s_cursormode.scale = 0;
	// C1.1 #346: чувствительность в дефолт.
	s_sens_scale = 1;
	// C2.2: string-buffers очистить (deep-copy строки).
	CSQC_Client_BufReset ();
	memset (&s_csqc, 0, sizeof (s_csqc));
	memset (s_csqc_stat, 0, sizeof (s_csqc_stat));
	s_csqc.func_init = s_csqc.func_world = s_csqc.func_update =
		s_csqc.func_console = s_csqc.func_shutdown = -1;
	s_csqc.func_entupdate = s_csqc.func_entremove = s_csqc.func_parseevent = -1;
	s_csqc.func_input = -1;
	s_csqc.func_inputevent = -1;
	s_csqc.global_time = -1;
	s_csqc.global_self = -1;
	s_csqc.field_entnum = -1;
	s_csqc.f_origin = s_csqc.f_velocity = s_csqc.f_mins = s_csqc.f_maxs = -1;
	s_csqc.f_movetype = s_csqc.f_flags = s_csqc.f_gravity = s_csqc.f_pmove_flags = -1;
	s_csqc.g_localentnum = -1;
	s_csqc.in_timelength = s_csqc.in_angles = s_csqc.in_movevalues = -1;
	s_csqc.in_buttons = s_csqc.in_impulse = -1;
	s_csqc.in_sequence = -1;
	s_csqc.g_ccframe = s_csqc.g_scframe = -1;
	s_csqc.p_org = s_csqc.p_vel = s_csqc.p_onground = -1;
	s_csqc.g_vfwd = s_csqc.g_vright = s_csqc.g_vup = -1;
	s_last_seq = 0;
}

#endif // !CLIENTONLY
