/*
 * Copyright 2012 Vadim Girlin <vadimgirlin@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *      Vadim Girlin
 */

#ifndef R600_ISA_H_
#define R600_ISA_H_

#include "util/u_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ALU flags */
enum alu_op_flags
{
	AF_NONE = 0,
	AF_V		= (1<<0),    /* allowed in vector slots */

	/* allowed in scalar(trans) slot (slots xyz on cayman, may be replicated
	 * to w) */
	AF_S		= (1<<1),

	AF_4SLOT	= (1<<2),            /* uses four vector slots (e.g. DOT4) */
	AF_4V		= (AF_V | AF_4SLOT),
	AF_VS		= (AF_V | AF_S),     /* allowed in any slot */

	AF_2SLOT = (1 << 3),
	AF_2V    = AF_V | AF_2SLOT, /* XY or ZW */

	AF_KILL		= (1<<4),
	AF_PRED		= (1<<5),
	AF_SET		= (1<<6),

	/* e.g. MUL_PREV instructions, allowed in x/y, depends on z/w */
	AF_PREV_INTERLEAVE	= (1<<7),

	AF_MOVA		= (1<<8),    /* all MOVA instructions */

	AF_IEEE		= (1<<10),

	AF_DST_TYPE_MASK = (3<<11),
	AF_FLOAT_DST	= 0,
	AF_INT_DST		= (1<<11),
	AF_UINT_DST		= (3<<11),

	/* DP instructions, 2-slot pairs */
	AF_64		= (1<<13),
	/* 24 bit instructions */
	AF_24		= (1<<14),
	/* DX10 variants */
	AF_DX10		= (1<<15),

	/* result is replicated to all channels (only if AF_4V is also set -
	 * for special handling of MULLO_INT on CM) */
	AF_REPL		= (1<<16),

	/* interpolation instructions */
	AF_INTERP	= (1<<17),

	/* LDS instructions */
	AF_LDS		= (1<<20),

	/* e.g. DOT - depends on the next slot in the same group (x<=y/y<=z/z<=w) */
	AF_PREV_NEXT	= (1<<21),

	/* int<->flt conversions */
	AF_CVT = (1<<22),

	/* commutative operation on src0 and src1 ( a op b = b op a),
	 * includes MULADDs (considering the MUL part on src0 and src1 only) */
	AF_M_COMM = (1 << 23),

	/* associative operation ((a op b) op c) == (a op (b op c)),
	 * includes MULADDs (considering the MUL part on src0 and src1 only) */
	AF_M_ASSOC = (1 << 24),

	AF_PRED_PUSH = (1 << 25),

	AF_ANY_PRED = (AF_PRED | AF_PRED_PUSH),

	AF_CMOV = (1 << 26),

	// for SETcc, PREDSETcc, ... - type of comparison
	AF_CMP_TYPE_MASK = (3 << 27),
	AF_FLOAT_CMP = 0,
	AF_INT_CMP = (1 << 27),
	AF_UINT_CMP = (3 << 27),

	/* condition codes - 3 bits */
	AF_CC_SHIFT = 29,

	AF_CC_MASK	= (7U << AF_CC_SHIFT),
	AF_CC_E		= (0U << AF_CC_SHIFT),
	AF_CC_GT	= (1U << AF_CC_SHIFT),
	AF_CC_GE	= (2U << AF_CC_SHIFT),
	AF_CC_NE	= (3U << AF_CC_SHIFT),
	AF_CC_LT	= (4U << AF_CC_SHIFT),
	AF_CC_LE	= (5U << AF_CC_SHIFT),
};

/* flags for FETCH instructions (TEX/VTX/GDS) */
enum fetch_op_flags
{
	FF_GDS		= (1<<0),
	FF_TEX		= (1<<1),

	FF_SETGRAD	= (1<<2),
	FF_GETGRAD	= (1<<3),
	FF_USEGRAD	= (1<<4),

	FF_VTX		= (1<<5),
	FF_MEM		= (1<<6),

	FF_SET_TEXTURE_OFFSETS = (1<<7),
	FF_USE_TEXTURE_OFFSETS = (1<<8),
};

/* flags for CF instructions */
enum cf_op_flags
{
	CF_CLAUSE	= (1<<0), 	/* execute clause (alu/fetch ...) */
	CF_ACK		= (1<<1),	/* acked versions of some instructions */
	CF_ALU		= (1<<2),	/* alu clause execution */
	CF_ALU_EXT	= (1<<3),	/* ALU_EXTENDED */
	CF_EXP		= (1<<4),	/* export (CF_ALLOC_EXPORT_WORD1_SWIZ) */
	CF_BRANCH	= (1<<5),   /* branch instructions */
	CF_LOOP		= (1<<6),   /* loop instructions */
	CF_CALL		= (1<<7),   /* call instructions */
	CF_MEM		= (1<<8),	/* export_mem (CF_ALLOC_EXPORT_WORD1_BUF) */
	CF_FETCH	= (1<<9),	/* fetch clause */

	CF_UNCOND	= (1<<10),	/* COND = ACTIVE required */
	CF_EMIT		= (1<<11),
	CF_STRM		= (1<<12),	/* MEM_STREAM* */

	CF_RAT		= (1<<13),  /* MEM_RAT* */

	CF_LOOP_START = (1<<14)
};

/* ALU instruction info */
struct alu_op_info
{
	/* instruction name */
	const char	*name;
	/* number of source operands */
	int	src_count;
	/* opcodes, [0] - for r6xx/r7xx, [1] - for evergreen/cayman
	 * (-1) if instruction doesn't exist (more precise info in "slots") */
	int opcode[2];
	/* slots for r6xx, r7xx, evergreen, cayman
	 * (0 if instruction doesn't exist for chip class) */
	int	slots[4];
	/* flags (mostly autogenerated from instruction name) */
	unsigned int flags;
};

/* FETCH instruction info */
struct fetch_op_info
{
	const char * name;
	/* for every chip class */
	int opcode[4];
	int flags;
};

/* CF instruction info */
struct cf_op_info
{
	const char * name;
	/* for every chip class */
	int opcode[4];
	int flags;
};


#define ALU_OP2_ADD                             0
#define ALU_OP2_MUL                             1
#define ALU_OP2_MUL_IEEE                        2
#define ALU_OP2_MAX                             3
#define ALU_OP2_MIN                             4
#define ALU_OP2_MAX_DX10                        5
#define ALU_OP2_MIN_DX10                        6
#define ALU_OP2_SETE                            7
#define ALU_OP2_SETGT                           8
#define ALU_OP2_SETGE                           9
#define ALU_OP2_SETNE                          10
#define ALU_OP2_SETE_DX10                      11
#define ALU_OP2_SETGT_DX10                     12
#define ALU_OP2_SETGE_DX10                     13
#define ALU_OP2_SETNE_DX10                     14
#define ALU_OP1_FRACT                          15
#define ALU_OP1_TRUNC                          16
#define ALU_OP1_CEIL                           17
#define ALU_OP1_RNDNE                          18
#define ALU_OP1_FLOOR                          19
#define ALU_OP2_ASHR_INT                       20
#define ALU_OP2_LSHR_INT                       21
#define ALU_OP2_LSHL_INT                       22
#define ALU_OP1_MOV                            23
#define ALU_OP0_NOP                            24
#define ALU_OP2_PRED_SETGT_UINT                25
#define ALU_OP2_PRED_SETGE_UINT                26
#define ALU_OP2_PRED_SETE                      27
#define ALU_OP2_PRED_SETGT                     28
#define ALU_OP2_PRED_SETGE                     29
#define ALU_OP2_PRED_SETNE                     30
#define ALU_OP1_PRED_SET_INV                   31
#define ALU_OP2_PRED_SET_POP                   32
#define ALU_OP0_PRED_SET_CLR                   33
#define ALU_OP1_PRED_SET_RESTORE               34
#define ALU_OP2_PRED_SETE_PUSH                 35
#define ALU_OP2_PRED_SETGT_PUSH                36
#define ALU_OP2_PRED_SETGE_PUSH                37
#define ALU_OP2_PRED_SETNE_PUSH                38
#define ALU_OP2_KILLE                          39
#define ALU_OP2_KILLGT                         40
#define ALU_OP2_KILLGE                         41
#define ALU_OP2_KILLNE                         42
#define ALU_OP2_AND_INT                        43
#define ALU_OP2_OR_INT                         44
#define ALU_OP2_XOR_INT                        45
#define ALU_OP1_NOT_INT                        46
#define ALU_OP2_ADD_INT                        47
#define ALU_OP2_SUB_INT                        48
#define ALU_OP2_MAX_INT                        49
#define ALU_OP2_MIN_INT                        50
#define ALU_OP2_MAX_UINT                       51
#define ALU_OP2_MIN_UINT                       52
#define ALU_OP2_SETE_INT                       53
#define ALU_OP2_SETGT_INT                      54
#define ALU_OP2_SETGE_INT                      55
#define ALU_OP2_SETNE_INT                      56
#define ALU_OP2_SETGT_UINT                     57
#define ALU_OP2_SETGE_UINT                     58
#define ALU_OP2_KILLGT_UINT                    59
#define ALU_OP2_KILLGE_UINT                    60
#define ALU_OP2_PRED_SETE_INT                  61
#define ALU_OP2_PRED_SETGT_INT                 62
#define ALU_OP2_PRED_SETGE_INT                 63
#define ALU_OP2_PRED_SETNE_INT                 64
#define ALU_OP2_KILLE_INT                      65
#define ALU_OP2_KILLGT_INT                     66
#define ALU_OP2_KILLGE_INT                     67
#define ALU_OP2_KILLNE_INT                     68
#define ALU_OP2_PRED_SETE_PUSH_INT             69
#define ALU_OP2_PRED_SETGT_PUSH_INT            70
#define ALU_OP2_PRED_SETGE_PUSH_INT            71
#define ALU_OP2_PRED_SETNE_PUSH_INT            72
#define ALU_OP2_PRED_SETLT_PUSH_INT            73
#define ALU_OP2_PRED_SETLE_PUSH_INT            74
#define ALU_OP1_FLT_TO_INT                     75
#define ALU_OP1_BFREV_INT                      76
#define ALU_OP2_ADDC_UINT                      77
#define ALU_OP2_SUBB_UINT                      78
#define ALU_OP0_GROUP_BARRIER                  79
#define ALU_OP0_GROUP_SEQ_BEGIN                80
#define ALU_OP0_GROUP_SEQ_END                  81
#define ALU_OP2_SET_MODE                       82
#define ALU_OP0_SET_CF_IDX0                    83
#define ALU_OP0_SET_CF_IDX1                    84
#define ALU_OP2_SET_LDS_SIZE                   85
#define ALU_OP2_MUL_INT24                      86
#define ALU_OP2_MULHI_INT24                    87
#define ALU_OP1_FLT_TO_INT_TRUNC               88
#define ALU_OP1_EXP_IEEE                       89
#define ALU_OP1_LOG_CLAMPED                    90
#define ALU_OP1_LOG_IEEE                       91
#define ALU_OP1_RECIP_CLAMPED                  92
#define ALU_OP1_RECIP_FF                       93
#define ALU_OP1_RECIP_IEEE                     94
#define ALU_OP1_RECIPSQRT_CLAMPED              95
#define ALU_OP1_RECIPSQRT_FF                   96
#define ALU_OP1_RECIPSQRT_IEEE                 97
#define ALU_OP1_SQRT_IEEE                      98
#define ALU_OP1_SIN                            99
#define ALU_OP1_COS                           100
#define ALU_OP2_MULLO_INT                     101
#define ALU_OP2_MULHI_INT                     102
#define ALU_OP2_MULLO_UINT                    103
#define ALU_OP2_MULHI_UINT                    104
#define ALU_OP1_RECIP_INT                     105
#define ALU_OP1_RECIP_UINT                    106
#define ALU_OP2_RECIP_64                      107
#define ALU_OP2_RECIP_CLAMPED_64              108
#define ALU_OP2_RECIPSQRT_64                  109
#define ALU_OP2_RECIPSQRT_CLAMPED_64          110
#define ALU_OP2_SQRT_64                       111
#define ALU_OP1_FLT_TO_UINT                   112
#define ALU_OP1_INT_TO_FLT                    113
#define ALU_OP1_UINT_TO_FLT                   114
#define ALU_OP2_BFM_INT                       115
#define ALU_OP1_FLT32_TO_FLT16                116
#define ALU_OP1_FLT16_TO_FLT32                117
#define ALU_OP1_UBYTE0_FLT                    118
#define ALU_OP1_UBYTE1_FLT                    119
#define ALU_OP1_UBYTE2_FLT                    120
#define ALU_OP1_UBYTE3_FLT                    121
#define ALU_OP1_BCNT_INT                      122
#define ALU_OP1_FFBH_UINT                     123
#define ALU_OP1_FFBL_INT                      124
#define ALU_OP1_FFBH_INT                      125
#define ALU_OP1_FLT_TO_UINT4                  126
#define ALU_OP2_DOT_IEEE                      127
#define ALU_OP1_FLT_TO_INT_RPI                128
#define ALU_OP1_FLT_TO_INT_FLOOR              129
#define ALU_OP2_MULHI_UINT24                  130
#define ALU_OP1_MBCNT_32HI_INT                131
#define ALU_OP1_OFFSET_TO_FLT                 132
#define ALU_OP2_MUL_UINT24                    133
#define ALU_OP1_BCNT_ACCUM_PREV_INT           134
#define ALU_OP1_MBCNT_32LO_ACCUM_PREV_INT     135
#define ALU_OP2_SETE_64                       136
#define ALU_OP2_SETNE_64                      137
#define ALU_OP2_SETGT_64                      138
#define ALU_OP2_SETGE_64                      139
#define ALU_OP2_MIN_64                        140
#define ALU_OP2_MAX_64                        141
#define ALU_OP2_DOT4                          142
#define ALU_OP2_DOT4_IEEE                     143
#define ALU_OP2_CUBE                          144
#define ALU_OP1_MAX4                          145
#define ALU_OP1_FREXP_64                      146
#define ALU_OP2_LDEXP_64                      147
#define ALU_OP1_FRACT_64                      148
#define ALU_OP2_PRED_SETGT_64                 149
#define ALU_OP2_PRED_SETE_64                  150
#define ALU_OP2_PRED_SETGE_64                 151
#define ALU_OP2_MUL_64                        152
#define ALU_OP2_ADD_64                        153
#define ALU_OP1_MOVA_INT                      154
#define ALU_OP1_FLT64_TO_FLT32                155
#define ALU_OP1_FLT32_TO_FLT64                156
#define ALU_OP2_SAD_ACCUM_PREV_UINT           157
#define ALU_OP2_DOT                           158
#define ALU_OP1_MUL_PREV                      159
#define ALU_OP1_MUL_IEEE_PREV                 160
#define ALU_OP1_ADD_PREV                      161
#define ALU_OP2_MULADD_PREV                   162
#define ALU_OP2_MULADD_IEEE_PREV              163
#define ALU_OP2_INTERP_XY                     164
#define ALU_OP2_INTERP_ZW                     165
#define ALU_OP2_INTERP_X                      166
#define ALU_OP2_INTERP_Z                      167
#define ALU_OP1_STORE_FLAGS                   168
#define ALU_OP1_LOAD_STORE_FLAGS              169
#define ALU_OP2_LDS_1A                        170
#define ALU_OP2_LDS_1A1D                      171
#define ALU_OP2_LDS_2A                        172
#define ALU_OP1_INTERP_LOAD_P0                173
#define ALU_OP1_INTERP_LOAD_P10               174
#define ALU_OP1_INTERP_LOAD_P20               175
#define ALU_OP3_BFE_UINT                      176
#define ALU_OP3_BFE_INT                       177
#define ALU_OP3_BFI_INT                       178
#define ALU_OP3_FMA                           179
#define ALU_OP3_MULADD_INT24                  180
#define ALU_OP3_CNDNE_64                      181
#define ALU_OP3_FMA_64                        182
#define ALU_OP3_LERP_UINT                     183
#define ALU_OP3_BIT_ALIGN_INT                 184
#define ALU_OP3_BYTE_ALIGN_INT                185
#define ALU_OP3_SAD_ACCUM_UINT                186
#define ALU_OP3_SAD_ACCUM_HI_UINT             187
#define ALU_OP3_MULADD_UINT24                 188
#define ALU_OP3_LDS_IDX_OP                    189
#define ALU_OP3_MULADD                        190
#define ALU_OP3_MULADD_M2                     191
#define ALU_OP3_MULADD_M4                     192
#define ALU_OP3_MULADD_D2                     193
#define ALU_OP3_MULADD_IEEE                   194
#define ALU_OP3_CNDE                          195
#define ALU_OP3_CNDGT                         196
#define ALU_OP3_CNDGE                         197
#define ALU_OP3_CNDE_INT                      198
#define ALU_OP3_CNDGT_INT                     199
#define ALU_OP3_CNDGE_INT                     200
#define ALU_OP3_MUL_LIT                       201
#define ALU_OP1_MOVA                          202
#define ALU_OP1_MOVA_FLOOR                    203
#define ALU_OP1_MOVA_GPR_INT                  204
#define ALU_OP3_MULADD_64                     205
#define ALU_OP3_MULADD_64_M2                  206
#define ALU_OP3_MULADD_64_M4                  207
#define ALU_OP3_MULADD_64_D2                  208
#define ALU_OP3_MUL_LIT_M2                    209
#define ALU_OP3_MUL_LIT_M4                    210
#define ALU_OP3_MUL_LIT_D2                    211
#define ALU_OP3_MULADD_IEEE_M2                212
#define ALU_OP3_MULADD_IEEE_M4                213
#define ALU_OP3_MULADD_IEEE_D2                214

#define LDS_OP2_LDS_ADD                       215
#define LDS_OP2_LDS_SUB                       216
#define LDS_OP2_LDS_RSUB                      217
#define LDS_OP2_LDS_INC                       218
#define LDS_OP2_LDS_DEC                       219
#define LDS_OP2_LDS_MIN_INT                   220
#define LDS_OP2_LDS_MAX_INT                   221
#define LDS_OP2_LDS_MIN_UINT                  222
#define LDS_OP2_LDS_MAX_UINT                  223
#define LDS_OP2_LDS_AND                       224
#define LDS_OP2_LDS_OR                        225
#define LDS_OP2_LDS_XOR                       226
#define LDS_OP3_LDS_MSKOR                     227
#define LDS_OP2_LDS_WRITE                     228
#define LDS_OP3_LDS_WRITE_REL                 229
#define LDS_OP3_LDS_WRITE2                    230
#define LDS_OP3_LDS_CMP_STORE                 231
#define LDS_OP3_LDS_CMP_STORE_SPF             232
#define LDS_OP2_LDS_BYTE_WRITE                233
#define LDS_OP2_LDS_SHORT_WRITE               234
#define LDS_OP2_LDS_ADD_RET                   235
#define LDS_OP2_LDS_SUB_RET                   236
#define LDS_OP2_LDS_RSUB_RET                  237
#define LDS_OP2_LDS_INC_RET                   238
#define LDS_OP2_LDS_DEC_RET                   239
#define LDS_OP2_LDS_MIN_INT_RET               240
#define LDS_OP2_LDS_MAX_INT_RET               241
#define LDS_OP2_LDS_MIN_UINT_RET              242
#define LDS_OP2_LDS_MAX_UINT_RET              243
#define LDS_OP2_LDS_AND_RET                   244
#define LDS_OP2_LDS_OR_RET                    245
#define LDS_OP2_LDS_XOR_RET                   246
#define LDS_OP3_LDS_MSKOR_RET                 247
#define LDS_OP2_LDS_XCHG_RET                  248
#define LDS_OP3_LDS_XCHG_REL_RET              249
#define LDS_OP3_LDS_XCHG2_RET                 250
#define LDS_OP3_LDS_CMP_XCHG_RET              251
#define LDS_OP3_LDS_CMP_XCHG_SPF_RET          252
#define LDS_OP1_LDS_READ_RET                  253
#define LDS_OP1_LDS_READ_REL_RET              254
#define LDS_OP2_LDS_READ2_RET                 255
#define LDS_OP3_LDS_READWRITE_RET             256
#define LDS_OP1_LDS_BYTE_READ_RET             257
#define LDS_OP1_LDS_UBYTE_READ_RET            258
#define LDS_OP1_LDS_SHORT_READ_RET            259
#define LDS_OP1_LDS_USHORT_READ_RET           260

#define FETCH_OP_VFETCH                           0
#define FETCH_OP_SEMFETCH                         1
#define FETCH_OP_READ_SCRATCH                     2
#define FETCH_OP_READ_REDUCT                      3
#define FETCH_OP_READ_MEM                         4
#define FETCH_OP_DS_LOCAL_WRITE                   5
#define FETCH_OP_DS_LOCAL_READ                    6
#define FETCH_OP_GDS_ADD                          7
#define FETCH_OP_GDS_SUB                          8
#define FETCH_OP_GDS_RSUB                         9
#define FETCH_OP_GDS_INC                         10
#define FETCH_OP_GDS_DEC                         11
#define FETCH_OP_GDS_MIN_INT                     12
#define FETCH_OP_GDS_MAX_INT                     13
#define FETCH_OP_GDS_MIN_UINT                    14
#define FETCH_OP_GDS_MAX_UINT                    15
#define FETCH_OP_GDS_AND                         16
#define FETCH_OP_GDS_OR                          17
#define FETCH_OP_GDS_XOR                         18
#define FETCH_OP_GDS_MSKOR                       19
#define FETCH_OP_GDS_WRITE                       20
#define FETCH_OP_GDS_WRITE_REL                   21
#define FETCH_OP_GDS_WRITE2                      22
#define FETCH_OP_GDS_CMP_STORE                   23
#define FETCH_OP_GDS_CMP_STORE_SPF               24
#define FETCH_OP_GDS_BYTE_WRITE                  25
#define FETCH_OP_GDS_SHORT_WRITE                 26
#define FETCH_OP_GDS_ADD_RET                     27
#define FETCH_OP_GDS_SUB_RET                     28
#define FETCH_OP_GDS_RSUB_RET                    29
#define FETCH_OP_GDS_INC_RET                     30
#define FETCH_OP_GDS_DEC_RET                     31
#define FETCH_OP_GDS_MIN_INT_RET                 32
#define FETCH_OP_GDS_MAX_INT_RET                 33
#define FETCH_OP_GDS_MIN_UINT_RET                34
#define FETCH_OP_GDS_MAX_UINT_RET                35
#define FETCH_OP_GDS_AND_RET                     36
#define FETCH_OP_GDS_OR_RET                      37
#define FETCH_OP_GDS_XOR_RET                     38
#define FETCH_OP_GDS_MSKOR_RET                   39
#define FETCH_OP_GDS_XCHG_RET                    40
#define FETCH_OP_GDS_XCHG_REL_RET                41
#define FETCH_OP_GDS_XCHG2_RET                   42
#define FETCH_OP_GDS_CMP_XCHG_RET                43
#define FETCH_OP_GDS_CMP_XCHG_SPF_RET            44
#define FETCH_OP_GDS_READ_RET                    45
#define FETCH_OP_GDS_READ_REL_RET                46
#define FETCH_OP_GDS_READ2_RET                   47
#define FETCH_OP_GDS_READWRITE_RET               48
#define FETCH_OP_GDS_BYTE_READ_RET               49
#define FETCH_OP_GDS_UBYTE_READ_RET              50
#define FETCH_OP_GDS_SHORT_READ_RET              51
#define FETCH_OP_GDS_USHORT_READ_RET             52
#define FETCH_OP_GDS_ATOMIC_ORDERED_ALLOC        53
#define FETCH_OP_TF_WRITE                        54
#define FETCH_OP_DS_GLOBAL_WRITE                 55
#define FETCH_OP_DS_GLOBAL_READ                  56
#define FETCH_OP_LD                              57
#define FETCH_OP_LDFPTR                          58
#define FETCH_OP_GET_TEXTURE_RESINFO             59
#define FETCH_OP_GET_NUMBER_OF_SAMPLES           60
#define FETCH_OP_GET_LOD                         61
#define FETCH_OP_GET_GRADIENTS_H                 62
#define FETCH_OP_GET_GRADIENTS_V                 63
#define FETCH_OP_GET_GRADIENTS_H_FINE            64
#define FETCH_OP_GET_GRADIENTS_V_FINE            65
#define FETCH_OP_GET_LERP                        66
#define FETCH_OP_SET_TEXTURE_OFFSETS             67
#define FETCH_OP_KEEP_GRADIENTS                  68
#define FETCH_OP_SET_GRADIENTS_H                 69
#define FETCH_OP_SET_GRADIENTS_V                 70
#define FETCH_OP_SET_GRADIENTS_H_COARSE          71
#define FETCH_OP_SET_GRADIENTS_V_COARSE          72
#define FETCH_OP_SET_GRADIENTS_H_PACKED_FINE     73
#define FETCH_OP_SET_GRADIENTS_V_PACKED_FINE     74
#define FETCH_OP_SET_GRADIENTS_H_PACKED_COARSE   75
#define FETCH_OP_SET_GRADIENTS_V_PACKED_COARSE   76
#define FETCH_OP_PASS                            77
#define FETCH_OP_PASS1                           78
#define FETCH_OP_PASS2                           79
#define FETCH_OP_PASS3                           80
#define FETCH_OP_SET_CUBEMAP_INDEX               81
#define FETCH_OP_GET_BUFFER_RESINFO              82
#define FETCH_OP_FETCH4                          83
#define FETCH_OP_SAMPLE                          84
#define FETCH_OP_SAMPLE_L                        85
#define FETCH_OP_SAMPLE_LB                       86
#define FETCH_OP_SAMPLE_LZ                       87
#define FETCH_OP_SAMPLE_G                        88
#define FETCH_OP_SAMPLE_G_L                      89
#define FETCH_OP_GATHER4                         90
#define FETCH_OP_SAMPLE_G_LB                     91
#define FETCH_OP_SAMPLE_G_LZ                     92
#define FETCH_OP_GATHER4_O                       93
#define FETCH_OP_SAMPLE_C                        94
#define FETCH_OP_SAMPLE_C_L                      95
#define FETCH_OP_SAMPLE_C_LB                     96
#define FETCH_OP_SAMPLE_C_LZ                     97
#define FETCH_OP_SAMPLE_C_G                      98
#define FETCH_OP_SAMPLE_C_G_L                    99
#define FETCH_OP_GATHER4_C                      100
#define FETCH_OP_SAMPLE_C_G_LB                  101
#define FETCH_OP_SAMPLE_C_G_LZ                  102
#define FETCH_OP_GATHER4_C_O                    103

#define CF_OP_NOP                           0
#define CF_OP_TEX                           1
#define CF_OP_VTX                           2
#define CF_OP_VTX_TC                        3
#define CF_OP_GDS                           4
#define CF_OP_LOOP_START                    5
#define CF_OP_LOOP_END                      6
#define CF_OP_LOOP_START_DX10               7
#define CF_OP_LOOP_START_NO_AL              8
#define CF_OP_LOOP_CONTINUE                 9
#define CF_OP_LOOP_BREAK                   10
#define CF_OP_JUMP                         11
#define CF_OP_PUSH                         12
#define CF_OP_PUSH_ELSE                    13
#define CF_OP_ELSE                         14
#define CF_OP_POP                          15
#define CF_OP_POP_JUMP                     16
#define CF_OP_POP_PUSH                     17
#define CF_OP_POP_PUSH_ELSE                18
#define CF_OP_CALL                         19
#define CF_OP_CALL_FS                      20
#define CF_OP_RET                          21
#define CF_OP_EMIT_VERTEX                  22
#define CF_OP_EMIT_CUT_VERTEX              23
#define CF_OP_CUT_VERTEX                   24
#define CF_OP_KILL                         25
#define CF_OP_END_PROGRAM                  26
#define CF_OP_WAIT_ACK                     27
#define CF_OP_TEX_ACK                      28
#define CF_OP_VTX_ACK                      29
#define CF_OP_VTX_TC_ACK                   30
#define CF_OP_JUMPTABLE                    31
#define CF_OP_WAVE_SYNC                    32
#define CF_OP_HALT                         33
#define CF_OP_CF_END                       34
#define CF_OP_LDS_DEALLOC                  35
#define CF_OP_PUSH_WQM                     36
#define CF_OP_POP_WQM                      37
#define CF_OP_ELSE_WQM                     38
#define CF_OP_JUMP_ANY                     39
#define CF_OP_REACTIVATE                   40
#define CF_OP_REACTIVATE_WQM               41
#define CF_OP_INTERRUPT                    42
#define CF_OP_INTERRUPT_AND_SLEEP          43
#define CF_OP_SET_PRIORITY                 44
#define CF_OP_MEM_STREAM0_BUF0             45
#define CF_OP_MEM_STREAM0_BUF1             46
#define CF_OP_MEM_STREAM0_BUF2             47
#define CF_OP_MEM_STREAM0_BUF3             48
#define CF_OP_MEM_STREAM1_BUF0             49
#define CF_OP_MEM_STREAM1_BUF1             50
#define CF_OP_MEM_STREAM1_BUF2             51
#define CF_OP_MEM_STREAM1_BUF3             52
#define CF_OP_MEM_STREAM2_BUF0             53
#define CF_OP_MEM_STREAM2_BUF1             54
#define CF_OP_MEM_STREAM2_BUF2             55
#define CF_OP_MEM_STREAM2_BUF3             56
#define CF_OP_MEM_STREAM3_BUF0             57
#define CF_OP_MEM_STREAM3_BUF1             58
#define CF_OP_MEM_STREAM3_BUF2             59
#define CF_OP_MEM_STREAM3_BUF3             60
#define CF_OP_MEM_STREAM0                  61
#define CF_OP_MEM_STREAM1                  62
#define CF_OP_MEM_STREAM2                  63
#define CF_OP_MEM_STREAM3                  64
#define CF_OP_MEM_SCRATCH                  65
#define CF_OP_MEM_REDUCT                   66
#define CF_OP_MEM_RING                     67
#define CF_OP_EXPORT                       68
#define CF_OP_EXPORT_DONE                  69
#define CF_OP_MEM_EXPORT                   70
#define CF_OP_MEM_RAT                      71
#define CF_OP_MEM_RAT_NOCACHE              72
#define CF_OP_MEM_RING1                    73
#define CF_OP_MEM_RING2                    74
#define CF_OP_MEM_RING3                    75
#define CF_OP_MEM_MEM_COMBINED             76
#define CF_OP_MEM_RAT_COMBINED_NOCACHE     77
#define CF_OP_MEM_RAT_COMBINED             78
#define CF_OP_EXPORT_DONE_END              79
#define CF_OP_ALU                          80
#define CF_OP_ALU_PUSH_BEFORE              81
#define CF_OP_ALU_POP_AFTER                82
#define CF_OP_ALU_POP2_AFTER               83
#define CF_OP_ALU_EXT                      84
#define CF_OP_ALU_CONTINUE                 85
#define CF_OP_ALU_BREAK                    86
#define CF_OP_ALU_VALID_PIXEL_MODE         87
#define CF_OP_ALU_ELSE_AFTER               88

/* CF_NATIVE means that r600_bytecode_cf contains pre-encoded native data */
#define CF_NATIVE                          89

enum r600_chip_class {
	ISA_CC_R600,
	ISA_CC_R700,
	ISA_CC_EVERGREEN,
	ISA_CC_CAYMAN
};

struct r600_isa {
	enum r600_chip_class hw_class;

	/* these arrays provide reverse mapping - opcode => table_index,
	 * typically we don't need such lookup, unless we are decoding the native
	 * bytecode (e.g. when reading the bytestream from llvm backend) */
	unsigned *alu_op2_map;
	unsigned *alu_op3_map;
	unsigned *fetch_map;
	unsigned *cf_map;
};

struct r600_context;

int r600_isa_init(struct r600_context *ctx, struct r600_isa *isa);
int r600_isa_destroy(struct r600_isa *isa);

extern const struct alu_op_info r600_alu_op_table[];

unsigned
r600_alu_op_table_size(void);

const struct alu_op_info *
r600_isa_alu(unsigned op);

const struct fetch_op_info *
r600_isa_fetch(unsigned op);

const struct cf_op_info *
r600_isa_cf(unsigned op);

static inline unsigned
r600_isa_alu_opcode(enum r600_chip_class chip_class, unsigned op) {
	int opc =  r600_isa_alu(op)->opcode[chip_class >> 1];
	assert(opc != -1);
	return opc;
}

static inline unsigned
r600_isa_alu_slots(enum r600_chip_class chip_class, unsigned op) {
	unsigned slots = r600_isa_alu(op)->slots[chip_class];
	assert(slots != 0);
	return slots;
}

static inline unsigned
r600_isa_fetch_opcode(enum r600_chip_class chip_class, unsigned op) {
	int opc = r600_isa_fetch(op)->opcode[chip_class];
	assert(opc != -1);
	return opc;
}

static inline unsigned
r600_isa_cf_opcode(enum r600_chip_class chip_class, unsigned op) {
	int opc = r600_isa_cf(op)->opcode[chip_class];
	assert(opc != -1);
	return opc;
}

static inline unsigned
r600_isa_alu_by_opcode(struct r600_isa* isa, unsigned opcode, unsigned is_op3) {
	unsigned op;
	if (is_op3) {
		assert(isa->alu_op3_map);
		op = isa->alu_op3_map[opcode];
	} else {
		assert(isa->alu_op2_map);
		op = isa->alu_op2_map[opcode];
	}
	assert(op);
	return op - 1;
}

static inline unsigned
r600_isa_fetch_by_opcode(struct r600_isa* isa, unsigned opcode) {
	unsigned op;
	assert(isa->fetch_map);
	op = isa->fetch_map[opcode];
	assert(op);
	return op - 1;
}

static inline unsigned
r600_isa_cf_by_opcode(struct r600_isa* isa, unsigned opcode, unsigned is_alu) {
	unsigned op;
	assert(isa->cf_map);
	/* using offset for CF_ALU_xxx opcodes because they overlap with other
	 * CF opcodes (they use different encoding in hw) */
	op = isa->cf_map[is_alu ? opcode + 0x80 : opcode];
	assert(op);
	return op - 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* R600_ISA_H_ */
