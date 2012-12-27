/*
 * $Id$
 *
 * Copyright (C) 2012 Sergey "Exodus8" (rusguy6@gmail.com)
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#define YOUTUBE_URL			_T("youtube.com/watch?")
#define YOUTUBE_FULL_URL	_T("www.youtube.com/watch?v=")
#define YOUTU_BE_URL		_T("youtu.be/")
#define YOUTU_BE_FULL_URL	_T("www.youtu.be/")

#define MATCH_START			"url_encoded_fmt_stream_map"
#define MATCH_END			"\\u0026amp"

#define URL_DELIMETER		_T("url=")

static DWORD strpos(char* h, char* n)
{
	char* p = strstr(h, n);

	if (p) {
		return p - h;
	}

	return 0;
}

static void Explode(const CString str, const CString sep, CAtlList<CString>& arr)
{
	arr.RemoveAll();

	CString local(str);

	int pos = local.Find(sep);
	while (pos != -1) {
		CString sss = local.Left(pos);
		arr.AddTail(sss);
		local.Delete(0, pos + sep.GetLength());
		pos = local.Find(sep);
	}

	if (!local.IsEmpty()) {
		arr.AddTail(local);
	}
}

static CString PlayerYouTube(CString fn, CString* out_title)
{
	CString tmp_fn(CString(fn).MakeLower());

	if (tmp_fn.Find(YOUTUBE_URL) != -1 || tmp_fn.Find(YOUTU_BE_URL) != -1) {

		if (tmp_fn.Find(YOUTU_BE_URL) != -1) {
			fn.Replace(YOUTU_BE_FULL_URL, YOUTUBE_FULL_URL);
			fn.Replace(YOUTU_BE_URL, YOUTUBE_FULL_URL);
		}

		if (out_title) {
			*out_title = _T("");
		}

		int match_start	= 0;
		int match_len	= 0;

		char *out = NULL;
		HINTERNET f, s = InternetOpen(0, 0, 0, 0, 0);
		if (s) {
			f = InternetOpenUrl(s, fn, 0, 0, INTERNET_FLAG_TRANSFER_BINARY | INTERNET_FLAG_EXISTING_CONNECT | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
			if (f) {
				DWORD dwBytesRead	= 0;
				DWORD dataSize		= 0;

				do {
					char buffer[4096];
					if (InternetReadFile(f, (LPVOID)buffer, _countof(buffer), &dwBytesRead) == FALSE) {
						break;
					}
					char *tempData = DNew char[dataSize + dwBytesRead];
					memcpy(tempData, out, dataSize);
					memcpy(tempData + dataSize, buffer, dwBytesRead);
					delete[] out;
					out = tempData;
					dataSize += dwBytesRead;

					// optimization - to not download the entire page
					if (!match_start) {
						match_start	= strpos(out, MATCH_START);
					} else {
						match_len	= strpos(out + match_start, MATCH_END);
					};

					if (match_start && match_len) {
						match_start += strlen(MATCH_START);
						match_len	-= strlen(MATCH_START);
						break;
					}

				} while (dwBytesRead);

				InternetCloseHandle(f);
			}
			InternetCloseHandle(s);

			if (!f) {
				return fn;
			}
		} else {
			return fn;
		}

		if (!match_start || !match_len) {
			delete[] out;
			return fn;
		}

		// get name(title) for output filename
		CString Title;
		int t_start = strpos(out, "<title>");
		if (t_start > 0) {
			t_start += 7;
			int t_stop = strpos(out + t_start, "</title>");
			if (t_stop > 0) {
				char* title = DNew char[t_stop + 1];
				memset(title, 0, t_stop + 1);
				memcpy(title, out + t_start, t_stop);

				Title = UTF8To16(title);
				Title = Title.TrimLeft(_T(".")).TrimRight(_T("."));

				Title.Replace(_T(" - YouTube"), _T(""));
				Title.Replace(_T(":"), _T(" -"));
				Title.Replace(_T("|"), _T("-"));
				Title.Replace(_T("�"), _T("-"));
				Title.Replace(_T("--"), _T("-"));
				Title.Replace(_T("  "), _T(" "));

				Title.Replace(_T("&quot;"), _T("\""));
				Title.Replace(_T("&amp;"), _T("&"));
				Title.Replace(_T("&#39;"), _T("\""));

				delete [] title;
			}
		}

		if (Title.IsEmpty()) {
			Title = _T("video");
		}

		char *str1 = DNew char[match_len + 1];
		memset(str1, 0, match_len + 1);
		memcpy(str1, out + match_start, match_len);
				
		CString str = UTF8To16(UrlDecode(UrlDecode(CStringA(str1))));
		delete [] str1;
		delete [] out;

		if (str.Find(URL_DELIMETER) == -1) {
			return fn;
		}

		str.Delete(0, str.Find(URL_DELIMETER) + CString(URL_DELIMETER).GetLength());

		CAtlList<CString> sl;

		Explode(str, URL_DELIMETER, sl);

		POSITION pos = sl.GetHeadPosition();
		while (pos) {
			str = sl.GetNext(pos);

			// little fix ...
			str.Replace(_T(","), _T("&"));

			int sigStart = str.Find(_T("&sig"));
			if (sigStart <= 0) {
				continue;
			}

			int sigEnd = str.Find(_T("&"), sigStart + 1);
			if (sigEnd == -1) {
				sigEnd = str.GetLength();
			}

			CMapStringToString UrlFields;
			{
				// extract fields/params from url
				int p = str.Find(_T("videoplayback?"));
				if (p > 0) {
					CString templateStr = str.Mid(p + 14, str.GetLength() - p - 14);
					CAtlList<CString> sl;
					Explode(templateStr, sl, '&');
					POSITION pos = sl.GetHeadPosition();
					while (pos) {
						CAtlList<CString> sl2;
						Explode(sl.GetNext(pos), sl2, '=');
						CString rValue;
						if (sl2.GetCount() == 2 && !UrlFields.Lookup(sl2.GetHead(), rValue)) {
							UrlFields[sl2.GetHead()] = sl2.GetTail();
						}
					}
				} else {
					continue;
				}
			}

			/*
			CString itagValueStr;
			UrlFields.Lookup(_T("itag"), itagValueStr);
			if (itagValueStr.IsEmpty()) {
				continue;
			}

			// format of the video - http://en.wikipedia.org/wiki/YouTube#Quality_and_codecs
			int itagValue = 0;
			if (_stscanf_s(itagValueStr, _T("%d"), &itagValue) == 1) {
				// skip WebM & MP4/WebM 3D
				if (itagValue == 43
					|| itagValue == 44
					|| itagValue == 45
					|| itagValue == 46
					//
					|| itagValue == 82
					|| itagValue == 83
					|| itagValue == 84
					|| itagValue == 85
					//
					|| itagValue == 100
					|| itagValue == 101
					|| itagValue == 102) {
					continue;
				}
				switch (itagValue) {
					case 13:
					case 17:
					case 36:
						ext = _T(".3gp");
						break;
					case 5:
					case 6:
					case 34:
					case 35:
					case 120:
						ext = _T(".flv");
						break;
					default:
						ext = _T(".mp4");
				}
			}

			if (!itagValue) {
				continue;
			}
			*/

			if ((str.Find(_T("video/webm")) != -1) || (str.Find(_T("stereo3d")) != -1)) {
				continue;
			}

			CString ext;

			if (str.Find(_T("video/mp4")) != -1) {
				ext = _T(".mp4");
			} else if (str.Find(_T("video/x-flv")) != -1) {
				ext = _T(".flv");
			} else if (str.Find(_T("video/3gpp")) != -1) {
				ext = _T(".3gp");
			}

			if (ext.IsEmpty()) {
				continue;
			}

			str = str.Left(sigEnd);

			// it is necessary for the proper formation of links to the video file
			str.Replace(_T("&sig="), _T("&signature="));

			// add file name for future
			Title.Replace(ext, _T(""));
			str.Append(_T("&title="));
			str.Append(Title);//str.Append(CString(UrlEncode(UTF16To8(Title)))); // ???
			str.Append(ext);

			if (out_title) {
				*out_title = Title + ext;
			}

			return str;
		}

		return fn;
	}

	return fn;
}
