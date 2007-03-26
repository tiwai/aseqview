#include "bitmaps/blank1.xbm"
#include "bitmaps/Ces.xbm"
#include "bitmaps/Ges.xbm"
#include "bitmaps/Des.xbm"
#include "bitmaps/As.xbm"
#include "bitmaps/Es.xbm"
#include "bitmaps/B.xbm"
#include "bitmaps/F.xbm"
#include "bitmaps/C.xbm"
#include "bitmaps/G.xbm"
#include "bitmaps/D.xbm"
#include "bitmaps/A.xbm"
#include "bitmaps/E.xbm"
#include "bitmaps/H.xbm"
#include "bitmaps/Fis.xbm"
#include "bitmaps/Cis.xbm"
#include "bitmaps/As_m.xbm"
#include "bitmaps/Es_m.xbm"
#include "bitmaps/B_m.xbm"
#include "bitmaps/F_m.xbm"
#include "bitmaps/C_m.xbm"
#include "bitmaps/G_m.xbm"
#include "bitmaps/D_m.xbm"
#include "bitmaps/A_m.xbm"
#include "bitmaps/E_m.xbm"
#include "bitmaps/H_m.xbm"
#include "bitmaps/Fis_m.xbm"
#include "bitmaps/Cis_m.xbm"
#include "bitmaps/Gis_m.xbm"
#include "bitmaps/Dis_m.xbm"
#include "bitmaps/Ais_m.xbm"

#define tk_width 33
#define tk_height 20
static char *tk_bits[] = {
	blank1_bits, Ces_bits, Ges_bits, Des_bits,
	As_bits, Es_bits, B_bits, F_bits,
	C_bits, G_bits, D_bits, A_bits,
	E_bits, H_bits, Fis_bits, Cis_bits,
	blank1_bits, As_m_bits, Es_m_bits, B_m_bits,
	F_m_bits, C_m_bits, G_m_bits, D_m_bits,
	A_m_bits, E_m_bits, H_m_bits, Fis_m_bits,
	Cis_m_bits, Gis_m_bits, Dis_m_bits, Ais_m_bits
};

#include "bitmaps/blank2.xbm"
#include "bitmaps/eq.xbm"
#include "bitmaps/Py.xbm"
#include "bitmaps/mt.xbm"
#include "bitmaps/pu.xbm"
#include "bitmaps/u0.xbm"
#include "bitmaps/u1.xbm"
#include "bitmaps/u2.xbm"
#include "bitmaps/u3.xbm"

#define tt_width 19
#define tt_height 16
static char *tt_bits[] = {
	blank2_bits,
	eq_bits, Py_bits, mt_bits, pu_bits,
	u0_bits, u1_bits, u2_bits, u3_bits
};

static int tt_rgb[][3] = {
	{ 0x0000, 0x0000, 0x0000 },
	{ 0x4000, 0x4000, 0x4000 }, { 0x0000, 0xffff, 0x0000 },
	{ 0x8000, 0x8000, 0xffff }, { 0xffff, 0xffff, 0x0000 },
	{ 0xffff, 0x0000, 0xffff }, { 0xffff, 0x0000, 0xffff },
	{ 0xffff, 0x0000, 0xffff }, { 0xffff, 0x0000, 0xffff }
};

