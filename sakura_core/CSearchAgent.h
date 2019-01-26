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

// #define SEARCH_STRING_KMP
#define SEARCH_STRING_SUNDAY_QUICK

class CSearchStringPattern
{
public:
	CSearchStringPattern();
	CSearchStringPattern(HWND, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* pRegexp);
	~CSearchStringPattern();
	void Reset();
	bool SetPattern(HWND hwnd, const wchar_t* pszPattern, int nPatternLen, const SSearchOption& sSearchOption, CBregexp* pRegexp){
		return SetPattern(hwnd, pszPattern, nPatternLen, NULL, sSearchOption, pRegexp, false);
	}
	bool SetPattern(HWND, const wchar_t* pszPattern, int nPatternLen, const wchar_t* pszPattern2, const SSearchOption& sSearchOption, CBregexp* pRegexp, bool bGlobal);
	const wchar_t* GetKey() const{ return m_pszKey; }
	const wchar_t* GetCaseKey() const{ return m_pszCaseKeyRef; }
	int GetLen() const{ return m_nPatternLen; }
	bool GetIgnoreCase() const{ return !m_psSearchOption->bLoHiCase; }
	bool GetLoHiCase() const{ return m_psSearchOption->bLoHiCase; }
	const SSearchOption& GetSearchOption() const{ return *m_psSearchOption; }
	CBregexp* GetRegexp() const{ return m_pRegexp; }
#ifdef SEARCH_STRING_KMP
	const int* GetKMPNextTable() const{ return m_pnNextPossArr; }
#endif
#ifdef SEARCH_STRING_SUNDAY_QUICK
	const int* GetUseCharSkipMap() const{ return m_pnUseCharSkipArr; }

	static int GetMapIndex( wchar_t c );
#endif

	UINT m_uVzWordSearch;		//!< Vz 互換ワードサーチの境界
	enum {
		WORDSEARCH_TOP	= 0x1,	//!< 単語先頭は境界
		WORDSEARCH_TAIL	= 0x2,	//!< 単語終端は境界
	};

private:
	// 外部依存
	const wchar_t*	m_pszKey;
	const SSearchOption* m_psSearchOption;
	mutable CBregexp* m_pRegexp;

	const wchar_t* m_pszCaseKeyRef;

	// 内部バッファ
	wchar_t* m_pszPatternCase;
	int  m_nPatternLen;
#ifdef SEARCH_STRING_KMP
	int* m_pnNextPossArr;
#endif
#ifdef SEARCH_STRING_SUNDAY_QUICK
	int* m_pnUseCharSkipArr;
#endif

	DISALLOW_COPY_AND_ASSIGN(CSearchStringPattern);
};

class CSearchAgent{
public:
	/*!
		@brief SearchWord1Line 用の引数クラス
	*/
	class CSearchInfo {
	public:
		CSearchInfo(){
			m_bEOF			= false;
			m_bGenerated	= false;
		};
		
		~CSearchInfo(){
			if( m_bGenerated ) delete [] m_szSubject;
		}
		
		// (r) は正規表現のみで使用
		wchar_t		*m_szSubject;	// !< 検索対象文字列
		int			m_iSize;		// !< 検索対象文字列長
		int			m_iSizeNoEOL;	// !< (r)検索対象文字列長(EOL除く)
		CLogicInt	m_iLineNo;		// !< 論理行番号
		CLogicRange	*m_pMatchRange;	// !< hit 範囲
		bool		m_bEOF;			// !< (r)m_szSubject は最終行を含む
		bool		m_bGenerated;	// !< (r)m_szSubject は new された
	};
	
	// 文字列検索
	static const wchar_t* SearchString(
		const wchar_t*	pLine,
		int				nLineLen,
		int				nIdxPos,
		const CSearchStringPattern& pattern,
		bool			bVzWordSearch = true
	);
	// 単語単位で文字列検索
	static const wchar_t* SearchStringWord(
		const wchar_t*	pLine,
		int				nLineLen,
		int				nIdxPos,
		const std::vector<std::pair<const wchar_t*, CLogicInt> >& searchWords,
		bool	bLoHiCase,
		int*	pnMatchLen
	);
	
	static void CreateWordList(
		std::vector<std::pair<const wchar_t*, CLogicInt> >&	searchWords,
		const wchar_t*	pszPattern,
		int	nPatternLen
	);
	
private:
	bool SearchWord1Line(
		const CSearchStringPattern& Pattern,	//!< 検索パターン
		CSearchInfo	&Result,
		int	iStart
	);
	
public:
	CSearchAgent(CDocLineMgr* pcDocLineMgr) : m_pcDocLineMgr(pcDocLineMgr) { }

	bool WhereCurrentWord( CLogicInt nLineNum, CLogicInt nIdx,
						   CLogicInt* pnIdxFrom, CLogicInt* pnIdxTo,
						   CNativeW* pcmcmWord, CNativeW* pcmcmWordLeft );	/* 現在位置の単語の範囲を調べる */

	bool PrevOrNextWord( CLogicInt nLineNum, CLogicInt nIdx, CLogicInt* pnColumnNew,
						 BOOL bLEFT, BOOL bStopsBothEnds );	/* 現在位置の左右の単語の先頭位置を調べる */
	//	Jun. 26, 2001 genta	正規表現ライブラリの差し替え
	int SearchWord( CLogicPoint ptSerachBegin, ESearchDirection eDirection, CLogicRange* pMatchRange, const CSearchStringPattern& pattern ); /* 単語検索 */

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
