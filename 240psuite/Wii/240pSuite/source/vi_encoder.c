/****************************************************************************
 *  vi_encoder.c
 *
 *  Wii Audio/Video Encoder support
 *
 *  Copyright (C) 2009 Eke-Eke, with some code from libogc (C) Hector Martin 
 * 
 *  Redistribution and use of this code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *   - Redistributions may not be sold, nor may they be used in a commercial
 *     product or activity.
 *
 *   - Redistributions that are modified from the original source must include the
 *     complete source code, including the source code for all components used by a
 *     binary built from the modified sources. However, as a special exception, the
 *     source code distributed need not include anything that is normally distributed
 *     (in either source or binary form) with the major components (compiler, kernel,
 *     and so on) of the operating system on which the executable runs, unless that
 *     component itself accompanies the executable.
 *
 *   - Redistributions must reproduce the above copyright notice, this list of
 *     conditions and the following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************************/

#ifdef HW_RVL

#include <string.h>
#include <gccore.h>
#include <ogcsys.h>
#include <ogc/machine/processor.h>

#include "vi_encoder.h"

/****************************************************************************
 *  I2C driver by Hector Martin (marcan)
 *
 ****************************************************************************/

#define _SHIFTL(v, s, w)	\
    ((u32) (((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)	\
    ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))

extern void udelay(int us);

static u32 i2cIdentFirst = 0;
static u32 i2cIdentFlag = 1;
static vu16* const _viReg = (u16*)0xCC002000;
static vu32* const _i2cReg = (u32*)0xCD800000;

static inline void __viOpenI2C(u32 channel)
{
	u32 val = ((_i2cReg[49]&~0x8000)|0x4000);
	val |= _SHIFTL(channel,15,1);
	_i2cReg[49] = val;
}

static inline u32 __viSetSCL(u32 channel)
{
	u32 val = (_i2cReg[48]&~0x4000);
	val |= _SHIFTL(channel,14,1);
	_i2cReg[48] = val;
	return 1;
}
static inline u32 __viSetSDA(u32 channel)
{
	u32 val = (_i2cReg[48]&~0x8000);
	val |= _SHIFTL(channel,15,1);
	_i2cReg[48] = val;
	return 1;
}

static inline u32 __viGetSDA()
{
	return _SHIFTR(_i2cReg[50],15,1);
}

static inline void __viCheckI2C()
{
	__viOpenI2C(0);
	udelay(4);

	i2cIdentFlag = 0;
	if(__viGetSDA()!=0) i2cIdentFlag = 1;
}

static u32 __sendSlaveAddress(u8 addr)
{
	u32 i;

	__viSetSDA(i2cIdentFlag^1);
	udelay(2);

	__viSetSCL(0);
	for(i=0;i<8;i++) {
		if(addr&0x80) __viSetSDA(i2cIdentFlag);
		else __viSetSDA(i2cIdentFlag^1);
		udelay(2);

		__viSetSCL(1);
		udelay(2);

		__viSetSCL(0);
		addr <<= 1;
	}

	__viOpenI2C(0);
	udelay(2);

	__viSetSCL(1);
	udelay(2);

	if(i2cIdentFlag==1 && __viGetSDA()!=0) return 0;

	__viSetSDA(i2cIdentFlag^1);
	__viOpenI2C(1);
	__viSetSCL(0);

	return 1;
}

static u32 __VISendI2CData(u8 addr,void *val,u32 len)
{
	u8 c;
	s32 i,j;
	u32 level,ret;

	if(i2cIdentFirst==0) {
		__viCheckI2C();
		i2cIdentFirst = 1;
	}

	_CPU_ISR_Disable(level);

	__viOpenI2C(1);
	__viSetSCL(1);

	__viSetSDA(i2cIdentFlag);
	udelay(4);

	ret = __sendSlaveAddress(addr);
	if(ret==0) {
		_CPU_ISR_Restore(level);
		return 0;
	}

	__viOpenI2C(1);
	for(i=0;i<len;i++) {
		c = ((u8*)val)[i];
		for(j=0;j<8;j++) {
			if(c&0x80) __viSetSDA(i2cIdentFlag);
			else __viSetSDA(i2cIdentFlag^1);
			udelay(2);

			__viSetSCL(1);
			udelay(2);
			__viSetSCL(0);

			c <<= 1;
		}
		__viOpenI2C(0);
		udelay(2);
		__viSetSCL(1);
		udelay(2);

		if(i2cIdentFlag==1 && __viGetSDA()!=0) {
			_CPU_ISR_Restore(level);
			return 0;
		}

		__viSetSDA(i2cIdentFlag^1);
		__viOpenI2C(1);
		__viSetSCL(0);
	}

	__viOpenI2C(1);
	__viSetSDA(i2cIdentFlag^1);
	udelay(2);
	__viSetSDA(i2cIdentFlag);

	_CPU_ISR_Restore(level);
	return 1;
}

static void __VIWriteI2CRegister8(u8 reg, u8 data)
{
	u8 buf[2];
	buf[0] = reg;
	buf[1] = data;
	__VISendI2CData(0xe0,buf,2);
	udelay(2);
}

static void __VIWriteI2CRegister16(u8 reg, u16 data)
{
	u8 buf[3];
	buf[0] = reg;
	buf[1] = data >> 8;
	buf[2] = data & 0xFF;
	__VISendI2CData(0xe0,buf,3);
	udelay(2);
}

static void __VIWriteI2CRegister32(u8 reg, u32 data)
{
	u8 buf[5];
	buf[0] = reg;
	buf[1] = data >> 24;
	buf[2] = (data >> 16) & 0xFF;
	buf[3] = (data >> 8) & 0xFF;
	buf[4] = data & 0xFF;
	__VISendI2CData(0xe0,buf,5);
	udelay(2);
}

static void __VIWriteI2CRegisterBuf(u8 reg, int size, u8 *data)
{
	u8 buf[0x100];
	buf[0] = reg;
	memcpy(&buf[1], data, size);
	__VISendI2CData(0xe0,buf,size+1);
	udelay(2);
}

/****************************************************************************
 *  A/V functions support (Eke-Eke)
 *
 ****************************************************************************/
static const u8 gamma_coeffs[][33] =
{
	/* GM_0_0 */
	{
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
 
	},	
 
	/* GM_0_1 */
	{
		 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x03, 0x97, 0x3B, 0x49,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x80, 0x1B, 0x80, 0xEB, 0x00
	},	
 
	/* GM_0_2 */
	{
		 0x00, 0x00, 0x00, 0x28, 0x00, 0x5A, 0x02, 0xDB, 0x0D, 0x8D, 0x30, 0x49,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x10, 0x00, 0x10, 0x40, 0x11, 0x00, 0x18, 0x80, 0x42, 0x00, 0xEB, 0x00
	},	
 
	/* GM_0_3 */
	{
		 0x00, 0x00, 0x00, 0x7A, 0x02, 0x3C, 0x07, 0x6D, 0x12, 0x9C, 0x27, 0x24,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x10, 0x00, 0x10, 0xC0, 0x15, 0x80, 0x29, 0x00, 0x62, 0x00, 0xEB, 0x00
	},	
 
	/* GM_0_4 */
	{
		 0x00, 0x4E, 0x01, 0x99, 0x05, 0x2D, 0x0B, 0x24, 0x14, 0x29, 0x20, 0xA4,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x00, 0x10, 0x10, 0x40, 0x12, 0xC0, 0x1D, 0xC0, 0x3B, 0x00, 0x78, 0xC0, 0xEB, 0x00
	},	
 
	/* GM_0_5 */
	{
		 0x00, 0xEC, 0x03, 0xD7, 0x08, 0x00, 0x0D, 0x9E, 0x14, 0x3E, 0x1B, 0xDB,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x10, 0xC0, 0x16, 0xC0, 0x27, 0xC0, 0x4B, 0x80, 0x89, 0x80, 0xEB, 0x00
	},	
 
	/* GM_0_6 */
	{
		 0x02, 0x76, 0x06, 0x66, 0x0A, 0x96, 0x0E, 0xF3, 0x13, 0xAC, 0x18, 0x49,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB, 
		 0x10, 0x00, 0x12, 0x00, 0x1C, 0x00, 0x32, 0x80, 0x59, 0xC0, 0x96, 0x00, 0xEB, 0x00
	},	
 
	/* GM_0_7 */
	{
		 0x04, 0xEC, 0x08, 0xF5, 0x0C, 0x96, 0x0F, 0xCF, 0x12, 0xC6, 0x15, 0x80,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB, 
		 0x10, 0x00, 0x14, 0x00, 0x22, 0x00, 0x3C, 0xC0, 0x66, 0x40, 0x9F, 0xC0, 0xEB, 0x00
	},	
 
	/* GM_0_8 */
	{
		 0x08, 0x00, 0x0B, 0xAE, 0x0E, 0x00, 0x10, 0x30, 0x11, 0xCB, 0x13, 0x49,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB, 
		 0x10, 0x00, 0x16, 0x80, 0x28, 0xC0, 0x46, 0x80, 0x71, 0x00, 0xA7, 0x80, 0xEB, 0x00
	},	
 
	/* GM_0_9 */
	{
		 0x0B, 0xB1, 0x0E, 0x14, 0x0F, 0x2D, 0x10, 0x18, 0x10, 0xE5, 0x11, 0x80,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x19, 0x80, 0x2F, 0x80, 0x4F, 0xC0, 0x7A, 0x00, 0xAD, 0xC0, 0xEB, 0x00
	},	
 
	/* GM_1_0 */
	{
		 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
		 0x10, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xEB,
		 0x10, 0x00, 0x20, 0x00, 0x40, 0x00, 0x60, 0x00, 0x80, 0x00, 0xA0, 0x00, 0xEB, 0x00
	},	
 
	/* GM_1_1 */
	{
		 0x14, 0xEC, 0x11, 0xC2, 0x10, 0x78, 0x0F, 0xB6, 0x0F, 0x2F, 0x0E, 0xB6,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x21, 0x00, 0x3C, 0xC0, 0x5F, 0xC0, 0x89, 0x00, 0xB7, 0x80, 0xEB, 0x00
	},	
 
	/* GM_1_2 */
	{
		 0x19, 0xD8, 0x13, 0x33, 0x10, 0xD2, 0x0F, 0x6D, 0x0E, 0x5E, 0x0D, 0xA4,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x25, 0x00, 0x43, 0x00, 0x66, 0xC0, 0x8F, 0x40, 0xBB, 0x40, 0xEB, 0x00
	},	
 
	/* GM_1_3 */
	{
		 0x1E, 0xC4, 0x14, 0x7A, 0x11, 0x0F, 0xF, 0x0C, 0x0D, 0xA1, 0x0C, 0xB6,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x29, 0x00, 0x49, 0x00, 0x6D, 0x40, 0x94, 0xC0, 0xBE, 0x80, 0xEB, 0x00
	},	
 
	/* GM_1_4 */
	{
		 0x24, 0x00, 0x15, 0x70, 0x11, 0x0F, 0x0E, 0xAA, 0x0D, 0x0F, 0x0B, 0xDB,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x2D, 0x40, 0x4E, 0xC0, 0x73, 0x00, 0x99, 0x80, 0xC1, 0x80, 0xEB, 0x00
 	},	
 
	/* GM_1_5 */
	{
		 0x29, 0x3B, 0x16, 0x3D, 0x11, 0x0F, 0x0E, 0x30, 0x0C, 0x7D, 0x0B, 0x24,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x31, 0x80, 0x54, 0x40, 0x78, 0x80, 0x9D, 0xC0, 0xC4, 0x00, 0xEB, 0x00
	},	
 
	/* GM_1_6 */
	{
		 0x2E, 0x27, 0x17, 0x0A, 0x10, 0xD2, 0x0D, 0xE7, 0x0B, 0xEB, 0x0A, 0x80,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x35, 0x80, 0x59, 0x80, 0x7D, 0x40, 0xA1, 0xC0, 0xC6, 0x40, 0xEB, 0x00
	},	
 
	/* GM_1_7 */
	{
		 0x33, 0x62, 0x17, 0x5C, 0x10, 0xD2, 0x0D, 0x6D, 0x0B, 0x6D, 0x09, 0xED,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x39, 0xC0, 0x5E, 0x40, 0x82, 0x00, 0xA5, 0x40, 0xC8, 0x40, 0xEB, 0x00
	},	
 
	/* GM_1_8 */
	{
		 0x38, 0x4E, 0x17, 0xAE, 0x10, 0xB4, 0x0D, 0x0C, 0x0A, 0xF0, 0x09, 0x6D,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x3D, 0xC0, 0x62, 0xC0, 0x86, 0x40, 0xA8, 0x80, 0xCA, 0x00, 0xEB, 0x00
	},	
 
	/* GM_1_9 */
	{
		 0x3D, 0x3B, 0x18, 0x00, 0x10, 0x5A, 0x0C, 0xC3, 0x0A, 0x72, 0x09, 0x00,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x41, 0xC0, 0x67, 0x40, 0x8A, 0x00, 0xAB, 0x80, 0xCB, 0x80, 0xEB, 0x00
	},	
 
	/* GM_2_0 */
	{
		 0x41, 0xD8, 0x18, 0x28, 0x10, 0x3C, 0x0C, 0x49, 0x0A, 0x1F, 0x08, 0x92,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x45, 0x80, 0x6B, 0x40, 0x8D, 0xC0, 0xAE, 0x00, 0xCD, 0x00, 0xEB, 0x00
	},	
 
	/* GM_2_1 */
	{
		 0x46, 0x76, 0x18, 0x51, 0x0F, 0xE1, 0x0C, 0x00, 0x09, 0xB6, 0x08, 0x36,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x49, 0x40, 0x6F, 0x40, 0x91, 0x00, 0xB0, 0x80, 0xCE, 0x40, 0xEB, 0x00
	},	
 
	/* GM_2_2 */
	{
		 0x4A, 0xC4, 0x18, 0x7A, 0x0F, 0xA5, 0x0B, 0x9E, 0x09, 0x63, 0x07, 0xDB,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x4C, 0xC0, 0x73, 0x00, 0x94, 0x40, 0xB2, 0xC0, 0xCF, 0x80, 0xEB, 0x00
	},	
 
	/* GM_2_3 */
	{
		 0x4F, 0x13, 0x18, 0x51, 0x0F, 0x69, 0x0B, 0x6D, 0x09, 0x0F, 0x07, 0x80,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x50, 0x40, 0x76, 0x40, 0x97, 0x00, 0xB5, 0x00, 0xD0, 0xC0, 0xEB, 0x00
	},	
 
	/* GM_2_4 */
	{
		 0x53, 0x13, 0x18, 0x7A, 0x0F, 0x0F, 0x0B, 0x24, 0x08, 0xBC, 0x07, 0x36,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x53, 0x80, 0x79, 0xC0, 0x99, 0xC0, 0xB7, 0x00, 0xD1, 0xC0, 0xEB, 0x00
	},	
 
	/* GM_2_5 */
	{
		 0x57, 0x13, 0x18, 0x51, 0x0E, 0xF0, 0x0A, 0xC3, 0x08, 0x7D, 0x06, 0xED,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x56, 0xC0, 0x7C, 0xC0, 0x9C, 0x80, 0xB8, 0xC0, 0xD2, 0xC0, 0xEB, 0x00
	},	
 
	/* GM_2_6 */
	{
		 0x5B, 0x13, 0x18, 0x28, 0x0E, 0x96, 0x0A, 0x92, 0x08, 0x29, 0x06, 0xB6,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x5A, 0x00, 0x7F, 0xC0, 0x9E, 0xC0, 0xBA, 0x80, 0xD3, 0x80, 0xEB, 0x00
	},	
 
	/* GM_2_7 */
	{
		 0x5E, 0xC4, 0x18, 0x00, 0x0E, 0x78, 0x0A, 0x30, 0x08, 0x00, 0x06, 0x6D,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x5D, 0x00, 0x82, 0x80, 0xA1, 0x40, 0xBC, 0x00, 0xD4, 0x80, 0xEB, 0x00
	},	
 
	/* GM_2_8 */
	{
		 0x62, 0x76, 0x17, 0xD7, 0x0E, 0x1E, 0x0A, 0x00, 0x07, 0xC1, 0x06, 0x36,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x60, 0x00, 0x85, 0x40, 0xA3, 0x40, 0xBD, 0x80, 0xD5, 0x40, 0xEB, 0x00
	},	
 
	/* GM_2_9 */
	{
		 0x65, 0xD8, 0x17, 0xAE, 0x0D, 0xE1, 0x09, 0xCF, 0x07, 0x82, 0x06, 0x00,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x62, 0xC0, 0x87, 0xC0, 0xA5, 0x40, 0xBF, 0x00, 0xD6, 0x00, 0xEB, 0x00
	},	
 
	/* GM_3_0 */
	{
		 0x69, 0x3B, 0x17, 0x85, 0x0D, 0xA5, 0x09, 0x86, 0x07, 0x43, 0x05, 0xDB,
		 0x10, 0x1D, 0x36, 0x58, 0x82, 0xB3, 0xEB,
		 0x10, 0x00, 0x65, 0x80, 0x8A, 0x40, 0xA7, 0x40, 0xC0, 0x40, 0xD6, 0x80, 0xEB, 0x00
	}
};

void __VISetTiming(u8 timing)
{
  __VIWriteI2CRegister8(0x00,timing);
}

void __VISetYUVSEL(u8 dtvstatus)
{
  u8 vdacFlagRegion = 0;
  u32 currTvMode = _SHIFTR(_viReg[1],8,2);
  if(currTvMode==VI_PAL || currTvMode==VI_EURGB60)
    vdacFlagRegion = 2;
	else if(currTvMode==VI_MPAL)
    vdacFlagRegion = 1;

	__VIWriteI2CRegister8(0x01, _SHIFTL(dtvstatus,5,3)|(vdacFlagRegion&0x1f)); 
}

void __VISetVBICtrl(u16 data)
{
	__VIWriteI2CRegister16(0x02, data);
}

void __VISetTrapFilter(u8 enable)
{
	if (enable)
    __VIWriteI2CRegister8(0x03, 1);
	else
    __VIWriteI2CRegister8(0x03, 0);
}

void __VISet3in1Output(u8 enable)
{
  __VIWriteI2CRegister8(0x04,enable);
}

void __VISetCGMS(u16 value)
{
	__VIWriteI2CRegister16(0x05, value);
}

void __VISetWSS(u16 value)
{
	__VIWriteI2CRegister16(0x08, value);
}

void __VISetRGBOverDrive(u8 value)
{
  u32 currTvMode = _SHIFTR(_viReg[1],8,2);
  if (currTvMode == VI_DEBUG)
    __VIWriteI2CRegister8(0x0A,(value<<1)|1);
  else
    __VIWriteI2CRegister8(0x0A,0);
}

void __VISetOverSampling(void)
{
  __VIWriteI2CRegister8(0x65,1);
}

void __VISetCCSEL(void)
{
  __VIWriteI2CRegister8(0x6a,1);
}

void __VISetFilterEURGB60(u8 enable)
{
	__VIWriteI2CRegister8(0x6e, enable);
}

void __VISetVolume(u16 value)
{
  __VIWriteI2CRegister16(0x71,value);
}

void __VISetClosedCaption(u32 value)
{
	__VIWriteI2CRegister32(0x7a, value);
}

void __VISetGamma(VIGamma gamma)
{
  u8 *data = (u8 *)&gamma_coeffs[gamma][0];
  __VIWriteI2CRegisterBuf(0x10, 0x21, data);
}

/* User Configurable */

void VIDEO_SetGamma(VIGamma gamma)
{
  __VISetGamma(gamma);
}

void VIDEO_SetTrapFilter(bool enable)
{
    __VISetTrapFilter(enable ? 1 : 0);
}

#endif
