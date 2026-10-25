/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef __PR_COMP_H__
#define __PR_COMP_H__

// this file is shared by quake and qcc

typedef int func_t;
typedef int string_t;
typedef intptr_t stringptr_t;

typedef enum {ev_void, ev_string, ev_float, ev_vector, ev_entity, ev_field, ev_function, ev_pointer} etype_t;


#define	OFS_NULL		0
#define	OFS_RETURN		1
#define	OFS_PARM0		4		// leave 3 ofs for each parm to hold vectors
#define	OFS_PARM1		7
#define	OFS_PARM2		10
#define	OFS_PARM3		13
#define	OFS_PARM4		16
#define	OFS_PARM5		19
#define	OFS_PARM6		22
#define	OFS_PARM7		25
#define	RESERVED_OFS	28


enum {
	OP_DONE,
	OP_MUL_F,
	OP_MUL_V,
	OP_MUL_FV,
	OP_MUL_VF,
	OP_DIV_F,
	OP_ADD_F,
	OP_ADD_V,
	OP_SUB_F,
	OP_SUB_V,
	
	OP_EQ_F,
	OP_EQ_V,
	OP_EQ_S,
	OP_EQ_E,
	OP_EQ_FNC,
	
	OP_NE_F,
	OP_NE_V,
	OP_NE_S,
	OP_NE_E,
	OP_NE_FNC,
	
	OP_LE,
	OP_GE,
	OP_LT,
	OP_GT,

	OP_LOAD_F,
	OP_LOAD_V,
	OP_LOAD_S,
	OP_LOAD_ENT,
	OP_LOAD_FLD,
	OP_LOAD_FNC,

	OP_ADDRESS,

	OP_STORE_F,
	OP_STORE_V,
	OP_STORE_S,
	OP_STORE_ENT,
	OP_STORE_FLD,
	OP_STORE_FNC,

	OP_STOREP_F,
	OP_STOREP_V,
	OP_STOREP_S,
	OP_STOREP_ENT,
	OP_STOREP_FLD,
	OP_STOREP_FNC,

	OP_RETURN,
	OP_NOT_F,
	OP_NOT_V,
	OP_NOT_S,
	OP_NOT_ENT,
	OP_NOT_FNC,
	OP_IF,
	OP_IFNOT,
	OP_CALL0,
	OP_CALL1,
	OP_CALL2,
	OP_CALL3,
	OP_CALL4,
	OP_CALL5,
	OP_CALL6,
	OP_CALL7,
	OP_CALL8,
	OP_STATE,
	OP_GOTO,
	OP_AND,
	OP_OR,
	
	OP_BITAND,
	OP_BITOR
};

// FTE extended opcodes (fteqw qclib/pr_comp.h numbering) accepted from client
// csprogs v7 only: the integer, mixed int/float, array and call-with-args ops
// fteqcc emits in "fte format" for CSQC modules.
enum {
	OP_MULSTOREP_F		= 68,
	OP_ADDSTOREP_F		= 74,
	OP_ADDSTOREP_V		= 75,
	OP_SUBSTOREP_F		= 78,
	OP_BITSETSTOREP_F	= 89,
	OP_SWITCH_F			= 98,
	OP_CASE				= 103,
	OP_CASERANGE		= 104,
	OP_CALL1H			= 105,
	OP_CALL2H, OP_CALL3H, OP_CALL4H, OP_CALL5H, OP_CALL6H, OP_CALL7H,
	OP_CALL8H,			// 112
	OP_STORE_I			= 113,
	OP_ADD_I			= 116,
	OP_ADD_IF			= 118,
	OP_SUB_I			= 119,
	OP_SUB_IF			= 121,
	OP_CONV_ITOF		= 122,
	OP_CONV_FTOI		= 123,
	OP_STOREP_I			= 127,
	OP_BITAND_I			= 130,
	OP_BITOR_I			= 131,
	OP_MUL_I			= 132,
	OP_EQ_I				= 134,
	OP_NE_I				= 135,
	OP_NOT_I			= 138,
	OP_RSHIFT_I			= 141,
	OP_LSHIFT_I			= 142,
	OP_GLOBALADDRESS	= 143,
	OP_LOADA_F			= 145,
	OP_LOADA_V			= 146,
	OP_LOADA_S			= 147,
	OP_LOADA_ENT		= 148,
	OP_LOADA_FLD		= 149,
	OP_LOADA_FNC		= 150,
	OP_LOADA_I			= 151,
	OP_LOADP_F			= 154,
	OP_LOADP_V			= 155,
	OP_LOADP_S			= 156,
	OP_LOADP_ENT		= 157,
	OP_LOADP_FLD		= 158,
	OP_LOADP_FNC		= 159,
	OP_LOADP_I			= 160,
	OP_LE_I				= 161,
	OP_GE_I				= 162,
	OP_LT_I				= 163,
	OP_GT_I				= 164,
	OP_LE_IF			= 165,
	OP_GE_IF			= 166,
	OP_LT_IF			= 167,
	OP_GT_IF			= 168,
	OP_LE_FI			= 169,
	OP_GE_FI			= 170,
	OP_LT_FI			= 171,
	OP_GT_FI			= 172,
	OP_EQ_IF			= 173,
	OP_EQ_FI			= 174,
	OP_BITAND_IF		= 185,
	OP_BITOR_IF			= 186,
	OP_BITAND_FI		= 187,
	OP_AND_I			= 189,
	OP_OR_I				= 190,
	OP_AND_IF			= 191,
	OP_OR_IF			= 192,
	OP_AND_FI			= 193,
	OP_OR_FI			= 194,
	OP_NE_IF			= 195,
	OP_BOUNDCHECK		= 211,
	OP_IF_F				= 217,
	OP_IFNOT_F			= 218
};


typedef struct statement_s
{
	unsigned short	op;
	short	a,b,c;
} dstatement_t;

typedef struct
{
	unsigned short	type;		// if DEF_SAVEGLOBGAL bit is set
								// the variable needs to be saved in savegames
	unsigned short	ofs;
	int			s_name;
} ddef_t;
#define	DEF_SAVEGLOBAL	(1<<15)

#define	MAX_PARMS	8

typedef struct
{
	int		first_statement;	// negative numbers are builtins
	int		parm_start;
	int		locals;				// total ints of parms + locals
	
	int		profile;		// runtime
	
	int		s_name;
	int		s_file;			// source file defined in
	
	int		numparms;
	byte	parm_size[MAX_PARMS];
} dfunction_t;


#define	PROG_VERSION	6
typedef struct
{
	int		version;
	int		crc;			// check of header file
	
	int		ofs_statements;
	int		numstatements;	// statement 0 is an error

	int		ofs_globaldefs;
	int		numglobaldefs;
	
	int		ofs_fielddefs;
	int		numfielddefs;
	
	int		ofs_functions;
	int		numfunctions;	// function 0 is an empty
	
	int		ofs_strings;
	int		numstrings;		// first string is a null string

	int		ofs_globals;
	int		numglobals;
	
	int		entityfields;
} dprograms_t;

// FTE progs v7 header tail (fteqw qclib/pr_comp.h), right after the v6 header.
// The client accepts only the 16-bit statement, uncompressed variant.
#define PROG_EXTENDEDVERSION	7
#define PROG_SECONDARYVERSION16	((('1'<<0)|('F'<<8)|('T'<<16)|('E'<<24))^(('P'<<0)|('R'<<8)|('O'<<16)|('G'<<24)))
typedef struct
{
	dprograms_t	v6;
	int		ofsfiles;
	int		ofslinenums;
	int		ofsbodylessfuncs;
	int		numbodylessfuncs;
	int		ofs_types;
	int		numtypes;
	int		blockscompressed;
	int		secondaryversion;
} dprograms_v7_t;

#endif /* !__PR_COMP_H__ */
