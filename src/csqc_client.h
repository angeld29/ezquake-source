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
void CSQC_Client_DrawText (float x, float y, const char *text, float alpha);
void CSQC_Client_RegisterCommand (const char *cmd);	// привязка registercommand -> консоль

// Точки вызова клиентского жизненного цикла CSQC-VM:
int CSQC_Client_Active (void);			// модуль загружен и не в ошибке
void CSQC_Client_ConnectCheck (void);	// после полного serverinfo: load + CSQC_Init
void CSQC_Client_Disconnect (void);		// CSQC_Shutdown + выгрузка + снятие команд
void CSQC_Client_Update (void);			// каждый 2D-кадр: WorldLoaded-once + UpdateView

#endif /* CSQC_CLIENT_H */
