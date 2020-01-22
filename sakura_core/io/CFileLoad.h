/*!	@file
	@brief メモリバッファクラスへのファイル入力クラス

	@author Moca
	@date 2002/08/30 新規作成
*/
/*
	Copyright (C) 2002, Moca, genta
	Copyright (C) 2003, Moca, ryoji

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
#pragma once

#include <Windows.h>
#include "CStream.h" //CError_FileOpen
#include "charset/CCodeBase.h"
#include "charset/CCodePage.h"
#include "util/design_template.h"

struct SEncodingConfig;
class CCodeBase;

/*!
	文字コードを変換してデータを行単位で取得するためのクラス
	@note 明示的にFileOpenメンバを呼び出さないと使えない
		ファイルポインタを共有すると困るので、クラスのコピー禁止
*/
class CFileLoad
{
public:
	static bool IsLoadableSize(ULONGLONG size, bool ignoreLimit = false);
	static ULONGLONG GetLimitSize();
	static std::wstring GetSizeStringForHuman(ULONGLONG size); //!< 人にとって見やすいサイズ文字列を作る (例: "2 GB", "10 GB", "400 MB", "32 KB")

public:
	CFileLoad(){ _Init(); };
	CFileLoad( const SEncodingConfig& encode ) : m_pEencoding( &encode ){ _Init(); }
	~CFileLoad( void );

	//	Jul. 26, 2003 ryoji BOM引数追加
	ECodeType FileOpen( LPCWSTR, bool bBigFile, ECodeType, int, bool* pbBomExist = NULL );		// 指定文字コードでファイルをオープンする
	void FileClose( void );					// 明示的にファイルをクローズする

	//! 1行データをロードする 順アクセス用
	EConvertResult ReadLine(
		CNativeW*	pUnicodeBuffer,	//!< [out] UNICODEデータ受け取りバッファ
		CEol*		pcEol			//!< [i/o]
	);

//	未実装関数郡
//	cosnt char* ReadAtLine( int, int*, CEol* ); // 指定行目をロードする
//	cosnt wchar_t* ReadAtLineW( int, int*, CEol* ); // 指定行目をロードする(Unicode版)
//	bool ReadIgnoreLine( void ); // 1行読み飛ばす

	//! ファイルの日時を取得する
	BOOL GetFileTime( FILETIME* pftCreate, FILETIME* pftLastAccess, FILETIME* pftLastWrite ); // inline

	//	Jun. 08, 2003 Moca
	//! 開いたファイルにはBOMがあるか？
	bool IsBomExist( void ){ return m_bBomExist; }

	//! 現在の進行率を取得する(0% - 100%) 若干誤差が出る
	int GetPercent( void );

	//! ファイルサイズを取得する
	inline size_t GetFileSize( void ){ return m_nFileSize; }

	static const size_t gm_nBufSizeDef; // ロード用バッファサイズの初期値
//	static const int gm_nBufSizeMin; // ロード用バッファサイズの設定可能な最低値
	
	void SetEncodingConfig( const SEncodingConfig& Cfg ){
		m_pEencoding = &Cfg;
	}
	
	// 並列実行用コピー
	void Copy( CFileLoad& Src );
	
	// 次の行頭を検索
	size_t GetNextLineTop( size_t uPos );
	
	// buf 処理範囲を制限
	void SetBufLimit( size_t uBegin, size_t uEnd ){
		m_nReadBufOffSet	= uBegin;
		m_uBufSize			= uEnd;
	}
	
protected:
	void _Init( void );
	
	// Oct. 19, 2002 genta スペルミス修正
//	void SeekBegin( void );		// ファイルの先頭位置に移動する(BOMを考慮する)

	// GetLextLine の 文字コード考慮版
	const char* GetNextLineCharCode(const char*	pData, size_t nDataLen, size_t* pnLineLen, size_t* pnBgn, CEol* pcEol, int* pnEolLen);
	EConvertResult ReadLine_core(CNativeW* pUnicodeBuffer, CEol* pcEol);

	/* メンバオブジェクト */
	const SEncodingConfig* m_pEencoding;

//	LPWSTR	m_pszFileName;	// ファイル名
	HANDLE	m_hFile;		// ファイルハンドル
	HANDLE	m_hMap;		//!< メモリマップドファイルハンドル
	
	size_t	m_nFileSize;	// ファイルサイズ(64bit)
	size_t	m_uBufSize;		// 処理対象の buf サイズ
	int		m_nLineIndex;	// 現在ロードしている論理行(0開始)
	ECodeType	m_CharCode;		// 文字コード
	CCodeBase*	m_pCodeBase;	////
	EEncodingTrait	m_encodingTrait;
	CMemory			m_memEols[3];
	bool	m_bEolEx;		//!< CR/LF以外のEOLが有効か
	int		m_nMaxEolLen;	//!< EOLの長さ
	bool	m_bBomExist;	// ファイルのBOMが付いているか Jun. 08, 2003 Moca 
	int		m_nFlag;		// 文字コードの変換オプション
	bool	m_bCopyInstance;// 並列実行用のコピーインスタンス
	
	// 読み込みバッファ系
	const char*	m_pReadBuf;		// 読み込みバッファへのポインタ
	size_t		m_nReadBufOffSet;	// 読み込みバッファ中のオフセット(次の行頭位置)
	CMemory m_cLineBuffer;
	CNativeW m_cLineTemp;
	int		m_nReadOffset2;
	EConvertResult m_nTempResult;

	DISALLOW_COPY_AND_ASSIGN(CFileLoad);
}; // class CFileLoad

// インライン関数郡

// public
inline BOOL CFileLoad::GetFileTime( FILETIME* pftCreate, FILETIME* pftLastAccess, FILETIME* pftLastWrite ){
	return ::GetFileTime( m_hFile, pftCreate, pftLastAccess, pftLastWrite );
}

