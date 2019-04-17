/*! @file */
/*
	Copyright (C) 2008, kobake

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
#ifndef SAKURA_CSEARCHAGENT_FC5366A0_91D4_438B_80C6_DDA9791B50009_H_
#define SAKURA_CSEARCHAGENT_FC5366A0_91D4_438B_80C6_DDA9791B50009_H_

#include "_main/global.h"

class CDocLineMgr;
struct DocLineReplaceArg;
class CBregexp;

class CSearchStringPattern
{
public:
	CSearchStringPattern();
	CSearchStringPattern(HWND, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* pRegexp);
	~CSearchStringPattern();
	void Reset();
	bool SetPattern( HWND hwnd, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* pRegexp, bool bGlobal = false );
	bool GetIgnoreCase() const{ return !m_psSearchOption->bLoHiCase; }
	const SSearchOption& GetSearchOption() const{ return *m_psSearchOption; }
	CBregexp* GetRegexp() const{ return m_pRegexp; }
	void SetRegexp( CBregexp *re ){ m_pRegexp = re; }

private:
	// 外部依存
	const SSearchOption* m_psSearchOption;
	mutable CBregexp* m_pRegexp;

	DISALLOW_COPY_AND_ASSIGN(CSearchStringPattern);
};

class CSearchAgent{
public:
	
private:
	bool SearchWord1Line(
		const CSearchStringPattern& Pattern,	//!< 検索パターン
		wchar_t		*szSubject,					//!< 検索対象文字列
		int			iSubjectSize,				//!< 検索対象文字列長
		int			iStart,						//!< 検索開始位置
		CLogicRange	*pMatchRange,				//!< hit 範囲
		UINT		uOption = 0					//!< grep オプション
	);
	
	/*! 次行取得 */
	static int GetNextLine( const wchar_t *&pNextLine, void *pParam );
	
public:
	CSearchAgent(CDocLineMgr* pcDocLineMgr) : m_pcDocLineMgr(pcDocLineMgr) { }

	bool WhereCurrentWord( CLogicInt nLineNum, CLogicInt nIdx,
						   CLogicInt* pnIdxFrom, CLogicInt* pnIdxTo,
						   CNativeW* pcmcmWord, CNativeW* pcmcmWordLeft );	/* 現在位置の単語の範囲を調べる */

	bool PrevOrNextWord( CLogicInt nLineNum, CLogicInt nIdx, CLogicInt* pnColumnNew,
						 BOOL bLEFT, BOOL bStopsBothEnds );	/* 現在位置の左右の単語の先頭位置を調べる */
	
	int SearchWord(
		CLogicPoint				ptSerachBegin,	//!< 検索開始位置
		ESearchDirection		eDirection,		//!< 検索方向
		CLogicRange*			pMatchRange,	//!< [out] マッチ範囲。ロジック単位。
		const CSearchStringPattern&	pattern,	//!< 検索パターン
		UINT					uOption = 0		//!< grep オプション
	);

	void ReplaceData( DocLineReplaceArg* pArg );
	
	//! 8bit の alnum, '_' か?
	static bool IsAlnum( wchar_t c ){
		return c < 256 && ( iswalnum( c ) || c == L'_' );
	}
private:
	CDocLineMgr* m_pcDocLineMgr;
};

#endif /* SAKURA_CSEARCHAGENT_FC5366A0_91D4_438B_80C6_DDA9791B50009_H_ */
/*[EOF]*/
