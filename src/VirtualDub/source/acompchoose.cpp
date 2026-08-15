// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2016-2018 Anton Shekhovtsov
// Copyright (C) 2024-2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>

#include "resource.h"
#include <vd2/system/list.h>
#include <vd2/system/protscope.h>
#include <vd2/Riza/audiocodec.h>
#include "gui.h"
#include "AVIOutputPlugin.h"
#include <vd2/plugin/vdinputdriver.h>

///////////////////////////////////////////////////////////////////////////

extern HINSTANCE g_hInst;

///////////////////////////////////////////////////////////////////////////

void CopyWaveFormat(WAVEFORMATEX *pDst, const WAVEFORMATEX *pSrc) {
	if (pSrc->wFormatTag == WAVE_FORMAT_PCM)
		memcpy(pDst, pSrc, sizeof(PCMWAVEFORMAT));
	else
		memcpy(pDst, pSrc, sizeof(WAVEFORMATEX) + pSrc->cbSize);
}

///////////////////////////////////////////////////////////////////////////

class ACMTagEntry;

class ACMFormatEntry : public ListNode2<ACMFormatEntry> {
public:
	ACMFORMATDETAILSW afd;
	ACMTagEntry *pFormatTag;
	WAVEFORMATEX *pwfex;
	bool fCompatible;

	~ACMFormatEntry();
};

ACMFormatEntry::~ACMFormatEntry() {
	freemem(pwfex);
}

class ACMTagEntry {
public:
	List2<ACMFormatEntry> formats;
	ACMFORMATTAGDETAILSW aftd;
	ACMDRIVERDETAILSW add;
	HACMDRIVERID hadid;
	IVDAudioEnc* driver;
	bool mbSupportsAbout;
	bool mbSupportsConfigure;

	ACMTagEntry();
	~ACMTagEntry();
	void clearFormats();
};

ACMTagEntry::ACMTagEntry() {
	memset(&add, 0, sizeof add);
	add.cbStruct = sizeof add;
	driver = 0;
}

ACMTagEntry::~ACMTagEntry() {
	clearFormats();
}

void ACMTagEntry::clearFormats() {
	ACMFormatEntry *pafe;

	while(pafe = formats.RemoveHead())
		delete pafe;
}

///////////////////////////////////////////////////////////////////////////

struct ACMEnumeratorData {
	HWND hwndDriverList;
	ACMTagEntry *pate;
	HACMDRIVER had;
	WAVEFORMATEX *pwfex, *pwfexSelect, *pwfexSrc;
	DWORD cbwfex;
	ACMFormatEntry *pFormatSelect;
	ACMTagEntry *pTagSelect;
	const wchar_t* pHintSelect;
	bool fAttemptedWeird;

	bool mbCurrentFormatTagMatchesHint;
	bool mbCurrentSelectedFormatMatchesHint;

	ACMEnumeratorData()
		: mbCurrentSelectedFormatMatchesHint(false)
	{
	}
};

struct ACMChooserData {
	WAVEFORMATEX *pwfex, *pwfexSrc;
	VDStringW* pHint;
	vdblock<uint8> *pConfig;
	bool enable_plugin;
};

///////////////////////////////////////////////////////////////////////////

BOOL CALLBACK ACMFormatEnumerator(HACMDRIVERID hadid, LPACMFORMATDETAILSW pafd, DWORD_PTR dwInstance, DWORD fdwSupport) {
	ACMEnumeratorData *pData = (ACMEnumeratorData *)dwInstance;
	ACMFormatEntry *pafe = new ACMFormatEntry();

	if (!pafe) return TRUE;

	if (!(pafe->pwfex = (WAVEFORMATEX *)allocmem(sizeof(WAVEFORMATEX) + pafd->pwfx->cbSize))) {
		delete pafe;
		return TRUE;
	}

	pafe->afd = *pafd;

	memcpy(pafe->pwfex, pafd->pwfx, sizeof(WAVEFORMATEX) + pafd->pwfx->cbSize);

	if (!pData->pTagSelect && pData->pwfexSelect && pafd->pwfx->wFormatTag == pData->pwfexSelect->wFormatTag) {

		if (pafd->pwfx->wFormatTag == WAVE_FORMAT_PCM ||
			(pafd->pwfx->cbSize == pData->pwfexSelect->cbSize && !memcmp(pafd->pwfx, pData->pwfexSelect, sizeof(WAVEFORMATEX)+pafd->pwfx->cbSize))) {

			// Mark this one as selected only if no format has been found yet or
			// this one is a better match (matches hint).
			if (pData->mbCurrentFormatTagMatchesHint || !pData->mbCurrentSelectedFormatMatchesHint) {
				pData->pTagSelect = pData->pate;
				pData->pFormatSelect = pafe;
			}
		}
	}

	pafe->fCompatible = true;

	if (pData->pwfexSrc) {
		pafe->fCompatible = !acmStreamOpen(NULL, pData->had, pData->pwfexSrc, pafd->pwfx, NULL, 0, 0, ACM_STREAMOPENF_QUERY)
						|| !acmStreamOpen(NULL, pData->had, pData->pwfexSrc, pafd->pwfx, NULL, 0, 0, ACM_STREAMOPENF_QUERY | ACM_STREAMOPENF_NONREALTIME);

		if (pafd->pwfx->nChannels == pData->pwfexSrc->nChannels
			&& pafd->pwfx->wBitsPerSample == pData->pwfexSrc->wBitsPerSample
			&& pafd->pwfx->nSamplesPerSec == pData->pwfexSrc->nSamplesPerSec)
			pData->fAttemptedWeird = true;
	}

	pData->pate->formats.AddTail(pafe);
	pafe->pFormatTag = pData->pate;

	return TRUE;
}

BOOL /*ACMFORMATTAGENUMCB*/ CALLBACK ACMFormatTagEnumerator(HACMDRIVERID hadid, LPACMFORMATTAGDETAILSW paftd, DWORD_PTR dwInstance, DWORD fdwSupport) {
	ACMEnumeratorData *pData = (ACMEnumeratorData *)dwInstance;

	if (paftd->dwFormatTag != WAVE_FORMAT_PCM) {
		int index;

		index = guiListboxInsertSortedString(pData->hwndDriverList, paftd->szFormatTag);

		if (index != LB_ERR) {
			ACMTagEntry *pate = new ACMTagEntry();
			ACMFORMATDETAILSW afd;

			pate->hadid = hadid;
			pate->aftd = *paftd;

			pate->mbSupportsAbout = (0 == acmDriverMessage(pData->had, ACMDM_DRIVER_ABOUT, -1L, 0));
			pate->mbSupportsConfigure = (0 != acmDriverMessage(pData->had, DRV_QUERYCONFIGURE, 0, 0));

			pData->pate = pate;
			pData->mbCurrentFormatTagMatchesHint = false;

			if (acmDriverDetailsW(hadid, &pate->add, 0)) {
				pate->add.cbStruct = 0;
			} else {
				if (pData->pHintSelect && !_wcsicmp(pData->pHintSelect, pate->add.szShortName)) {
					pData->mbCurrentFormatTagMatchesHint = true;
				}
			}

			memset(&afd, 0, sizeof afd);
			afd.cbStruct = sizeof(ACMFORMATDETAILS);
			afd.pwfx = pData->pwfex;
			afd.cbwfx = pData->cbwfex;
			afd.dwFormatTag = paftd->dwFormatTag;
			pData->pwfex->wFormatTag = (WORD)paftd->dwFormatTag;

			pData->fAttemptedWeird = false;
			acmFormatEnumW(pData->had, &afd, ACMFormatEnumerator, dwInstance, ACM_FORMATENUMF_WFORMATTAG);

			if (!pData->fAttemptedWeird && pData->pwfexSrc) {

				CopyWaveFormat(pData->pwfex, pData->pwfexSrc);

				pData->pwfex->wFormatTag = (WORD)paftd->dwFormatTag;

				if (!acmFormatSuggest(pData->had, pData->pwfexSrc, pData->pwfex, pData->cbwfex, ACM_FORMATSUGGESTF_NCHANNELS|ACM_FORMATSUGGESTF_NSAMPLESPERSEC|ACM_FORMATSUGGESTF_WFORMATTAG)) {
					afd.dwFormatIndex = 0;
					afd.fdwSupport = 0;

					if (!acmFormatDetailsW(pData->had, &afd, ACM_FORMATDETAILSF_FORMAT))
						ACMFormatEnumerator(hadid, &afd, dwInstance, 0);
				}
			}

			SendMessageW(pData->hwndDriverList, LB_SETITEMDATA, index, (LPARAM)pate);
		}
	}

	return TRUE;
}

BOOL /*ACMDRIVERENUMCB*/ CALLBACK ACMDriverEnumerator(HACMDRIVERID hadid, DWORD_PTR dwInstance, DWORD fdwSupport) {
	ACMEnumeratorData *pData = (ACMEnumeratorData *)dwInstance;

	vdprotected1("enumerating audio codec ID %p", decltype(hadid), hadid) {
		if (!acmDriverOpen(&pData->had, hadid, 0)) {
			ACMDRIVERDETAILSW add = { sizeof(ACMDRIVERDETAILSW) };
			acmDriverDetailsW(hadid, &add, 0);

			vdprotected1("enumerating formats for audio codec \"%.64ls\"", const wchar_t*, add.szLongName) {
				ACMFORMATTAGDETAILSW aftd;

				memset(&aftd, 0, sizeof aftd);
				aftd.cbStruct = sizeof aftd;

				acmFormatTagEnumW(pData->had, &aftd, ACMFormatTagEnumerator, dwInstance, 0);

				acmDriverClose(pData->had, 0);
			}
		}
	}

	return TRUE;
}

static void AudioChooseDisplaySpecs(HWND hdlg, WAVEFORMATEX *pwfex) {
	char buf[128];
	int blps;

	if (pwfex) {
		if (is_audio_pcm((VDWaveFormat*)pwfex)) {
			strcpy(buf, "PCM");
		}
		else if (is_audio_float((VDWaveFormat*)pwfex)) {
			strcpy(buf, "PCM float");
		}
		else {
			wsprintfA(buf, "0x%04x", pwfex->wFormatTag);
		}
	} else {
		buf[0] = 0;
	}
	SetDlgItemTextA(hdlg, IDC_STATIC_FORMATID, buf);

	if (pwfex) {
		wsprintfA(buf, "%ld bytes", pwfex->nBlockAlign);
	}
	SetDlgItemTextA(hdlg, IDC_STATIC_BYTESPERBLOCK, buf);

	if (pwfex) {
		wsprintfA(buf, "%lu bytes/sec", pwfex->nAvgBytesPerSec);
	}
	SetDlgItemTextA(hdlg, IDC_STATIC_DATARATE, buf);

	if (pwfex) {
		if (pwfex->nAvgBytesPerSec && pwfex->nBlockAlign) {
			blps = MulDiv(pwfex->nAvgBytesPerSec, 10, pwfex->nBlockAlign);
			wsprintfA(buf, "%ld.%c blocks/sec", blps/10, (blps%10)+'0');
		} else {
			strcpy(buf, "");
		}
	}
	SetDlgItemTextA(hdlg, IDC_STATIC_GRANULARITY, buf);
}

static void AudioChooseShowFormats(HWND hdlg, ACMTagEntry *pTag, bool fShowCompatibleOnly) {
	ACMChooserData* thisPtr = (ACMChooserData*)GetWindowLongPtrW(hdlg, DWLP_USER);
	HWND hwndListFormats = GetDlgItem(hdlg, IDC_FORMAT);
	int idx;

	SendMessageW(hwndListFormats, LB_RESETCONTENT, 0, 0);

	if (!pTag) {
		HWND hwndItem = GetDlgItem(hdlg, IDC_CONFIGURE);
		if (hwndItem)
			EnableWindow(hwndItem, false);

		if (hwndItem = GetDlgItem(hdlg, IDC_ABOUT))
			EnableWindow(hwndItem, false);

		AudioChooseDisplaySpecs(hdlg, thisPtr->pwfexSrc);
		return;
	}

	HWND hwndItem = GetDlgItem(hdlg, IDC_CONFIGURE);
	if (hwndItem)
		EnableWindow(hwndItem, pTag->mbSupportsConfigure);

	if (hwndItem = GetDlgItem(hdlg, IDC_ABOUT))
		EnableWindow(hwndItem, pTag->mbSupportsAbout);

	AudioChooseDisplaySpecs(hdlg, NULL);

	ACMFormatEntry *pFormat = pTag->formats.AtHead(), *pFormat_next;

	while(pFormat_next = pFormat->NextFromHead()) {
		wchar_t buf[128];
		int band;

		if (!fShowCompatibleOnly || pFormat->fCompatible) {
			if (pFormat->pwfex->nAvgBytesPerSec) {
				band = (pFormat->pwfex->nAvgBytesPerSec+1023)/1024;
				wsprintfW(buf, L"%s\t%dKB/s", pFormat->afd.szFormat, band);
			} else {
				wcscpy(buf, pFormat->afd.szFormat);
			}

			idx = SendMessageW(hwndListFormats, LB_ADDSTRING, 0, (LPARAM)buf);
			if (idx != LB_ERR) {
				SendMessageW(hwndListFormats, LB_SETITEMDATA, idx, (LPARAM)pFormat);
			}
		}

		pFormat = pFormat_next;
	}
}

void PluginReloadFormat(IVDXAudioEnc* plugin, ACMChooserData* thisPtr, ACMTagEntry* entry) {
	plugin->SetInputFormat((VDXWAVEFORMATEX*)thisPtr->pwfexSrc);

	int dst_format_len = plugin->GetOutputFormatSize();
	if (dst_format_len) {
		ACMFormatEntry *f1 = new ACMFormatEntry();
		f1->pFormatTag = entry;
		f1->fCompatible = true;
		memset(&f1->afd,0,sizeof(f1->afd));

		f1->pwfex = (WAVEFORMATEX *)allocmem(dst_format_len);
		memcpy(f1->pwfex, plugin->GetOutputFormat(), dst_format_len);

		if (f1->pwfex->nChannels == 1) {
			wsprintfW(f1->afd.szFormat, L"%u Hz, Mono", f1->pwfex->nSamplesPerSec);
		}
		if (f1->pwfex->nChannels == 2) {
			wsprintfW(f1->afd.szFormat, L"%u Hz, Stereo", f1->pwfex->nSamplesPerSec);
		}
		if (f1->pwfex->nChannels > 2) {
			wsprintfW(f1->afd.szFormat, L"%u Hz, %d ch", f1->pwfex->nSamplesPerSec, f1->pwfex->nChannels);
		}

		entry->formats.AddTail(f1);
	}

	plugin->SetInputFormat(0);
}

static INT_PTR CALLBACK AudioChooseCompressionDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	ACMChooserData* thisPtr = (ACMChooserData*)GetWindowLongPtrW(hdlg, DWLP_USER);

	switch(msg) {
	case WM_INITDIALOG:
		{
			ACMEnumeratorData aed;
			INT tabs[1];

			thisPtr = (ACMChooserData *)lParam;
			SetWindowLongPtrW(hdlg, DWLP_USER, lParam);

			tabs[0] = 140;

			SendDlgItemMessageW(hdlg, IDC_FORMAT, LB_SETTABSTOPS, 1, (LPARAM)tabs);

			acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, &aed.cbwfex);

			aed.pwfex = (WAVEFORMATEX *)allocmem(aed.cbwfex);
			aed.pwfexSelect = thisPtr->pwfex;
			aed.pwfexSrc = thisPtr->pwfexSrc;
			aed.pTagSelect = NULL;
			aed.pFormatSelect = NULL;
			aed.pHintSelect = thisPtr->pHint->empty() ? NULL : thisPtr->pHint->c_str();

			if (!aed.pwfex) {
				EndDialog(hdlg, 0);
				return FALSE;
			}

			aed.hwndDriverList = GetDlgItem(hdlg, IDC_FORMATTAG);

			acmDriverEnum(ACMDriverEnumerator, (DWORD_PTR)&aed, ACM_DRIVERENUMF_NOLOCAL);

			freemem(aed.pwfex);

			tVDAudioEncList drivers;
			VDGetAudioEncList(drivers);
			if(thisPtr->enable_plugin){for(int i=0; i<drivers.size(); i++){
				IVDAudioEnc *driver = drivers[i];
				const wchar_t* name = driver->GetName();
				int idx = SendDlgItemMessageW(hdlg, IDC_FORMATTAG, LB_INSERTSTRING, i, (LPARAM)name);
				if (idx >= 0) {
					ACMTagEntry* entry = new ACMTagEntry;
					memset(&entry->add,0,sizeof(entry->add));
					memset(&entry->aftd,0,sizeof(entry->aftd));
					entry->hadid = 0;
					entry->driver = driver;
					entry->mbSupportsAbout = driver->GetDriver()->HasAbout();
					entry->mbSupportsConfigure = driver->GetDriver()->HasConfig();
					SendDlgItemMessageW(hdlg, IDC_FORMATTAG, LB_SETITEMDATA, idx, (LPARAM)entry);

					IVDXAudioEnc* plugin = driver->GetDriver();
					if (*thisPtr->pHint==driver->GetSignatureName()) {
						plugin->SetConfig(thisPtr->pConfig->data(),thisPtr->pConfig->size());
					}
					PluginReloadFormat(plugin,thisPtr,entry);
					if (*thisPtr->pHint==driver->GetSignatureName() && !entry->formats.IsEmpty()) {
						aed.pTagSelect = entry;
						aed.pFormatSelect = entry->formats.begin();
					}
				}
			}}

			// This has to go last, because some version of DivX audio come up
			// with a blank name. #*$&@*#$^)&@*#^@$

			int idx = SendDlgItemMessageA(hdlg, IDC_FORMATTAG, LB_INSERTSTRING, 0, (LPARAM)"<No compression (PCM)>");

			if (idx >= 0) {
				SendDlgItemMessageW(hdlg, IDC_FORMATTAG, LB_SETITEMDATA, idx, NULL);
			}


			if (!aed.pTagSelect) {
				SendMessageW(aed.hwndDriverList, LB_SETCURSEL, 0, 0);
				SendMessageW(hdlg, WM_COMMAND, LBN_SELCHANGE<<16, (LPARAM)aed.hwndDriverList);
			} else {
				int cnt, i;
				HWND hwndItem;
				ACMTagEntry *pTag;
				ACMFormatEntry *pFormat;

				hwndItem = GetDlgItem(hdlg, IDC_FORMATTAG);
				cnt = SendMessageW(hwndItem, LB_GETCOUNT, 0, 0);

				for(i=0; i<cnt; i++) {
					pTag = (ACMTagEntry *)SendMessageW(hwndItem, LB_GETITEMDATA, i, 0);

					if (pTag == aed.pTagSelect) {
						SendMessageW(hwndItem, LB_SETCURSEL, i, 0);
						AudioChooseShowFormats(hdlg, pTag, !!thisPtr->pwfexSrc);

						hwndItem = GetDlgItem(hdlg, IDC_FORMAT);
						cnt = SendMessageW(hwndItem, LB_GETCOUNT, 0, 0);

						for(i=0; i<cnt; i++) {
							pFormat = (ACMFormatEntry *)SendMessageW(hwndItem, LB_GETITEMDATA, i, 0);

							if (pFormat == aed.pFormatSelect) {
								SendMessageW(hwndItem, LB_SETCURSEL, i, 0);
								SendMessageW(hdlg, WM_COMMAND, LBN_SELCHANGE<<16, (LPARAM)hwndItem);
								break;
							}
						}
						break;
					}
				}
			}

			EnableWindow(GetDlgItem(hdlg, IDC_SHOWALL), !!thisPtr->pwfexSrc);
		}
		return TRUE;

	case WM_DESTROY:
		{
			HWND hwndList = GetDlgItem(hdlg, IDC_FORMATTAG);
			int cnt = SendMessageW(hwndList, LB_GETCOUNT, 0, 0);
			int i;

			for(i=0; i<cnt; i++) {
				ACMTagEntry *pTag = (ACMTagEntry *)SendMessageW(hwndList, LB_GETITEMDATA, i, 0);

				delete pTag;
			}

		}
		return TRUE;

	case WM_CLOSE:
		EndDialog(hdlg, 0);
		return TRUE;

	case WM_COMMAND:
		switch(GetWindowLongW((HWND)lParam, GWL_ID)) {
		case IDOK:
			{
				int idx = SendDlgItemMessageW(hdlg, IDC_FORMAT, LB_GETCURSEL, 0, 0);
				thisPtr->pConfig->clear();

				if (idx < 0) {
					thisPtr->pwfex = NULL;
					thisPtr->pHint->clear();
				} else {
					ACMFormatEntry *pFormat = (ACMFormatEntry *)SendDlgItemMessageW(hdlg, IDC_FORMAT, LB_GETITEMDATA, idx, 0);

					thisPtr->pwfex = pFormat->pwfex;
					pFormat->pwfex = NULL;

					if (pFormat->pFormatTag->driver) {
						thisPtr->pHint->assign(pFormat->pFormatTag->driver->GetSignatureName());
						IVDXAudioEnc* driver = pFormat->pFormatTag->driver->GetDriver();
						size_t config_size = driver->GetConfigSize();
						void* config_data = driver->GetConfig();
						thisPtr->pConfig->resize(config_size);
						memcpy(thisPtr->pConfig->data(),config_data,config_size);
					} else if (pFormat->pFormatTag->add.cbStruct > 0)
						thisPtr->pHint->assign(pFormat->pFormatTag->add.szShortName);
					else
						thisPtr->pHint->clear();
				}
			}
			EndDialog(hdlg, 1);
			return TRUE;
		case IDCANCEL:
			EndDialog(hdlg, 0);
			return TRUE;

		case IDC_FORMATTAG:
			switch(HIWORD(wParam)) {
			case LBN_SELCHANGE:
redisplay_formats:
				{
					int idx = SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);

					if (idx < 0) {
						AudioChooseShowFormats(hdlg, NULL, false);
						return TRUE;
					}

					ACMTagEntry *pTag = (ACMTagEntry *)SendMessageW((HWND)lParam, LB_GETITEMDATA, idx, 0);

					AudioChooseShowFormats(hdlg, pTag, !IsDlgButtonChecked(hdlg, IDC_SHOWALL));
					HWND hwndItem = GetDlgItem(hdlg, IDC_FORMAT);
					int cnt = SendMessageW(hwndItem, LB_GETCOUNT, 0, 0);
					if (cnt>0) {
						SendMessageW(hwndItem, LB_SETCURSEL, 0, 0);
						SendMessageW(hdlg, WM_COMMAND, LBN_SELCHANGE<<16, (LPARAM)hwndItem);
					}

				}
				return TRUE;
			}
			break;
		case IDC_FORMAT:
			switch(HIWORD(wParam)) {
			case LBN_SELCHANGE:
				{
					int idx = SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);

					if (idx < 0) {
						if (!SendDlgItemMessageW(hdlg, IDC_FORMATTAG, LB_GETCURSEL, 0, 0)) {
							AudioChooseDisplaySpecs(hdlg, thisPtr->pwfexSrc);
						} else {
							AudioChooseDisplaySpecs(hdlg, NULL);
						}
						return TRUE;
					}

					ACMFormatEntry *pFormat = (ACMFormatEntry *)SendMessageW((HWND)lParam, LB_GETITEMDATA, idx, 0);

					AudioChooseDisplaySpecs(hdlg, pFormat->pwfex);
				}
				return TRUE;
			}
			break;
		case IDC_SHOWALL:

			// Yeah, yeah...

			if (HIWORD(wParam) == BN_CLICKED) {
				lParam = (LPARAM)GetDlgItem(hdlg, IDC_FORMATTAG);
				goto redisplay_formats;
			}

			break;

		case IDC_CONFIGURE:
			{
				HWND hwndItem = GetDlgItem(hdlg, IDC_FORMATTAG);
				int idx = SendMessageW(hwndItem, LB_GETCURSEL, 0, 0);

				if (idx < 0)
					return TRUE;

				ACMTagEntry *pTag = (ACMTagEntry *)SendMessageW(hwndItem, LB_GETITEMDATA, idx, 0);

				if (pTag && pTag->mbSupportsConfigure) {
					if (pTag->driver) {
						pTag->driver->GetDriver()->ShowConfig((VDXHWND)hdlg);
						pTag->clearFormats();
						PluginReloadFormat(pTag->driver->GetDriver(),thisPtr,pTag);
						ACMFormatEntry* pFormatSelect = 0;
						if (!pTag->formats.IsEmpty()) {
							pFormatSelect = pTag->formats.begin();
						}
						AudioChooseShowFormats(hdlg, pTag, !!thisPtr->pwfexSrc);

						hwndItem = GetDlgItem(hdlg, IDC_FORMAT);
						int cnt = SendMessageW(hwndItem, LB_GETCOUNT, 0, 0);

						for(int i=0; i<cnt; i++) {
							ACMFormatEntry* pFormat = (ACMFormatEntry *)SendMessageW(hwndItem, LB_GETITEMDATA, i, 0);

							if (pFormat == pFormatSelect) {
								SendMessageW(hwndItem, LB_SETCURSEL, i, 0);
								SendMessageW(hdlg, WM_COMMAND, LBN_SELCHANGE<<16, (LPARAM)hwndItem);
								break;
							}
						}
					}
					else {
						HACMDRIVER hDriver;
						if (!acmDriverOpen(&hDriver, pTag->hadid, 0)) {
							acmDriverMessage(hDriver, DRV_CONFIGURE, (LPARAM)hdlg, NULL);
							acmDriverClose(hDriver, 0);
						}
					}
				}
			}
			break;

		case IDC_ABOUT:
			{
				HWND hwndItem = GetDlgItem(hdlg, IDC_FORMATTAG);
				int idx = SendMessageW(hwndItem, LB_GETCURSEL, 0, 0);

				if (idx < 0)
					return TRUE;

				ACMTagEntry *pTag = (ACMTagEntry *)SendMessageW(hwndItem, LB_GETITEMDATA, idx, 0);

				HACMDRIVER hDriver;
				if (pTag && pTag->mbSupportsAbout && !acmDriverOpen(&hDriver, pTag->hadid, 0)) {
					acmDriverMessage(hDriver, ACMDM_DRIVER_ABOUT, (LPARAM)hdlg, NULL);

					acmDriverClose(hDriver, 0);
				}
			}
			break;

		}
		return TRUE;
	}

	return FALSE;
}

WAVEFORMATEX *AudioChooseCompressor(HWND hwndParent, WAVEFORMATEX *pwfexOld, WAVEFORMATEX *pwfexSrc, VDStringW& shortNameHint, vdblock<uint8>& config, bool enable_plugin) {
	ACMChooserData data;

	data.pwfex = pwfexOld;
	data.pwfexSrc = pwfexSrc;
	data.pHint = &shortNameHint;
	data.pConfig = &config;
	data.enable_plugin = enable_plugin;

	if (!DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_AUDIOCOMPRESSION), hwndParent, AudioChooseCompressionDlgProc, (LPARAM)&data)) {
		return pwfexOld;
	}
	else {
		if (pwfexOld) {
			freemem(pwfexOld);
		}
		return data.pwfex;
	}
}
