/*
 * Copyright (c) 2006-2014 MStar Semiconductor, Inc.
 * All rights reserved.

 * Unless otherwise stipulated in writing, any and all information contained
 * herein regardless in any format shall remain the sole proprietary of
 * MStar Semiconductor Inc. and be kept in strict confidence
 * (??MStar Confidential Information??) by the recipient.
 * Any unauthorized act including without limitation unauthorized disclosure,
 * copying, use, reproduction, sale, distribution, modification, disassembling,
 * reverse engineering and compiling of the contents of MStar Confidential
 * Information is unlawful and strictly prohibited. MStar hereby reserves the
 * rights to any and all damages, losses, costs and expenses resulting
 * therefrom.
 */

/**
 *
 * @file    mstar_drv_utility_adaption.c
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#include "mstar_drv_utility_adaption.h"

u16 RegGet16BitValue(u16 nAddr)
{
	u8 tx_data[3] = {0x10, (nAddr >> 8) & 0xFF, nAddr & 0xFF};
	u8 rx_data[2] = {0};

	IicWriteData(SLAVE_I2C_ID_DBBUS, &tx_data[0], 3);
	IicReadData(SLAVE_I2C_ID_DBBUS, &rx_data[0], 2);

	return rx_data[1] << 8 | rx_data[0];
}

u8 RegGetLByteValue(u16 nAddr)
{
	u8 tx_data[3] = {0x10, (nAddr >> 8) & 0xFF, nAddr & 0xFF};
	u8 rx_data = {0};

	IicWriteData(SLAVE_I2C_ID_DBBUS, &tx_data[0], 3);
	IicReadData(SLAVE_I2C_ID_DBBUS, &rx_data, 1);

	return rx_data;
}

u8 RegGetHByteValue(u16 nAddr)
{
	u8 tx_data[3] = {0x10, (nAddr >> 8) & 0xFF, (nAddr & 0xFF) + 1};
	u8 rx_data = {0};

	IicWriteData(SLAVE_I2C_ID_DBBUS, &tx_data[0], 3);
	IicReadData(SLAVE_I2C_ID_DBBUS, &rx_data, 1);

	return rx_data;
}

void RegSet16BitValue(u16 nAddr, u16 nData)
{
	u8 tx_data[5] = {0x10, (nAddr >> 8) & 0xFF, nAddr & 0xFF, nData & 0xFF,
		nData >> 8};
	IicWriteData(SLAVE_I2C_ID_DBBUS, &tx_data[0], 5);
}

void RegSetLByteValue(u16 nAddr, u8 nData)
{
	u8 tx_data[4] = {0x10, (nAddr >> 8) & 0xFF, nAddr & 0xFF, nData};
	IicWriteData(SLAVE_I2C_ID_DBBUS, &tx_data[0], 4);
}

void RegSetHByteValue(u16 nAddr, u8 nData)
{
	u8 tx_data[4] = {0x10, (nAddr >> 8) & 0xFF, (nAddr & 0xFF) + 1, nData};
	IicWriteData(SLAVE_I2C_ID_DBBUS, &tx_data[0], 4);
}

/* Set bit on nData from 0 to 1 */
void RegSet16BitValueOn(u16 nAddr, u16 nData)
{
	u16 rData = RegGet16BitValue(nAddr);
	rData |= nData;
	RegSet16BitValue(nAddr, rData);
}

/* Set bit on nData from 1 to 0 */
void RegSet16BitValueOff(u16 nAddr, u16 nData)
{
	u16 rData = RegGet16BitValue(nAddr);
	rData &= (~nData);
	RegSet16BitValue(nAddr, rData);
}

u16 RegGet16BitValueByAddressMode(u16 nAddr, enum AddressMode_e eAddressMode)
{
	u16 nData = 0;

	if (eAddressMode == ADDRESS_MODE_16BIT)
		nAddr = nAddr - (nAddr & 0xFF) + ((nAddr & 0xFF) << 1);

	nData = RegGet16BitValue(nAddr);

	return nData;
}

void RegSet16BitValueByAddressMode(u16 nAddr, u16 nData,
		enum AddressMode_e eAddressMode)
{
	if (eAddressMode == ADDRESS_MODE_16BIT)
		nAddr = nAddr - (nAddr & 0xFF) + ((nAddr & 0xFF) << 1);

	RegSet16BitValue(nAddr, nData);
}

void RegMask16BitValue(u16 nAddr, u16 nMask, u16 nData,
		enum AddressMode_e eAddressMode)
{
	u16 nTmpData = 0;

	if (nData > nMask)
		return;

	nTmpData = RegGet16BitValueByAddressMode(nAddr, eAddressMode);
	nTmpData = (nTmpData & (~nMask));
	nTmpData = (nTmpData | nData);
	RegSet16BitValueByAddressMode(nAddr, nTmpData, eAddressMode);
}

void DbBusEnterSerialDebugMode(void)
{
	u8 data[5];

	/* Enter the Serial Debug Mode */
	data[0] = 0x53;
	data[1] = 0x45;
	data[2] = 0x52;
	data[3] = 0x44;
	data[4] = 0x42;

	IicWriteData(SLAVE_I2C_ID_DBBUS, data, 5);
}

void DbBusExitSerialDebugMode(void)
{
	u8 data[1];

	/* Exit the Serial Debug Mode */
	data[0] = 0x45;

	IicWriteData(SLAVE_I2C_ID_DBBUS, data, 1);

}

void DbBusIICUseBus(void)
{
	u8 data[1];

	/* IIC Use Bus */
	data[0] = 0x35;

	IicWriteData(SLAVE_I2C_ID_DBBUS, data, 1);
}

void DbBusIICNotUseBus(void)
{
	u8 data[1];

	/* IIC Not Use Bus */
	data[0] = 0x34;

	IicWriteData(SLAVE_I2C_ID_DBBUS, data, 1);
}

void DbBusIICReshape(void)
{
	u8 data[1];

	/* IIC Re-shape */
	data[0] = 0x71;

	IicWriteData(SLAVE_I2C_ID_DBBUS, data, 1);
}

void DbBusStopMCU(void)
{
	u8 data[1];

	/* Stop the MCU */
	data[0] = 0x37;

	IicWriteData(SLAVE_I2C_ID_DBBUS, data, 1);
}

void DbBusNotStopMCU(void)
{
	u8 data[1];

	/* Not Stop the MCU */
	data[0] = 0x36;

	IicWriteData(SLAVE_I2C_ID_DBBUS, data, 1);
}

s32 IicWriteData(u8 nSlaveId, u8 *pBuf, u16 nSize)
{
	s32 rc = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = nSlaveId,
			/*
			 * if read flag is undefined, then
			 * it means write flag.
			 */
			.flags = 0,
			.len = nSize,
			.buf = pBuf,
		},
	};

	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	   transmitted, else error code. */
	if (g_I2cClient != NULL) {
		rc = i2c_transfer(g_I2cClient->adapter, msgs, 1);

		if (rc == 1)
			rc = nSize;
		else
			PRINTF_ERR("IicWriteData() error %d\n", rc);
	} else
		PRINTF_ERR("i2c client is NULL\n");

	return rc;
}

s32 IicReadData(u8 nSlaveId, u8 *pBuf, u16 nSize)
{
	s32 rc = 0;

	struct i2c_msg msgs[] = {
		{
			.addr = nSlaveId,
			.flags = I2C_M_RD, /* read flag */
			.len = nSize,
			.buf = pBuf,
		},
	};

	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	   transmitted, else error code. */
	if (g_I2cClient != NULL) {
		rc = i2c_transfer(g_I2cClient->adapter, msgs, 1);

		if (rc == 1)
			rc = nSize;
		else /* rc < 0 */
			PRINTF_ERR("IicReadData() error %d\n", rc);
	} else {
		PRINTF_ERR("i2c client is NULL\n");
	}

	return rc;
}

s32 IicSegmentReadDataByDbBus(u8 nRegBank, u8 nRegAddr, u8 *pBuf,
		u16 nSize, u16 nMaxI2cLengthLimit)
{
	s32 rc = 0;
	u16 nLeft = nSize;
	u16 nOffset = 0;
	u16 nSegmentLength = 0;
	u16 nReadSize = 0;
	u16 nOver = 0;
	u8  szWriteBuf[3] = {0};
	u8  nNextRegBank = nRegBank;
	u8  nNextRegAddr = nRegAddr;

	struct i2c_msg msgs[2] = {
		{
			.addr = SLAVE_I2C_ID_DBBUS,
			.flags = 0, /* write flag */
			.len = 3,
			.buf = szWriteBuf,
		},
		{
			.addr = SLAVE_I2C_ID_DBBUS,
			.flags =  I2C_M_RD, /* read flag */
		},
	};

	/*
	 * If everything went ok (i.e. 1 msg transmitted),
	 * return #bytes transmitted, else error code.
	 */
	if (g_I2cClient != NULL) {
		if (nMaxI2cLengthLimit >= 256)
			nSegmentLength = 256;
		else
			nSegmentLength = 128;

		/* add for debug */
		PRINTF_ERR("nSegmentLength = %d\n", nSegmentLength);

		while (nLeft > 0) {
			szWriteBuf[0] = 0x10;
			nRegBank = nNextRegBank;
			szWriteBuf[1] = nRegBank;
			nRegAddr = nNextRegAddr;
			szWriteBuf[2] = nRegAddr;

			PRINTF_ERR("nRegBank = 0x%x\n", nRegBank);
			PRINTF_ERR("nRegAddr = 0x%x\n", nRegAddr);

			msgs[1].buf = &pBuf[nOffset];

			if (nLeft > nSegmentLength) {
				if ((nRegAddr + nSegmentLength) <
				    MAX_TOUCH_IC_REGISTER_BANK_SIZE) {
					nNextRegAddr = nRegAddr +
						nSegmentLength;

					PRINTF_ERR("nNextRegAddr = 0x%x\n",
							nNextRegAddr);

					msgs[1].len = nSegmentLength;
					nLeft -= nSegmentLength;
					nOffset += nSegmentLength;
				} else if ((nRegAddr + nSegmentLength) ==
					    MAX_TOUCH_IC_REGISTER_BANK_SIZE) {
					nNextRegAddr = 0x00;
					/*
					 * shift to read data from next
					 * register bank
					 */
					nNextRegBank = nRegBank + 1;

					PRINTF_ERR("nNextRegBank = 0x%x\n",
							nNextRegBank);

					msgs[1].len = nSegmentLength;
					nLeft -= nSegmentLength;
					nOffset += nSegmentLength;
				} else {
					nNextRegAddr = 0x00;
					/*
					 * shift to read data from next
					 * register bank
					 */
					nNextRegBank = nRegBank + 1;

					PRINTF_INFO("nNextRegBank = 0x%x\n",
							nNextRegBank);

					nOver = (nRegAddr + nSegmentLength) -
						MAX_TOUCH_IC_REGISTER_BANK_SIZE;

					PRINTF_ERR("nOver = 0x%x\n", nOver);

					msgs[1].len = nSegmentLength - nOver;
					nLeft -= msgs[1].len;
					nOffset += msgs[1].len;
				}
			} else {
				if ((nRegAddr + nLeft) <
				    MAX_TOUCH_IC_REGISTER_BANK_SIZE) {
					nNextRegAddr = nRegAddr + nLeft;

					PRINTF_ERR("nNextRegAddr = 0x%x\n",
							nNextRegAddr);

					msgs[1].len = nLeft;
					nLeft = 0;
				} else if ((nRegAddr + nLeft) ==
					    MAX_TOUCH_IC_REGISTER_BANK_SIZE) {
					nNextRegAddr = 0x00;
					/*
					 * shift to read data from next
					 * register bank
					 */
					nNextRegBank = nRegBank + 1;

					PRINTF_ERR("nNextRegBank = 0x%x\n",
							nNextRegBank);
					msgs[1].len = nLeft;
					nLeft = 0;
				/*
				 * ((nRegAddr + nLeft) >
				 * MAX_TOUCH_IC_REGISTER_BANK_SIZE)
				 */
				} else {
					nNextRegAddr = 0x00;
					/*
					 * shift to read data from next
					 * register bank
					 */
					nNextRegBank = nRegBank + 1;

					PRINTF_ERR("nNextRegBank = 0x%x\n",
							nNextRegBank);

					nOver = (nRegAddr + nLeft) -
						MAX_TOUCH_IC_REGISTER_BANK_SIZE;

					PRINTF_ERR("nOver = 0x%x\n", nOver);

					msgs[1].len = nLeft - nOver;
					nLeft -= msgs[1].len;
					nOffset += msgs[1].len;
				}
			}

			rc = i2c_transfer(g_I2cClient->adapter, &msgs[0], 2);
			if (rc == 2)
				nReadSize = nReadSize + msgs[1].len;
			else {
				PRINTF_ERR("IicSegmentReadDataByDbBus()->");
				PRINTF_ERR("i2c_transfer() error %d\n", rc);
				return rc;
			}
		}
	} else
		PRINTF_ERR("i2c client is NULL\n");

	return nReadSize;
}

s32 IicSegmentReadDataBySmBus(u16 nAddr, u8 *pBuf, u16 nSize,
		u16 nMaxI2cLengthLimit)
{
	s32 rc = 0;
	u16 nLeft = nSize;
	u16 nOffset = 0;
	u16 nReadSize = 0;
	u8  szWriteBuf[3] = {0};

	struct i2c_msg msgs[2] = {
		{
			.addr = SLAVE_I2C_ID_DWI2C,
			.flags = 0, /* write flag */
			.len = 3,
			.buf = szWriteBuf,
		},
		{
			.addr = SLAVE_I2C_ID_DWI2C,
			.flags =  I2C_M_RD, /* read flag */
		},
	};

	/*
	 * If everything went ok (i.e. 1 msg transmitted),
	 * return #bytes transmitted, else error code.
	 */
	if (g_I2cClient != NULL) {
		while (nLeft > 0) {
			szWriteBuf[0] = 0x53;
			szWriteBuf[1] = ((nAddr + nOffset) >> 8) & 0xFF;
			szWriteBuf[2] = (nAddr + nOffset) & 0xFF;

			msgs[1].buf = &pBuf[nOffset];

			if (nLeft > nMaxI2cLengthLimit) {
				msgs[1].len = nMaxI2cLengthLimit;
				nLeft -= nMaxI2cLengthLimit;
				nOffset += nMaxI2cLengthLimit;
			} else {
				msgs[1].len = nLeft;
				nLeft = 0;
			}

			rc = i2c_transfer(g_I2cClient->adapter, &msgs[0], 2);
			if (rc == 2)
				nReadSize = nReadSize + msgs[1].len;
			else {
				PRINTF_ERR("IicSegmentReadDataBySmBus()->");
				PRINTF_ERR("i2c_transfer() error %d\n", rc);
				return rc;
			}
		}
	} else
		PRINTF_ERR("i2c client is NULL\n");

	return nReadSize;
}

void mstpMemSet(void *pDst, s8 nVal, u32 nSize)
{
	memset(pDst, nVal, nSize);
}

void mstpMemCopy(void *pDst, void *pSource, u32 nSize)
{
	memcpy(pDst, pSource, nSize);
}

void mstpDelay(u32 nTime)
{
	mdelay(nTime);
}
