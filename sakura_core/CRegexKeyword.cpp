/*!	@file
	@brief CRegexKeyword Library

	正規表現キーワードを扱う。
	BREGEXP.DLLを利用する。

	@author MIK
	@date Nov. 17, 2001
*/
/*
	Copyright (C) 2001, MIK
	Copyright (C) 2002, YAZAKI

	This source code is designed for sakura editor.
	Please contact the copyright holder to use this code for other purpose.
*/

//@@@ 2001.11.17 add start MIK

#include "StdAfx.h"
#include "CRegexKeyword.h"
#include "extmodule/CBregexp.h"
#include "types/CType.h"
#include "view/colors/EColorIndexType.h"

#if 0
#include <stdio.h>
#define	MYDBGMSG(s) \
{\
	FILE	*fp;\
	fp = fopen("debug.log", "a");\
	fprintf(fp, "%08x: %ls  BMatch(%d)=%d, Use=%d, Idx=%d\n", &m_pTypes, s, &BMatch, BMatch, m_bUseRegexKeyword, m_nTypeIndex);\
	fclose(fp);\
}
#else
#define	MYDBGMSG(a)
#endif

/*
 * パラメータ宣言
 */
#define RK_EMPTY          0      //初期状態
#define RK_CLOSE          1      //BREGEXPクローズ
#define RK_OPEN           2      //BREGEXPオープン
#define RK_ACTIVE         3      //コンパイル済み
#define RK_ERROR          9      //コンパイルエラー

#define RK_MATCH          4      //マッチする
#define RK_NOMATCH        5      //この行ではマッチしない

#define RK_SIZE           100    //最大登録可能数

//!	コンストラクタ
/*!	@brief コンストラクタ

	BREGEXP.DLL 初期化、正規表現キーワード初期化を行う。

	@date 2002.2.17 YAZAKI CShareDataのインスタンスは、CProcessにひとつあるのみ。
	@date 2007.08.12 genta 正規表現DLL指定のため引数追加
*/
CRegexKeyword::CRegexKeyword(LPCTSTR regexp_dll )
{
	InitDll( regexp_dll );	// 2007.08.12 genta 引数追加
	MYDBGMSG("CRegexKeyword")

	m_pTypes    = NULL;
	m_nTypeIndex = -1;
	m_nTypeId = -1;

	RegexKeyInit();
}

//!	デストラクタ
/*!	@brief デストラクタ

	コンパイル済みデータの破棄を行う。
*/
CRegexKeyword::~CRegexKeyword()
{
	int	i;

	MYDBGMSG("~CRegexKeyword")
	//コンパイル済みのバッファを解放する。
	for(i = 0; i < MAX_REGEX_KEYWORD; i++)
	{
		if( m_sInfo[i].pBregexp && IsAvailable() )
			delete m_sInfo[i].pBregexp;
		m_sInfo[i].pBregexp = NULL;
	}

	RegexKeyInit();

	m_nTypeIndex = -1;
	m_pTypes     = NULL;
}

//!	正規表現キーワード初期化処理
/*!	@brief 正規表現キーワード初期化

	 正規表現キーワードに関する変数類を初期化する。

	@retval TRUE 成功
*/
BOOL CRegexKeyword::RegexKeyInit( void )
{
	int	i;

	MYDBGMSG("RegexKeyInit")
	m_nTypeIndex = -1;
	m_nTypeId = -1;
	m_nCompiledMagicNumber = 1;
	m_bUseRegexKeyword = false;
	m_nRegexKeyCount = 0;
	for(i = 0; i < MAX_REGEX_KEYWORD; i++)
	{
		m_sInfo[i].pBregexp = NULL;
#ifdef USE_PARENT
#else
		m_sInfo[i].sRegexKey.m_nColorIndex = COLORIDX_REGEX1;
#endif
	}
#ifdef USE_PARENT
#else
	wmemset( m_keywordList, _countof(m_keywordList), L'\0' );
#endif

	return TRUE;
}

//!	現在タイプ設定処理
/*!	@brief 現在タイプ設定

	現在のタイプ設定を設定する。

	@param pTypesPtr [in] タイプ設定構造体へのポインタ

	@retval TRUE 成功
	@retval FALSE 失敗

	@note タイプ設定が変わったら再ロードしコンパイルする。
*/
BOOL CRegexKeyword::RegexKeySetTypes( const STypeConfig *pTypesPtr )
{
	MYDBGMSG("RegexKeySetTypes")
	if( pTypesPtr == NULL ) 
	{
		m_pTypes = NULL;
		m_bUseRegexKeyword = false;
		return FALSE;
	}

	if( !pTypesPtr->m_bUseRegexKeyword )
	{
		//OFFになったのにまだONならOFFにする。
		if( m_bUseRegexKeyword )
		{
			m_pTypes = NULL;
			m_bUseRegexKeyword = false;
		}
		return FALSE;
	}

	if( m_nTypeId              == pTypesPtr->m_id
	 && m_nCompiledMagicNumber == pTypesPtr->m_nRegexKeyMagicNumber
	 && m_pTypes != NULL  // 2014.07.02 条件追加
	){
		return TRUE;
	}

	m_pTypes = pTypesPtr;

	RegexKeyCompile();
	
	return TRUE;
}

//!	正規表現キーワードコンパイル処理
/*!	@brief 正規表現キーワードコンパイル

	正規表現キーワードをコンパイルする。

	@retval TRUE 成功
	@retval FALSE 失敗

	@note すでにコンパイル済みの場合はそれを破棄する。
	キーワードはコンパイルデータとして内部変数にコピーする。
	先頭指定、色指定側の使用・未使用をチェックする。
*/
BOOL CRegexKeyword::RegexKeyCompile( void )
{
	int	i;
	static const wchar_t dummy[2] = L"\0";
	const struct RegexKeywordInfo	*rp;

	MYDBGMSG("RegexKeyCompile")
	//コンパイル済みのバッファを解放する。
	for(i = 0; i < MAX_REGEX_KEYWORD; i++)
	{
		if( m_sInfo[i].pBregexp && IsAvailable() )
			delete m_sInfo[i].pBregexp;
		m_sInfo[i].pBregexp = NULL;
	}

	//コンパイルパターンを内部変数に移す。
	m_nRegexKeyCount = 0;
	const wchar_t * pKeyword = &m_pTypes->m_RegexKeywordList[0];
#ifdef USE_PARENT
#else
	wmemcpy( m_keywordList,  m_pTypes->m_RegexKeywordList, _countof(m_RegexKeywordList) );
#endif
	for(i = 0; i < MAX_REGEX_KEYWORD; i++)
	{
		if( pKeyword[0] == L'\0' ) break;
#ifdef USE_PARENT
#else
		m_sInfo[i].sRegexKey.m_nColorIndex = m_pTypes->m_RegexKeywordArr[i].m_nColorIndex;
#endif
		m_nRegexKeyCount++;
		for(; *pKeyword != '\0'; pKeyword++ ){}
		pKeyword++;
	}

	m_nTypeIndex = m_pTypes->m_nIdx;
	m_nTypeId = m_pTypes->m_id;
	m_nCompiledMagicNumber = 1;	//Not Compiled.
	m_bUseRegexKeyword  = m_pTypes->m_bUseRegexKeyword;
	if( !m_bUseRegexKeyword ) return FALSE;

	if( ! IsAvailable() )
	{
		m_bUseRegexKeyword = false;
		return FALSE;
	}

#ifdef USE_PARENT
	pKeyword = &m_pTypes->m_RegexKeywordList[0];
#else
	pKeyword = &m_keywordList[0];
#endif
	//パターンをコンパイルする。
	for(i = 0; i < m_nRegexKeyCount; i++)
	{
#ifdef USE_PARENT
		rp = &m_pTypes->m_RegexKeywordArr[i];
#else
		rp = &m_sInfo[i].sRegexKey;
#endif

		m_sInfo[i].pBregexp = Compile( pKeyword, &m_sInfo[i].nHead );
		
		if( m_sInfo[i].pBregexp ){	//エラーがないかチェックする
			
			if( COLORIDX_REGEX1  <= rp->m_nColorIndex
			 && COLORIDX_REGEX10 >= rp->m_nColorIndex )
			{
				//色指定でチェックが入ってなければ検索しなくてもよい
				if( m_pTypes->m_ColorInfoArr[rp->m_nColorIndex].m_bDisp )
				{
					m_sInfo[i].nFlag = RK_EMPTY;
				}
				else
				{
					//正規表現では色指定のチェックを見る。
					m_sInfo[i].nFlag = RK_NOMATCH;
				}
			}
			else
			{
				//正規表現以外では、色指定チェックは見ない。
				//例えば、半角数値は正規表現を使い、基本機能を使わないという指定もあり得るため
				m_sInfo[i].nFlag = RK_EMPTY;
			}
		}
		else
		{
			//コンパイルエラーなので検索対象からはずす
			m_sInfo[i].nFlag = RK_NOMATCH;
			delete m_sInfo[i].pBregexp;
			m_sInfo[i].pBregexp = nullptr;
		}
		
		for(; *pKeyword != '\0'; pKeyword++ ){}
		pKeyword++;
	}

	m_nCompiledMagicNumber = m_pTypes->m_nRegexKeyMagicNumber;	//Compiled.

	return TRUE;
}

//!	行検索開始処理
/*!	@brief 行検索開始

	行検索を開始する。

	@retval TRUE 成功
	@retval FALSE 失敗または検索しない指定あり

	@note それぞれの行検索の最初に実行する。
	タイプ設定等が変更されている場合はリロードする。
*/
BOOL CRegexKeyword::RegexKeyLineStart( void )
{
	int	i;

	MYDBGMSG("RegexKeyLineStart")

	//動作に必要なチェックをする。
	if( !m_bUseRegexKeyword || !IsAvailable() || m_pTypes==NULL )
	{
		return FALSE;
	}

#if 0	//RegexKeySetTypesで設定されているはずなので廃止
	//情報不一致ならマスタから取得してコンパイルする。
	if( m_nCompiledMagicNumber != m_pTypes->m_nRegexKeyMagicNumber
	 || m_nTypeIndex           != m_pTypes->m_nIdx )
	{
		RegexKeyCompile();
	}
#endif

	//検索開始のためにオフセット情報等をクリアする。
	for(i = 0; i < m_nRegexKeyCount; i++)
	{
		m_sInfo[i].nOffset = -1;
		//m_sInfo[i].nMatch  = RK_EMPTY;
		m_sInfo[i].nMatch  = m_sInfo[i].nFlag;
		m_sInfo[i].nStatus = RK_EMPTY;
	}

	return TRUE;
}

//!	正規表現検索処理
/*!	@brief 正規表現検索

	正規表現キーワードを検索する。

	@retval TRUE 一致
	@retval FALSE 不一致

	@note RegexKeyLineStart関数によって初期化されていること。
*/
BOOL CRegexKeyword::RegexIsKeyword(
	const CStringRef&	cStr,		//!< [in] 検索対象文字列
//	const wchar_t*		pLine,		//!< [in] １行のデータ
	int					nPos,		//!< [in] 検索開始オフセット
//	int					nLineLen,	//!< [in] １行の長さ
	int*				nMatchLen,	//!< [out] マッチした長さ
	int*				nMatchColor	//!< [out] マッチした色番号
)
{
	int	i;

	MYDBGMSG("RegexIsKeyword")

	//動作に必要なチェックをする。
	if( !m_bUseRegexKeyword || !IsAvailable()
#ifdef USE_PARENT
	 || m_pTypes == NULL
#endif
	 /* || ( pLine == NULL ) */ )
	{
		return FALSE;
	}

	for(i = 0; i < m_nRegexKeyCount; i++)
	{
		if( m_sInfo[i].nMatch != RK_NOMATCH )  /* この行にキーワードがないと分かっていない */
		{
			if( m_sInfo[i].nOffset == nPos )  /* 以前検索した結果に一致する */
			{
				*nMatchLen   = m_sInfo[i].nLength;
#ifdef USE_PARENT
				*nMatchColor = m_pTypes->m_RegexKeywordArr[i].m_nColorIndex;
#else
				*nMatchColor = m_sInfo[i].sRegexKey.m_nColorIndex;
#endif
				return TRUE;  /* マッチした */
			}

			/* 以前の結果はもう古いので再検索する */
			if( m_sInfo[i].nOffset < nPos )
			{
				if( m_sInfo[i].pBregexp->Match( cStr.GetPtr(), cStr.GetLength(), nPos )){
					m_sInfo[i].nOffset = m_sInfo[i].pBregexp->GetIndex();
					m_sInfo[i].nLength = m_sInfo[i].pBregexp->GetMatchLen();
					m_sInfo[i].nMatch  = RK_MATCH;
				
					/* 指定の開始位置でマッチした */
					if( m_sInfo[i].nOffset == nPos )
					{
						if( m_sInfo[i].nHead != 1 || nPos == 0 )
						{
							*nMatchLen   = m_sInfo[i].nLength;
#ifdef USE_PARENT
							*nMatchColor = m_pTypes->m_RegexKeywordArr[i].m_nColorIndex;
#else
							*nMatchColor = m_sInfo[i].sRegexKey.m_nColorIndex;
#endif
							return TRUE;  /* マッチした */
						}
					}

					/* 行先頭を要求する正規表現では次回から無視する */
					if( m_sInfo[i].nHead == 1 )
					{
						m_sInfo[i].nMatch = RK_NOMATCH;
					}
				}
				else
				{
					/* この行にこのキーワードはない */
					m_sInfo[i].nMatch = RK_NOMATCH;
				}
			}
		}
	}  /* for */

	return FALSE;
}

CBregexp *CRegexKeyword::Compile( const wchar_t *szRe, int *pHead ){
	// /re/opt の簡易パース
	// /re/opt, #re#opt, m/re/opt, m#re#opt
	// 最後の # / のみ opt の区切りと認識．
	
	// 'm' スキップ
	if( !szRe || !*szRe ) return nullptr;
	if( *szRe == L'm' ) ++szRe;
	
	// '/' or '#' チェック
	if( *szRe != L'/' && *szRe != L'#' ) return nullptr;
	
	// 後ろの '/' or '#' 検索
	const wchar_t	*pSt = szRe + 1;
	if( !*pSt || !pSt[ 1 ]) return nullptr;
	
	const wchar_t	*pEd = wcsrchr( pSt + 1, *szRe );
	
	if( !pEd ) pEd = wcsrchr( pSt + 1, L'\0' );
	
	// オプション解析，i のみ
	int iOption = wcschr( pEd, L'i' ) ? 0 : optCaseSensitive;
	
	std::wstring strRe( pSt, pEd - pSt );
	CBregexp *re = new CBregexp;
	
	// '^' チェック
	if( pHead ) *pHead = *pSt == L'^' ? 1 : 0;
	
	if( re->Compile( strRe.c_str(), iOption )){
		return re;
	}
	
	delete re;
	return nullptr;
}

BOOL CRegexKeyword::RegexKeyCheckSyntax(const wchar_t *s){
	CBregexp *re = Compile( s );
	if( re ){
		delete re;
		return true;
	}
	
	return false;
}

//@@@ 2001.11.17 add end MIK

/*static*/
DWORD CRegexKeyword::GetNewMagicNumber()
{
	return ::GetTickCount();
}

