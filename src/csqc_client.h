/*
csqc_client.h -- клиентская обвязка PR1VM (наш csprogs.dat), Фаза 5 (мини-каркас).

Точки вызова из клиентского кода (cl_parse.c / cl_screen.c / cl_main.c)
и accessor'ы, которыми пользуются builtins из csqc_builtins.c. Заголовок
намеренно без зависимостей (только примитивы), чтобы его можно было
подключать и из server-, и из client-TU.
*/

#ifndef CSQC_CLIENT_H
#define CSQC_CLIENT_H

#include <stddef.h>	// size_t (buf API)
struct usercmd_s;	// ezquake usercmd_t (common.h -> protocol.h); без зависимостей в шапке
struct pr1vm_s;		// PR1 инстанс (pr1vm.h); здесь — только opaque-указатель

// Доступ к клиентскому состоянию/выводу (реализация в csqc_client.c):
float CSQC_Client_GetStat (int idx);				// 0..31 -> cl.stats, 32..127 -> ext-статы
void CSQC_Client_SetStat (int idx, int value);		// приём ext-статов 32..127 (CL_SetStat)
void CSQC_Client_GetScreenSize (int *w, int *h);	// vid.width/height (VF_SCREENVSIZE)
void CSQC_Client_DrawText (float x, float y, const char *text, int r, int g, int b, float alpha, float scale);
void CSQC_Client_RegisterCommand (const char *cmd);	// привязка registercommand -> консоль
void CSQC_Client_Abort (const char *msg);			// фатально: дисконнект клиента (паритет FTE CSQC_Abort)

// FTE-пул (слот ≠ серверный номер). entnum-функции работают со слотами пула;
// сетевые номера держатся картой номер→слот (svc 76/92). slot 0 = world.
int CSQC_Client_EntAlloc (struct pr1vm_s *vm);			// первый свободный слот пула (свой) / 0
void CSQC_Client_EntFree (struct pr1vm_s *vm, int slot);	// освободить свою сущность (сеть не трогаем)
int CSQC_Client_NetAllocSlot (void);					// слот без s_own (сетевой приём)
void CSQC_Client_NetFreeSlot (int slot, int number);		// освободить слот + numslot
int CSQC_Client_NumToSlot (int number);					// карта номер→слот / 0
int CSQC_Client_MapNumber (int number, int slot);		// запись карты (возврат slot)
// P1d C1 — обход/диагностика пула и полей модуля.
qbool CSQC_Client_EntUsed (int slot);			// слот занят (сеть или spawn)
int CSQC_Client_EntSpawnBase (void);			// первый используемый слот (1)
int CSQC_Client_EntUsedCount (void);			// число занятых слотов пула
int CSQC_Client_FindField (struct pr1vm_s *vm, const char *name);	// offset поля в float-словах / -1
// Окружение builtins «как в FTE»: публикация player_localentnum каждый 2D-кадр
// до CSQC_UpdateView. Сущности игроков НЕ фабрикуются (см. csqc_client.c).
void CSQC_Client_UpdateLocalEntnum (void);

// Точки вызова клиентского жизненного цикла CSQC-VM:
int CSQC_Client_Active (void);			// модуль загружен и не в ошибке
void CSQC_Client_ConnectCheck (void);	// после полного serverinfo: load + CSQC_Init
void CSQC_Client_Disconnect (void);		// CSQC_Shutdown + выгрузка + снятие команд
void CSQC_Client_Update (void);			// каждый 2D-кадр: WorldLoaded-once + UpdateView

// CSQC_Input_Frame: перед отправкой каждого usercmd (CL_SendCmd, cl_input.c).
// Движок заполняет input_* глобалы из cmd, исполняет модуль и пишет обратно
// изменения (см. csqc_client.c; механика FTE pr_csqc.c:9418).
void CSQC_Client_InputFrame (struct usercmd_s *cmd);

// C1.3 #345: локальная история отправленных usercmd (QW без ack движения).
void CSQC_Client_RecordInput (struct usercmd_s *cmd);	// запись из CL_SendCmd
int CSQC_Client_ApplyInput (unsigned int seq);			// заполнить input_* по seq; 0/1
void CSQC_Client_RunPlayerPhysics (int entnum);			// #347 runstandardplayerphysics (C1.4)
// #1 makevectors (C6.1): по vector-углам пишет v_forward/v_right/v_up модуля.
void CSQC_Client_MakeVectors (float *ang);

// C2.2 #460-469 — string-buffers (DP). handle = 1-based; строки deep-copy.
int CSQC_Client_BufCreate (void);
void CSQC_Client_BufDel (int handle);
int CSQC_Client_BufGetSize (int handle);
int CSQC_Client_BufAdd (int handle, const char *s, int order);
int CSQC_Client_BufGet (int handle, int idx, char *out, size_t max);
int CSQC_Client_BufSet (int handle, int idx, const char *s);
int CSQC_Client_BufFree (int handle, int idx);
int CSQC_Client_BufCopy (int from, int to);
int CSQC_Client_BufSort (int handle, int prefixlen, int backward);
int CSQC_Client_BufImplode (int handle, const char *glue, char *out, size_t max);
void CSQC_Client_BufReset (void);

// Слой D, шаг 1 — 2D-графика (draw.h/r_draw*; координаты — сырые пиксели видео,
// как DrawText). Помощники для csqc_builtins.c (см. docs/ezquake_csqc_client_layerd_2d_plan.md).
void CSQC_Client_DrawFill (float x, float y, float w, float h, int r, int g, int b, float alpha);
void CSQC_Client_DrawPic (float x, float y, float w, float h, const char *name, int r, int g, int b, float alpha);
void CSQC_Client_DrawSubPic (float x, float y, float w, float h, const char *name, float srcx, float srcy, float srcw, float srch, int r, int g, int b, float alpha);
void CSQC_Client_DrawCharacter (float x, float y, int ch, int r, int g, int b, float alpha, float scale);
void CSQC_Client_DrawLine (float x1, float y1, float x2, float y2, float width, int r, int g, int b, float alpha);
float CSQC_Client_StringWidth (const char *text, qbool usecolours, float fontsize_x);
qbool CSQC_Client_PrecachePic (const char *name);

// Слой D, шаг 3 — #343 setcursormode (полная реализация, A3.1). Парсинг ABI в
// csqc_builtins.c; здесь состояние курсора модуля и его отрисовка. Пока активен
// CSQC-курсор, mouse-механика ezquake учитывает CSQC_Client_CSQCCursor() (vid_sdl2.c),
// а SCR_DrawCursor рисует курсор модуля (image/hotspot/scale). Клики/InputEvent в
// модуль — C1; здесь — release/grab мыши + собственный курсор + позиция (A3.2 #344).
void CSQC_Client_SetCursorMode (qbool usecursor, const char *image,
	float hotspot_x, float hotspot_y, float scale);
qbool CSQC_Client_CSQCCursor (void);		// usecursor=1 && модуль загружен && в игре
void CSQC_Client_DrawCursor (void);			// отрисовка курсора модуля (SCR_DrawCursor)
void CSQC_Client_GetCursorPos (float *x, float *y);	// позиция указателя (#344, A3.2)
void CSQC_Client_SetSensitivityScale (float scale);	// #346 setsensitivityscaler (C1.1)
float CSQC_Client_SensitivityScale (void);	// множитель чувствительности (неактивен → 1)

// C1.2 — события ввода модулю (CSQC_InputEvent): клавиши/мышь/колесо.
qbool CSQC_Client_HasInputEvent (void);		// модуль определил CSQC_InputEvent
int CSQC_Client_InputEvent (int evtype, float a, float b, float c);	// возврат handled

// Типы событий (паритет csdefs.qc IE_*, FTE CSIE_*).
#ifndef IE_KEYDOWN
#define IE_KEYDOWN	0
#define IE_KEYUP	1
#define IE_MOUSEDELTA	2
#define IE_MOUSEABS	3
#define IE_ACCELEROMETER 4
#define IE_FOCUS		5
#define IE_JOYAXIS		6
#endif

// Wire-номер клиентского sendevent (client -> server; в qwprot его нет —
// как в mvdsv server.h: локально, #ifndef-защищено). Пишется первым байтом
// клиентского сообщения (см. csqc_builtins.c csqc_sendevent).
#ifndef clcfte_qcrequest
#define clcfte_qcrequest	81	// CSQC sendevent (client -> server)
#endif

// Размер-варианты CSQC-сообщений (только от mvdsv под sv_csqcdebug; в qwprot
// нет — как в mvdsv server.h, локально): 92 = 76 + short-длина payload на
// каждую сущность, 90 = 83 + short-длина payload в начале.
#ifndef svc_fte_csqcentities_sized
#define svc_fte_csqcentities_sized	92
#endif
#ifndef svc_fte_cgamepacket_sized
#define svc_fte_cgamepacket_sized	90
#endif

// Парсинг svc_fte_csqcentities(76) (S1; sized-92 — E3).
void CSQC_Client_ParseEntities (qbool sized);
// Парсинг svc_fte_cgamepacket(83) (E1): модуль сам читает имя + payload.
void CSQC_Client_ParseEvent (void);

#endif /* CSQC_CLIENT_H */
