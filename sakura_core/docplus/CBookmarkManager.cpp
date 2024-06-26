﻿/*! @file */
/*
	Copyright (C) 2018-2022, Sakura Editor Organization

	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

		1. The origin of this software must not be misrepresented;
		   you must not claim that you wrote the original software.
		   If you use this software in a product, an acknowledgment
		   in the product documentation would be appreciated but is
		   not required.

		2. Altered source versions must be plainly marked as such,
		   and must not be misrepresented as being the original software.

		3. This notice may not be removed or altered from any source
		   distribution.
*/
#include "StdAfx.h"
#include "docplus/CBookmarkManager.h"
#include "doc/logic/CDocLineMgr.h"
#include "doc/logic/CDocLine.h"
#include "CSearchAgent.h"
#include "extmodule/CBregexp.h"

bool CBookmarkGetter::IsBookmarked() const{ return m_pcDocLine->m_sMark.m_cBookmarked; }
void CBookmarkSetter::SetBookmark(bool bFlag){ m_pcDocLine->m_sMark.m_cBookmarked = bFlag; }

//!ブックマークの全解除
/*
	@date 2001.12.03 hor
*/
void CBookmarkManager::ResetAllBookMark( void )
{
	CDocLine* pDocLine = m_pcDocLineMgr->GetDocLineTop();
	while( pDocLine ){
		CBookmarkSetter(pDocLine).SetBookmark(false);
		pDocLine = pDocLine->GetNextLine();
	}
}

//! ブックマーク検索
/*
	@date 2001.12.03 hor
*/
bool CBookmarkManager::SearchBookMark(
	CLogicInt			nLineNum,		//!< 検索開始行
	ESearchDirection	bPrevOrNext,	//!< 検索方向
	CLogicInt*			pnLineNum 		//!< マッチ行
)
{
	CDocLine*	pDocLine;
	CLogicInt	nLinePos=nLineNum;

	// 後方検索
	if( bPrevOrNext == SEARCH_BACKWARD ){
		nLinePos--;
		pDocLine = m_pcDocLineMgr->GetLine( nLinePos );
		while( pDocLine ){
			if(CBookmarkGetter(pDocLine).IsBookmarked()){
				*pnLineNum = nLinePos;				/* マッチ行 */
				return true;
			}
			nLinePos--;
			pDocLine = pDocLine->GetPrevLine();
		}
	}
	// 前方検索
	else{
		nLinePos++;
		pDocLine = m_pcDocLineMgr->GetLine( nLinePos );
		while( NULL != pDocLine ){
			if(CBookmarkGetter(pDocLine).IsBookmarked()){
				*pnLineNum = nLinePos;				/* マッチ行 */
				return true;
			}
			nLinePos++;
			pDocLine = pDocLine->GetNextLine();
		}
	}
	return false;
}

//! 物理行番号のリストからまとめて行マーク
/*
	@date 2002.01.16 hor
	@date 2014.04.24 Moca ver2 差分32進数方式に変更
*/
void CBookmarkManager::SetBookMarks( wchar_t* pMarkLines )
{
	CDocLine*	pCDocLine;
	wchar_t *p;
	wchar_t delim[] = L", ";
	p = pMarkLines;
	if( p[0] == L':' ){
		if( p[1] == L'0' ){
			// ver2 形式 [0-9a-v] 0-31(終端バージョン) [w-zA-Z\+\-] 0-31
			// 2番目以降は、数値+1+ひとつ前の値
			// :00123x0 => 0,1,2,3,x0 => 0,(1+1),(2+2+1),(3+5+1),(32+9+1) => 0,2,5,9,42 => 1,3,6,10,43行目
			// :0a => a, => 10, 11
			p += 2;
			int nLineNum = 0;
			int nLineTemp = 0;
			while( *p != L'\0' ){
				bool bSeparete = false;
				if( L'0' <= *p && *p <= L'9' ){
					nLineTemp += (*p - L'0');
					bSeparete = true;
				}else if( L'a' <= *p && *p <= L'v' ){
					nLineTemp += (*p - L'a') + 10;
					bSeparete = true;
				}else if( L'w' <= *p && *p <= L'z' ){
					nLineTemp += (*p - L'w');
				}else if( L'A' <= *p && *p <= L'Z' ){
					nLineTemp += (*p - L'A') + 4;
				}else if( *p == L'+' ){
					nLineTemp += 30;
				}else if( *p == L'-' ){
					nLineTemp += 31;
				}else{
					break;
				}
				if( bSeparete ){
					nLineNum += nLineTemp;
					pCDocLine = m_pcDocLineMgr->GetLine( CLogicInt(nLineNum) );
					if( pCDocLine ){
						CBookmarkSetter(pCDocLine).SetBookmark(true);
					}
					nLineNum++;
					nLineTemp = 0;
				}else{
					nLineTemp *= 32;
				}
				p++;
			}
		}else{
			// 不明なバージョン
		}
	}else{
		// 旧形式 行番号,区切り
		wchar_t *context{ nullptr };
		while(wcstok_s(p, delim, &context) != NULL) {
			while(wcschr(delim, *p) != NULL)p++;
			pCDocLine=m_pcDocLineMgr->GetLine( CLogicInt(_wtol(p)) );
			if(pCDocLine)CBookmarkSetter(pCDocLine).SetBookmark(true);
			p += wcslen(p) + 1;
		}
	}
}

//! 行マークされてる物理行番号のリストを作る
/*
	@date 2002.01.16 hor
	@date 2014.04.24 Moca ver2 差分32進数方式に変更
*/
LPCWSTR CBookmarkManager::GetBookMarks()
{
	const CDocLine*	pCDocLine;
	static wchar_t szText[MAX_MARKLINES_LEN + 1];	//2002.01.17 // Feb. 17, 2003 genta staticに
	wchar_t szBuff[10];
	wchar_t szBuff2[10];
	CLogicInt	nLinePos=CLogicInt(0);
	CLogicInt	nLinePosOld=CLogicInt(-1);
	int			nTextLen = 2;
	pCDocLine = m_pcDocLineMgr->GetLine( nLinePos );
	wcscpy( szText, L":0" );
	while( pCDocLine ){
		if(CBookmarkGetter(pCDocLine).IsBookmarked()){
			CLogicInt nDiff = nLinePos - nLinePosOld - CLogicInt(1);
			nLinePosOld = nLinePos;
			if( nDiff == CLogicInt(0) ){
				szBuff2[0] = L'0';
				szBuff2[1] = L'\0';
			}else{
				int nColumn = 0;
				while( nDiff ){
					CLogicInt nKeta = nDiff % 32;
					wchar_t c;
					if( nColumn == 0 ){
						if( nKeta <= 9 ){
							c = (wchar_t)((Int)nKeta + L'0');
						}else{
							c = (wchar_t)((Int)nKeta - 10 + L'a');
						}
					}else{
						if( nKeta <= 3 ){
							c = (wchar_t)((Int)nKeta + L'w');
						}else if( nKeta <= 29 ){
							c = (wchar_t)((Int)nKeta - 4 + L'A');
						}else if( nKeta == 30 ){
							c = L'+';
						}else{ // 31
							c = L'-';
						}
					}
					szBuff[nColumn] = c;
					nColumn++;
					nDiff /= 32;
				}
				int i = 0;
				for(; i < nColumn; i++ ){
					szBuff2[i] = szBuff[nColumn-1-i];
				}
				szBuff2[nColumn] = L'\0';
			}
			int nBuff2Len = wcslen(szBuff2);
			if( nBuff2Len + nTextLen > MAX_MARKLINES_LEN ) break;	//2002.01.17
			wcscpy( szText + nTextLen, szBuff2 );
			nTextLen += nBuff2Len;
		}
		nLinePos++;
		pCDocLine = pCDocLine->GetNextLine();
	}
	return szText; // Feb. 17, 2003 genta
}

// GetNextLine 用 param
class CGetNextLineInfoBM {
public:
	CDocLine	*m_pDoc;
	CLogicInt	m_iLineNo;
	
	CGetNextLineInfoBM( CDocLine *pDoc, int iLineNo ) : m_pDoc( pDoc ), m_iLineNo( iLineNo ){};
};

int CBookmarkManager::GetNextLine( const wchar_t **ppNextLine, void *pParam ){
	CGetNextLineInfoBM& DocInfo = *reinterpret_cast<CGetNextLineInfoBM *>( pParam );
	
	// unget
	if( !ppNextLine ){
		DocInfo.m_pDoc = DocInfo.m_pDoc->GetPrevLine();	// 前行
		--DocInfo.m_iLineNo;
		return 0;
	}
	
	DocInfo.m_pDoc = DocInfo.m_pDoc->GetNextLine();		// 次行取得
	if( !DocInfo.m_pDoc ) return 0;						// 次行なし
	
	int iLen;
	*ppNextLine = ( wchar_t *)DocInfo.m_pDoc->GetDocLineStrWithEOL( &iLen );
	
	// この行が最終行?
	if( !DocInfo.m_pDoc->GetNextLine()) iLen |= CBregexp::SIZE_LAST;
	++DocInfo.m_iLineNo;
	
	return iLen;
}

//! 検索条件に該当する行にブックマークをセットする
/*
	@date 2002.01.16 hor
*/
void CBookmarkManager::MarkSearchWord(
	const CSearchStringPattern& pattern
)
{
	const SSearchOption&	sSearchOption = pattern.GetSearchOption();
	const wchar_t*	pLine;
	int			nLineLen;

	/* 1==正規表現 */
	CBregexp*	pRegexp = pattern.GetRegexp();
	CLogicInt	iLineNo( 0 );
	CDocLine	*pDoc;
	CGetNextLineInfoBM	DocInfo( pDoc = m_pcDocLineMgr->GetLine( iLineNo ), iLineNo );
	
	CLogicRange MatchRange;
	
	// 次行取得コールバック設定
	pRegexp->SetNextLineCallback( &GetNextLine, &DocInfo );
	
	while( pDoc ){
		if( !CBookmarkGetter( pDoc ).IsBookmarked()){
			pLine = pDoc->GetDocLineStrWithEOL( &nLineLen );
			
			// 検索開始行
			DocInfo.m_pDoc		= pDoc;
			DocInfo.m_iLineNo	= iLineNo;
			
			if( !pRegexp->Match( pLine, nLineLen, 0 )){
				// partial match 中に EOF に達した
				if( !DocInfo.m_pDoc ) break;
				
				// partial match で消費した次行
				pDoc	= DocInfo.m_pDoc->GetNextLine();
				iLineNo	= DocInfo.m_iLineNo + 1;
				
				continue;
			}
			
			// match レンジ取得
			pRegexp->GetMatchRange( &MatchRange, iLineNo );
			
			// 先頭行マーク
			iLineNo = MatchRange.GetFrom().y;
			pDoc = m_pcDocLineMgr->GetLine( iLineNo );
			CBookmarkSetter( pDoc ).SetBookmark( true );
		}
		
		// 検索開始行の次行
		pDoc = pDoc->GetNextLine();
		++iLineNo;
	}
}
