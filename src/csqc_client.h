/*
csqc_client.h -- клиентская обвязка PR1VM (наш csprogs.dat), Фаза 5 (мини-каркас).

Точки вызова из клиентского кода (cl_parse.c / cl_screen.c / cl_main.c)
и accessor'ы, которыми пользуются builtins из csqc_builtins.c. Заголовок
намеренно без зависимостей (только примитивы), чтобы его можно было
подключать и из server-, и из client-TU.
*/

#ifndef CSQC_CLIENT_H
#define CSQC_CLIENT_H

// Доступ к клиентскому состоянию/выводу (реализация в csqc_client.c):
float CSQC_Client_GetStat (int idx);				// 0..31 -> cl.stats, 32..127 -> ext-статы
void CSQC_Client_SetStat (int idx, int value);		// приём ext-статов 32..127 (CL_SetStat)
void CSQC_Client_GetScreenSize (int *w, int *h);	// vid.width/height (VF_SCREENVSIZE)
void CSQC_Client_DrawText (float x, float y, const char *text, int r, int g, int b, float alpha);
void CSQC_Client_RegisterCommand (const char *cmd);	// привязка registercommand -> консоль

// Точки вызова клиентского жизненного цикла CSQC-VM:
int CSQC_Client_Active (void);			// модуль загружен и не в ошибке
void CSQC_Client_ConnectCheck (void);	// после полного serverinfo: load + CSQC_Init
void CSQC_Client_Disconnect (void);		// CSQC_Shutdown + выгрузка + снятие команд
void CSQC_Client_Update (void);			// каждый 2D-кадр: WorldLoaded-once + UpdateView

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

// Парсинг svc_fte_csqcentities(76)/sized(92) (S1/E3): sized = перед payload
// каждой сущности идёт short-длина (skip-защита от рассинхрона).
void CSQC_Client_ParseEntities (qbool sized);
// Парсинг svc_fte_cgamepacket(83) (E1): модуль сам читает имя + payload.
void CSQC_Client_ParseEvent (void);
// Временный Remove: entnum передаётся builtin-стримом (не edict/self.entnum).
void CSQC_Client_SetRemoveEnt (int entnum);
int CSQC_Client_ReadEntityNum (void);	// remove-pending ? entnum (и потребляет) : -1

#endif /* CSQC_CLIENT_H */
