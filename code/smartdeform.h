/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "coord.h"

class CellClass;


struct DeformPointStruct {

	DeformPointStruct(void)
	{
		Height = 0;
		Rigid = false;
		Done = false;
	};

	DeformPointStruct(const DeformPointStruct & that)
	{
		Height = that.Height;
		Rigid = that.Rigid;
		Done = that.Done;
	};

	int Height;
	bool Rigid;
	bool Done;
	bool CanForce;
};


extern "C" {

extern DeformPointStruct * DeformPoints;
extern int DeformPointXAdd;
extern int DeformPointYAdd;
extern int DeformPointWidth;
extern int DeformPointHeight;

extern bool Asm_Ripple_Deform_Points(int startpointx, int startpointy, int general_direction, bool forced);

}

bool Deform_Cell(Cell cell, int dir, bool forced = true, int mask = 15);
void Init_Deform_Grid(Cell center, bool forced);
void Init_Deform_Grid_RMG(void);
bool Commit_Deform_Grid_RMG(void);
bool Deform_Cell_RMG(Cell originalcell, int general_direction, bool forced, int pointmask);
int Get_Deformed_Cell_Height(CellClass *cellptr);
int Get_Deformable_Cell_Corners(CellClass *cellptr, int direction);
