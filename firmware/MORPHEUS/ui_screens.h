/*
 * ============================================================================
 * MORPHEUS Standalone UI - Screen Drawing Interface
 * ============================================================================
 *
 * File: ui_screens.h
 * Author: Coder Chunk
 * License: GNU General Public License v3.0 (GPLv3)
 *
 * One draw function per screen class. Each reads the menu tree, the mock
 * model, and the navigation engine's public getters; none of them mutates
 * anything. Called exclusively by ui_renderer.cpp.
 *
 * Copyright (C) 2026 Coder Chunk
 *
 * ============================================================================
 */

#ifndef UI_SCREENS_H
#define UI_SCREENS_H
#include <U8g2lib.h>

void ui_screens_drawSplash(U8G2 &u8g2, unsigned long elapsedMs);
void ui_screens_drawHome(U8G2 &u8g2);
void ui_screens_drawMenu(U8G2 &u8g2);
void ui_screens_drawList(U8G2 &u8g2);
void ui_screens_drawEditValue(U8G2 &u8g2);
void ui_screens_drawEditToggle(U8G2 &u8g2);
void ui_screens_drawInfo(U8G2 &u8g2);
void ui_screens_drawDialogConfirm(U8G2 &u8g2);

void ui_screens_drawDiagInput(U8G2 &u8g2);
void ui_screens_drawDiagDisplay(U8G2 &u8g2);
void ui_screens_drawDiagAudio(U8G2 &u8g2);
void ui_screens_drawDiagGpio(U8G2 &u8g2);
void ui_screens_drawDiagLive(U8G2 &u8g2);

void ui_screens_drawLiveMonitor(U8G2 &u8g2);
void ui_screens_drawTune(U8G2 &u8g2);
void ui_screens_drawVolume(U8G2 &u8g2);

void ui_screens_drawTrainDrill(U8G2 &u8g2);
void ui_screens_drawTrainFarnsworth(U8G2 &u8g2);
void ui_screens_drawTrainExamResult(U8G2 &u8g2);

void ui_screens_drawGameCopy(U8G2 &u8g2);
void ui_screens_drawGameMemory(U8G2 &u8g2);
void ui_screens_drawGameSpeed(U8G2 &u8g2);
void ui_screens_drawGamePause(U8G2 &u8g2);

#endif