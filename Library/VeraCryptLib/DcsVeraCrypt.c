/** @file
Interface for DCS

Copyright (c) 2016. Disk Cryptography Services for EFI (DCS), Alex Kolotnikov
Copyright (c) 2016. VeraCrypt, Mounir IDRASSI

This program and the accompanying materials
are licensed and made available under the terms and conditions
of the Apache License, Version 2.0.  

The full text of the license may be found at
https://opensource.org/licenses/Apache-2.0
**/

#include <DcsVeraCrypt.h>
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Library/CommonLib.h>
#include <Library/GraphLib.h>
#include <Library/PasswordLib.h>
#include <Library/DcsCfgLib.h>

#include <common/Password.h>
#include "common/Crypto.h"
#include "common/Xml.h"
#include "common/Crc.h"
#include "BootCommon.h"
#include "Library/DcsTpmLib.h"

//////////////////////////////////////////////////////////////////////////
// Config
//////////////////////////////////////////////////////////////////////////
char *gConfigBuffer = NULL;
UINTN	gConfigBufferSize = 0;

BOOL ConfigRead(char *configKey, char *configValue, int maxValueSize)
{
	char *xml;

	if (gConfigBuffer == NULL) {
		if (FileLoad(NULL, L"\\EFI\\VeraCrypt\\DcsProp", &gConfigBuffer, &gConfigBufferSize) != EFI_SUCCESS) {
			return FALSE;
		}
	}

	xml = gConfigBuffer;
	if (xml != NULL)
	{
		xml = XmlFindElementByAttributeValue(xml, "config", "key", configKey);
		if (xml != NULL)
		{
			XmlGetNodeText(xml, configValue, maxValueSize);
			return TRUE;
		}
	}

	return FALSE;
}

int ConfigReadInt(char *configKey, int defaultValue)
{
	char s[32];
	if (ConfigRead(configKey, s, sizeof(s))) {
		if (*s == '-') {
			return (-1) * (int)AsciiStrDecimalToUintn(&s[1]);
		}
		return (int)AsciiStrDecimalToUintn(s);
	}
	else
		return defaultValue;
}


char *ConfigReadString(char *configKey, char *defaultValue, char *str, int maxLen)
{
	if (!ConfigRead(configKey, str, maxLen)) {
		AsciiStrCpyS(str, maxLen, defaultValue);
	}
	return str;
}


///////////////////////////////////////////////////////////////////////////
// Globals
//////////////////////////////////////////////////////////////////////////
#define MAX_MSG 256
int gAuthPasswordType = 0;
char* gAuthPasswordMsg = NULL;
Password gAuthPassword;

char* gAuthPimMsg = NULL;
int gAuthPimRqt = 1;
int gAuthPim = 0;

int gAuthTcRqt = 0;
int gAuthTc = 0;

char *gAuthHashMsg = NULL;
int gAuthHashRqt = 1;
int gAuthHash = 0;

int gAuthBootRqt = 0;
int gAuthBoot = 1;

int gAuthRetry = 10;
int gAuthPwdCode = 1;

INT32 gRUD = 0;

int gAuthSecRegionSearch = 0;

CHAR8* gPlatformKeyFile = NULL;
UINTN gPlatformKeyFileSize = 0;

EFI_GUID *gPartitionGuidOS = NULL;

int gDcsBootForce = 1;

CHAR8* gOnExitFailed = NULL;
CHAR8* gOnExitSuccess = NULL;
CHAR8* gOnExitNotFound = NULL;

//////////////////////////////////////////////////////////////////////////
// Authorize
/////////////////////////////////////////////////////////////////////////

VOID
VCAuthLoadConfig() 
{
	int tmp;

	if (gAuthPasswordMsg != NULL) return; // Already loaded

	SetMem(&gAuthPassword, sizeof(gAuthPassword), 0);
	{
		char* passwordPictureAscii = NULL;
		passwordPictureAscii = MEM_ALLOC(MAX_MSG);
		gPasswordPictureFileName = MEM_ALLOC(MAX_MSG * 2);
		ConfigReadString("PasswordPicture", "\\EFI\\VeraCrypt\\login.bmp", passwordPictureAscii, MAX_MSG);
		AsciiStrToUnicodeStr(passwordPictureAscii, gPasswordPictureFileName);
		MEM_FREE(passwordPictureAscii);
	}
	SetMem(&gAuthPassword, sizeof(gAuthPassword), 0);

	// 
	gAuthSecRegionSearch = ConfigReadInt("SecRegionSearch", 0);
	gPlatformLocked = ConfigReadInt("PlatformLocked", 0);
	gTPMLocked = ConfigReadInt("TPMLocked", 0);
	gSCLocked = ConfigReadInt("SCLocked", 0);
	gDcsBootForce = ConfigReadInt("DcsBootForce", 1);

	// Actions for DcsInt
	gOnExitSuccess = MEM_ALLOC(MAX_MSG);
	ConfigReadString("ActionSuccess", "Exit", gOnExitSuccess, MAX_MSG);
	gOnExitNotFound = MEM_ALLOC(MAX_MSG);
	ConfigReadString("ActionNotFound", "Exit", gOnExitNotFound, MAX_MSG);
	gOnExitFailed = MEM_ALLOC(MAX_MSG);
	ConfigReadString("ActionFailed", "Exit", gOnExitFailed, MAX_MSG);

	{
		char* strTemp = NULL;
		strTemp = MEM_ALLOC(MAX_MSG);
		ConfigReadString("PartitionGuidOS", "", strTemp, MAX_MSG);
		if (strTemp[0] != 0) {
			EFI_GUID g;
			if (AsciiStrToGuid(&g, strTemp)) {
				gPartitionGuidOS = MEM_ALLOC(sizeof(EFI_GUID));
				if (gPartitionGuidOS != NULL) {
					memcpy(gPartitionGuidOS, &g, sizeof(g));
				}
			}
		}
		MEM_FREE(strTemp);
	}

	gPasswordPictureChars = MEM_ALLOC(MAX_MSG);
	ConfigReadString("PictureChars", gPasswordPictureCharsDefault, gPasswordPictureChars, MAX_MSG);
	gPasswordPictureCharsLen = strlen(gPasswordPictureChars);

	gAuthPasswordType = ConfigReadInt("PasswordType", 0);
	gAuthPasswordMsg = MEM_ALLOC(MAX_MSG);
	ConfigReadString("PasswordMsg", "Password:", gAuthPasswordMsg, MAX_MSG);

	gAuthPimMsg = MEM_ALLOC(MAX_MSG);
	gAuthPimRqt = ConfigReadInt("PimRqt", 1);
	gAuthPim = ConfigReadInt("Pim", 0);
	ConfigReadString("PimMsg", "Pim:", gAuthPimMsg, MAX_MSG);

	gAuthHashMsg = MEM_ALLOC(MAX_MSG);
	gAuthHashRqt = ConfigReadInt("HashRqt", 1);
	gAuthHash = ConfigReadInt("Hash", 0);
	ConfigReadString("HashMsg", "(0) TEST ALL (1) SHA512 (2) WHIRLPOOL (3) SHA256 (4) RIPEMD160\n\rHash:", gAuthHashMsg, MAX_MSG);

	gPasswordVisible = (UINT8)ConfigReadInt("AuthorizeVisible", 0);
	gPasswordShowMark = ConfigReadInt("AuthorizeMarkTouch", 1);
	gAuthBootRqt = ConfigReadInt("BootRqt", 0);
	gAuthTcRqt = ConfigReadInt("TcRqt", 0);
	gRUD = ConfigReadInt("RUD", 0);
	gAuthRetry = ConfigReadInt("AuthorizeRetry", 10);

	// touch
	tmp = ConfigReadInt("TouchDevice", -1);
	if (tmp == -1) InitTouch();
	if (tmp >= 0) {
		if (gTouchCount == 0) InitTouch();
		if (tmp < (int)gTouchCount) {
			TouchGetIO(gTouchHandles[tmp], &gTouchPointer);
		}
	}
	gTouchSimulate = ConfigReadInt("TouchSimulate", 0);

	// Graph
	tmp = ConfigReadInt("GraphDevice", -1);
	if (tmp == -1) InitGraph();
	if (tmp >= 0) {
		if (gGraphCount == 0) InitGraph();
		if (tmp < (int)gGraphCount) {
			GraphGetIO(gGraphHandles[tmp], &gGraphOut);
		}
	}
	if (gGraphOut != NULL) {
		tmp = ConfigReadInt("GraphMode", -1);
		if (tmp >= 0 && tmp <= (int)gGraphOut->Mode->MaxMode) {
			gGraphOut->SetMode(gGraphOut, tmp);
		}
	}

	// Beep
	gBeepEnabled = ConfigReadInt("Beep", 0);
	if (gBeepEnabled) {
		gBeepNumberDefault = ConfigReadInt("BeepNumber", 1);
		gBeepDurationDefault = ConfigReadInt("BeepDuration", 100);
		gBeepIntervalDefault = ConfigReadInt("BeepInterval", 0);
		gBeepToneDefault = ConfigReadInt("BeepTone", 0x500);
		gBeepControlEnabled = ConfigReadInt("BeepControl", 1) != 0;

		tmp = ConfigReadInt("BeepDevice", -1);
		if (tmp == -1) InitSpeaker();
		if (tmp >= 0) {
			if (gSpeakerCount == 0) InitSpeaker();
			if (tmp < (int)gSpeakerCount) {
				SpeakerSelect(tmp);
			}
		}
	}

}

//////////////////////////////////////////////////////////////////////////
// Configuration menu
//////////////////////////////////////////////////////////////////////////
PMENU_ITEM          gCfgMenu = NULL;
BOOLEAN             gCfgMenuContinue = TRUE;

EFI_STATUS
ActionCfgReboot(IN VOID *ctx) {
	gST->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
	return EFI_DEVICE_ERROR;
}

EFI_STATUS
ActionCfgTpm(IN VOID *ctx) {
	return gTpm->Configure(gTpm);
}

EFI_STATUS
ActionBoot(IN VOID *ctx) {
	gCfgMenuContinue = FALSE;
	gAuthPwdCode = AskPwdRetCancel;
	return EFI_SUCCESS;
}

EFI_STATUS
ActionNewPassword(IN VOID *ctx) {
	gCfgMenuContinue = FALSE;
	gAuthPwdCode = AskPwdRetLogin;
	return EFI_SUCCESS;
}

VOID 
CfgMenuCreate() {
	PMENU_ITEM          item = NULL;
	item = DcsMenuAppend(item, L"Boot", 'b', ActionBoot, NULL);
	gCfgMenu = item;
	item = DcsMenuAppend(item, L"Hard reset", 'r', ActionCfgReboot, NULL);
	item = DcsMenuAppend(item, L"New password", 'n', ActionNewPassword, NULL);
	if (gTpm != NULL) {
		item = DcsMenuAppend(item, L"Configure TPM", 't', ActionCfgTpm, NULL);
	}
}


VOID
VCAskPwd(
	IN	 UINTN	pwdType,
	OUT Password* vcPwd) {
	BOOL pwdReady;
	if (gAuthPasswordMsg == NULL) VCAuthLoadConfig();
	do {
		pwdReady = TRUE;
		if (pwdType == AskPwdNew) {
			EFI_INPUT_KEY key;
			key = KeyWait(L"Press 'c' to configure, others to skip %1d\r", 9, 0, 0);
			if (key.UnicodeChar == 'c') {
				PMENU_ITEM          item = NULL;
				EFI_STATUS          res;
				OUT_PRINT(L"\n%V%a %a configuration%N\n", TC_APP_NAME, VERSION_STRING);
				if (gCfgMenu == NULL) CfgMenuCreate();
				do {
					DcsMenuPrint(gCfgMenu);
					item = NULL;
					key.UnicodeChar = 0;
					while (item == NULL) {
						item = gCfgMenu;
						key = GetKey();
						while (item != NULL) {
							if (item->Select == key.UnicodeChar) break;
							item = item->Next;
						}
					}
					OUT_PRINT(L"%c\n", key.UnicodeChar);
					res = item->Action(item->Context);
					if (EFI_ERROR(res)) {
						ERR_PRINT(L"%r\n", res);
					}
				} while (gCfgMenuContinue);
				if (gAuthPwdCode == AskPwdRetCancel) {
					return;
				}
			}
		}

		if (gAuthPasswordType == 1 &&
			gGraphOut != NULL &&
			((gTouchPointer != NULL) || (gTouchSimulate != 0))) {
			AskPictPwdInt(pwdType, sizeof(vcPwd->Text), vcPwd->Text, &vcPwd->Length, &gAuthPwdCode);
		}
		else {
			switch (pwdType) {
			case AskPwdNew:
				OUT_PRINT(L"New password:");
				break;
			case AskPwdConfirm:
				OUT_PRINT(L"Confirm password:");
				break;
			case AskPwdLogin:
			default:
				OUT_PRINT(L"%a", gAuthPasswordMsg);
				break;
			}
			AskConsolePwdInt(&vcPwd->Length, vcPwd->Text, &gAuthPwdCode, sizeof(vcPwd->Text), gPasswordVisible);
		}

		if (gAuthPwdCode == AskPwdRetCancel) {
			return;
		}

		if (gSCLocked) {
			ERR_PRINT(L"Smart card is not configured\n");
		}

		if (gPlatformLocked) {
			if (gPlatformKeyFile == NULL) {
				ERR_PRINT(L"Platform key file is absent\n");
			}
			else {
				ApplyKeyFile(vcPwd, gPlatformKeyFile, gPlatformKeyFileSize);
			}
		}

		if (gTPMLocked) {
			if (gTpm != NULL) {
				pwdReady = !EFI_ERROR(gTpm->Apply(gTpm, vcPwd));
				if (!pwdReady) {
					ERR_PRINT(L"TPM error: DCS configuration ");
					if (!gTpm->IsConfigured(gTpm)) {
						ERR_PRINT(L"absent\n");
					}
					else {
						ERR_PRINT(L"locked\n");
					}
				}
			}	else {
				ERR_PRINT(L"No TPM found\n");
			}
		}
	} while (!pwdReady);
}

VOID
VCAuthAsk() 
{
	VCAskPwd(AskPwdLogin, &gAuthPassword);

	if (gAuthPwdCode == AskPwdRetCancel) {
		return;
	}

	if (gAuthPimRqt) {
		gAuthPim = AskInt(gAuthPimMsg, gPasswordVisible);
	}
	if (gAuthTcRqt) {
		gAuthTc = AskConfirm("True crypt mode [N]?", gPasswordVisible);
	}

	if (gAuthBootRqt) {
		gAuthBoot = AskConfirm("Boot mount mode [N]?", gPasswordVisible);
	}

	if (gAuthHashRqt) {
		do {
			gAuthHash = AskInt(gAuthHashMsg, gPasswordVisible);
		} while (gAuthHash < 0 || gAuthHash > 4);
	}
}


//////////////////////////////////////////////////////////////////////////
// VeraCrypt helpers
//////////////////////////////////////////////////////////////////////////
void* VeraCryptMemAlloc(IN UINTN size) {
   return MEM_ALLOC(size);
}

void VeraCryptMemFree(IN VOID* ptr) {
   MEM_FREE(ptr);
}
void ThrowFatalException(int line) {
   ERR_PRINT(L"Fatal %d\n", line);
}

//////////////////////////////////////////////////////////////////////////
// Random data
//////////////////////////////////////////////////////////////////////////
BOOL
RandgetBytes(unsigned char *buf, int len, BOOL forceSlowPoll) {
	EFI_STATUS res;
	res = RndGetBytes(buf, len);
	return !EFI_ERROR(res);
}

//////////////////////////////////////////////////////////////////////////
// Key file
//////////////////////////////////////////////////////////////////////////

#define KEYFILE_POOL_SIZE	64
#define	KEYFILE_MAX_READ_LEN	(1024*1024)

VOID
ApplyKeyFile(
	IN OUT Password* password,
	IN     CHAR8*    keyfileData,
	IN     UINTN     keyfileDataSize
	) 
{
	unsigned __int32 crc = 0xffffffff;
	int writePos = 0;
	size_t totalRead = 0;
	size_t i;
	CHAR8 keyPool[KEYFILE_POOL_SIZE];
	
	ZeroMem(keyPool, sizeof(keyPool));

	for (i = 0; i < keyfileDataSize; i++)
	{
		crc = UPDC32(keyfileData[i], crc);

		keyPool[writePos++] += (unsigned __int8)(crc >> 24);
		keyPool[writePos++] += (unsigned __int8)(crc >> 16);
		keyPool[writePos++] += (unsigned __int8)(crc >> 8);
		keyPool[writePos++] += (unsigned __int8)crc;

		if (writePos >= KEYFILE_POOL_SIZE)
			writePos = 0;

		if (++totalRead >= KEYFILE_MAX_READ_LEN)
			break;
	}

	for (i = 0; i < sizeof(keyPool); i++)
	{
		if (i < password->Length)
			password->Text[i] += keyPool[i];
		else
			password->Text[i] = keyPool[i];
	}

	if (password->Length < (int)sizeof(keyPool))
		password->Length = sizeof(keyPool);

	burn (keyPool, sizeof(keyPool));
}