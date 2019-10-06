﻿/*!	@file
	@brief ファイル読み込みクラス

	@author Moca
	@date 2002/08/30 新規作成
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2002, Moca, genta
	Copyright (C) 2003, Moca, ryoji
	Copyright (C) 2006, rastiv

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
#include <stdlib.h>
#include <string.h>
#include "_main/global.h"
#include "mem/CMemory.h"
#include "CEol.h"
#include "io/CFileLoad.h"
#include "charset/charcode.h"
#include "io/CIoBridge.h"
#include "charset/CCodeFactory.h" ////
#include "charset/CCodePage.h"
#include "charset/CCodeMediator.h"
#include "util/string_ex2.h"
#include "charset/CESI.h"
#include "window/CEditWnd.h"

/*
	@note Win32APIで実装
		2GB以上のファイルは開けない
*/

/*! ロード用バッファサイズの初期値 */
const size_t CFileLoad::gm_nBufSizeDef = 32768;
//(最適値がマシンによって違うのでとりあえず32KB確保する)

// /*! ロード用バッファサイズの設定可能な最低値 */
// const int gm_nBufSizeMin = 1024;

bool CFileLoad::IsLoadableSize(ULONGLONG size, bool ignoreLimit)
{
	// 上限無視
	if (ignoreLimit)return true;

	// 判定
	return size < CFileLoad::GetLimitSize();
}

ULONGLONG CFileLoad::GetLimitSize()
{
#ifdef _WIN64
	// 64bit の場合
	// 実質上限は設けないこととする (x64 対応しながらここは要検討)
	return ULLONG_MAX;
#else
	// 32bit の場合
	// だいたい 2GB くらいを上限とする (既存コードがそうなっていたのでそれを踏襲)
	return 0x80000000;
#endif
}

//! 人にとって見やすいサイズ文字列を作る (例: "2 GB", "10 GB", "400 MB", "32 KB")
//  …と言いつつ、今のところは MB 単位での表示にします。適宜必要あれば改良してください。
std::wstring CFileLoad::GetSizeStringForHuman(ULONGLONG size)
{
	// bytes to megabytes
	ULONGLONG megabytes = size / 1024 / 1024;

	// to string
	wchar_t buf[32];
	swprintf_s(buf, _countof(buf), L"%I64u", megabytes);
	std::wstring str = buf;

	// https://stackoverflow.com/questions/7276826/c-format-number-with-commas
	// コンマ区切り文字列
	int insertPosition = str.length() - 3;
	while (insertPosition > 0) {
		str.insert(insertPosition, L",");
		insertPosition -= 3;
	}

	// 単位付けて返す
	return str + L" MB";
}

/*! コンストラクタ */
void CFileLoad::_Init( void ){
	m_hFile			= NULL;
	m_nFileSize		= 0;
	m_uBufSize		= 0;
	m_CharCode		= CODE_DEFAULT;
	m_pCodeBase		= NULL;////
	m_encodingTrait = ENCODING_TRAIT_ASCII;
	m_bBomExist		= false;	// Jun. 08, 2003 Moca
	m_nFlag 		= 0;
	m_hMap			= nullptr;
	m_bCopyInstance	= false;

	m_nLineIndex	= -1;

	m_pReadBuf = NULL;
	m_nReadBufOffSet  = 0;
}

// ReadLine の並列処理用に FileOpen 後のインスタンスをコピーする
void CFileLoad::Copy( CFileLoad& Src ){
	m_pEencoding	= Src.m_pEencoding;
	
	m_hFile			= nullptr;
	m_nFileSize		= Src.m_nFileSize;
	m_uBufSize		= Src.m_nFileSize;
	m_CharCode		= Src.m_CharCode;
	m_pCodeBase		= Src.m_pCodeBase;
	m_encodingTrait = Src.m_encodingTrait;
	m_bBomExist		= Src.m_bBomExist;
	m_nFlag 		= Src.m_nFlag;
	m_hMap			= nullptr;
	m_bCopyInstance	= true;
	
	m_nLineIndex	= -1;
	
	m_pReadBuf		= Src.m_pReadBuf;
	m_nReadBufOffSet  = 0;
}

/*! デストラクタ */
CFileLoad::~CFileLoad( void ){
	FileClose();
}

/*!
	ファイルを開く
	@param pFileName [in] ファイル名
	@param bBigFile  [in] 2GB以上のファイルを開くか。Grep=true, 32bit版はその他=falseで運用
	@param CharCode  [in] ファイルの文字コード．
	@param nFlag [in] 文字コードのオプション
	@param pbBomExist [out] BOMの有無
	@date 2003.06.08 Moca CODE_AUTODETECTを指定できるように変更
	@date 2003.07.26 ryoji BOM引数追加
*/
ECodeType CFileLoad::FileOpen( LPCWSTR pFileName, bool bBigFile, ECodeType CharCode, int nFlag, bool* pbBomExist )
{
	HANDLE	hFile;
	ULARGE_INTEGER	fileSize;
	ECodeType	nBomCode;

	// FileCloseを呼んでからにしてください
	if( NULL != m_hFile ){
#ifdef _DEBUG
		::MessageBox( NULL, L"CFileLoad::FileOpen\nFileCloseを呼んでからにしてください" , NULL, MB_OK );
#endif
		throw CError_FileOpen();
	}
	hFile = ::CreateFile(
		pFileName,
		GENERIC_READ,
		//	Oct. 18, 2002 genta FILE_SHARE_WRITE 追加
		//	他プロセスが書き込み中のファイルを開けるように
		FILE_SHARE_READ | FILE_SHARE_WRITE,	// 共有
		NULL,						// セキュリティ記述子
		OPEN_EXISTING,				// 作成方法
		FILE_FLAG_SEQUENTIAL_SCAN,	// ファイル属性
		NULL						// テンプレートファイルのハンドル
	);
	if( hFile == INVALID_HANDLE_VALUE ){
		throw CError_FileOpen();
	}
	m_hFile = hFile;

	// GetFileSizeEx は Win2K以上
	fileSize.LowPart = ::GetFileSize( hFile, &fileSize.HighPart );
	if( 0xFFFFFFFF == fileSize.LowPart ){
		DWORD lastError = ::GetLastError();
		if( NO_ERROR != lastError ){
			FileClose();
			throw CError_FileOpen();
		}
	}
	if (!CFileLoad::IsLoadableSize(fileSize.QuadPart, bBigFile)) {
		// ファイルが大きすぎる(2GB位)
		FileClose();
		throw CError_FileOpen(CError_FileOpen::TOO_BIG);
	}
	m_nFileSize = m_uBufSize = ( size_t )fileSize.QuadPart;
	
	if( m_nFileSize ){
		if(
			!( m_hMap = CreateFileMapping( hFile, nullptr, PAGE_READONLY, 0, 0, nullptr )) ||
			!( m_pReadBuf = ( const char *)MapViewOfFile( m_hMap, FILE_MAP_READ, 0, 0, 0 ))
		){
			FileClose();
			throw CError_FileOpen();
		}
	}
	
	// 文字コード判定

	nBomCode = CCodeMediator::DetectUnicodeBom( m_pReadBuf, ( int )t_min( m_nFileSize, gm_nBufSizeDef ));
	if( CharCode == CODE_AUTODETECT ){
		if( nBomCode != CODE_NONE ){
			CharCode = nBomCode;
		}else{
			CCodeMediator mediator(*m_pEencoding);
			CharCode = mediator.CheckKanjiCode( m_pReadBuf, ( int )t_min( m_nFileSize, gm_nBufSizeDef ));
		}
	}
	// 不正な文字コードのときはデフォルト(SJIS:無変換)を設定
	if( !IsValidCodeOrCPType(CharCode) ){
		CharCode = CODE_DEFAULT;
	}
	m_CharCode = CharCode;
	m_pCodeBase=CCodeFactory::CreateCodeBase(m_CharCode, m_nFlag);
	m_encodingTrait = CCodePage::GetEncodingTrait(m_CharCode);
	m_nFlag = nFlag;

	bool bBom = false;
	if( 0 < m_nFileSize ){
		CMemory headData(m_pReadBuf, ( int )t_min( m_nFileSize, ( size_t )10 ));
		CNativeW headUni;
		CIoBridge::FileToImpl(headData, &headUni, m_pCodeBase, m_nFlag);
		if( 1 <= headUni.GetStringLength() && headUni.GetStringPtr()[0] == 0xfeff ){
			bBom = true;
		}
	}
	if( bBom ){
		//	Jul. 26, 2003 ryoji BOMの有無をパラメータで返す
		m_bBomExist = true;
		if( pbBomExist != NULL ){
			*pbBomExist = true;
		}
	}else{
		//	Jul. 26, 2003 ryoji BOMの有無をパラメータで返す
		if( pbBomExist != NULL ){
			*pbBomExist = false;
		}
	}
	
	// To Here Jun. 13, 2003 Moca BOMの除去
//	m_cmemLine.AllocBuffer( 256 );
	m_pCodeBase->GetEol( &m_memEols[0], EOL_NEL );
	m_pCodeBase->GetEol( &m_memEols[1], EOL_LS );
	m_pCodeBase->GetEol( &m_memEols[2], EOL_PS );
	bool bEolEx = false;
	int  nMaxEolLen = 0;
	for( int k = 0; k < (int)_countof(m_memEols); k++ ){
		if( 0 != m_memEols[k].GetRawLength() ){
			bEolEx = true;
			nMaxEolLen = t_max(nMaxEolLen, m_memEols[k].GetRawLength());
		}
	}
	m_bEolEx = bEolEx;
	m_nMaxEolLen = nMaxEolLen;
	if(	false == GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol ){
		m_bEolEx = false;
	}

	m_nReadOffset2 = 0;
	m_nTempResult = RESULT_FAILURE;
	m_cLineTemp.SetString(L"");
	return m_CharCode;
}

/*!
	ファイルを閉じる
	読み込み用バッファとm_memLineもクリアされる
*/
void CFileLoad::FileClose( void )
{
	if( m_pReadBuf ){
		if( !m_bCopyInstance ) UnmapViewOfFile( m_pReadBuf );
		m_pReadBuf = nullptr;
	}
	if( nullptr != m_hMap ){
		if( !m_bCopyInstance ) CloseHandle( m_hMap );
		m_hMap = nullptr;
	}
	if( NULL != m_hFile ){
		if( !m_bCopyInstance ) ::CloseHandle( m_hFile );
		m_hFile = NULL;
	}
	if( NULL != m_pCodeBase ){
		if( !m_bCopyInstance ) delete m_pCodeBase;
		m_pCodeBase = NULL;
	}
	m_nReadBufOffSet	= 0;
	
	m_nFileSize		=  0;
	m_uBufSize		=  0;
	m_CharCode		= CODE_DEFAULT;
	m_bBomExist		= false; // From Here Jun. 08, 2003
	m_nFlag 		=  0;
	m_nLineIndex	= -1;
}

/*! 1行読み込み
	UTF-7場合、データ内のNEL,PS,LS等の改行までを1行として取り出す
*/
EConvertResult CFileLoad::ReadLine( CNativeW* pUnicodeBuffer, CEol* pcEol )
{
	if( m_CharCode != CODE_UTF7 && m_CharCode != CP_UTF7 ){
		return ReadLine_core( pUnicodeBuffer, pcEol );
	}
	if( m_nReadOffset2 == m_cLineTemp.GetStringLength() ){
		CEol cEol;
		EConvertResult e = ReadLine_core( &m_cLineTemp, &cEol );
		if( e == RESULT_FAILURE ){
			pUnicodeBuffer->_GetMemory()->SetRawDataHoldBuffer( L"", 0 );
			*pcEol = cEol;
			return RESULT_FAILURE;
		}
		m_nReadOffset2 = 0;
		m_nTempResult = e;
	}
	int  nOffsetTemp = m_nReadOffset2;
	int  nRetLineLen;
	CEol cEolTemp;
	const wchar_t* pRet = GetNextLineW( m_cLineTemp.GetStringPtr(), m_cLineTemp.GetStringLength(),
				&nRetLineLen, &m_nReadOffset2, &cEolTemp, GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol );
	if( m_cLineTemp.GetStringLength() == m_nReadOffset2 && nOffsetTemp == 0 ){
		// 途中に改行がない限りは、swapを使って中身のコピーを省略する
		pUnicodeBuffer->swap(m_cLineTemp);
		if( 0 < m_cLineTemp.GetStringLength() ){
			m_cLineTemp._GetMemory()->SetRawDataHoldBuffer( L"", 0 );
		}
		m_nReadOffset2 = 0;
	}else{
		// 改行が途中にあった。必要分をコピー
		pUnicodeBuffer->_GetMemory()->SetRawDataHoldBuffer( L"", 0 );
		pUnicodeBuffer->AppendString( pRet, nRetLineLen + cEolTemp.GetLen() );
	}
	*pcEol = cEolTemp;
	return m_nTempResult;
}

/*!
	次の論理行を文字コード変換してロードする
	順次アクセス専用
	GetNextLineのような動作をする
	@return	NULL以外	1行を保持しているデータの先頭アドレスを返す。永続的ではない一時的な領域。
			NULL		データがなかった
*/
EConvertResult CFileLoad::ReadLine_core(
	CNativeW*	pUnicodeBuffer,	//!< [out] UNICODEデータ受け取りバッファ。改行も含めて読み取る。
	CEol*		pcEol			//!< [i/o]
)
{
	EConvertResult eRet = RESULT_COMPLETE;

	//行データバッファ (文字コード変換無しの生のデータ)
	m_cLineBuffer.SetRawDataHoldBuffer("",0);

	// 1行取り出し ReadBuf -> m_memLine
	//	Oct. 19, 2002 genta while条件を整理
	size_t		nBufLineLen;
	int			nEolLen;
	
	const char* pLine = GetNextLineCharCode(
		m_pReadBuf,
		m_uBufSize,			//[in] バッファの有効データサイズ
		&nBufLineLen,		//[out]改行を含まない長さ
		&m_nReadBufOffSet,	//[i/o]オフセット
		pcEol,
		&nEolLen
	);
	
	if( pLine ) m_cLineBuffer.AppendRawData( pLine, nBufLineLen + nEolLen );

	// 文字コード変換 cLineBuffer -> pUnicodeBuffer
	EConvertResult eConvertResult = CIoBridge::FileToImpl(m_cLineBuffer,pUnicodeBuffer,m_pCodeBase,m_nFlag);
	if(eConvertResult==RESULT_LOSESOME){
		eRet = RESULT_LOSESOME;
	}

	m_nLineIndex++;

	// 2012.10.21 Moca BOMの除去(UTF-7対応)
	if( m_nLineIndex == 0 ){
		if( m_bBomExist && 1 <= pUnicodeBuffer->GetStringLength() ){
			if( pUnicodeBuffer->GetStringPtr()[0] == 0xfeff ){
				CNativeW tmp(pUnicodeBuffer->GetStringPtr() + 1, pUnicodeBuffer->GetStringLength() - 1);
				*pUnicodeBuffer = tmp;
			}
		}
	}
	if( 0 == pUnicodeBuffer->GetStringLength() ){
		eRet = RESULT_FAILURE;
	}

	return eRet;
}

/*!
	 現在の進行率を取得する
	 @return 0% - 100%  若干誤差が出る
*/
int CFileLoad::GetPercent( void ){
	int nRet;
	if( 0 == m_uBufSize || m_nReadBufOffSet > m_uBufSize ){
		nRet = 100;
	}else{
		nRet = static_cast<int>(m_nReadBufOffSet * 100 / m_uBufSize);
	}
	return nRet;
}

/*!
	GetNextLineの汎用文字コード版
*/
const char* CFileLoad::GetNextLineCharCode(
	const char*	pData,		//!< [in]	検索文字列
	size_t		nDataLen,	//!< [in]	検索文字列のバイト数
	size_t*		pnLineLen,	//!< [out]	1行のバイト数を返すただしEOLは含まない
	size_t*		pnBgn,		//!< [i/o]	検索文字列のバイト単位のオフセット位置
	CEol*		pcEol,		//!< [i/o]	EOL
	int*		pnEolLen	//!< [out]	EOLのバイト数 (Unicodeで困らないように)
){
	size_t nbgn = *pnBgn;
	size_t i;

	pcEol->SetType( EOL_NONE );

	if( nDataLen <= nbgn ){
		*pnLineLen = 0;
		*pnEolLen = 0;
		return NULL;
	}
	const unsigned char* pUData = (const unsigned char*)pData; // signedだと符号拡張でNELがおかしくなるので
	bool bExtEol = GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol;
	size_t nLen = nDataLen;
	int neollen = 0;
	switch( m_encodingTrait ){
	case ENCODING_TRAIT_ERROR://
	case ENCODING_TRAIT_ASCII:
		{
			static const EEolType eEolEx[] = {
				EOL_NEL,
				EOL_LS,
				EOL_PS,
			};
			nLen = nDataLen;
			for( i = nbgn; i < nDataLen; ++i ){
				if( pData[i] == '\r' || pData[i] == '\n' ){
					pcEol->SetTypeByStringForFile( &pData[i], nDataLen - i );
					neollen = pcEol->GetLen();
					break;
				}
				if( m_bEolEx ){
					int k;
					for( k = 0; k < (int)_countof(eEolEx); k++ ){
						if( 0 != m_memEols[k].GetRawLength() && i + m_memEols[k].GetRawLength() - 1 < nDataLen
								&& 0 == memcmp( m_memEols[k].GetRawPtr(), pData + i, m_memEols[k].GetRawLength()) ){
							pcEol->SetType(eEolEx[k]);
							neollen = m_memEols[k].GetRawLength();
							break;
						}
					}
					if( k != (int)_countof(eEolEx) ){
						break;
					}
				}
			}
			// UTF-8のNEL,PS,LS断片の検出
			if( i == nDataLen && m_bEolEx ){
				for( i = t_max(( size_t )0, nDataLen - m_nMaxEolLen - 1 ); i < nDataLen; i++ ){
					int k;
					bool bSet = false;
					for( k = 0; k < (int)_countof(eEolEx); k++ ){
						size_t nCompLen = t_min( nDataLen - i, ( size_t )m_memEols[k].GetRawLength());
						if( 0 != nCompLen && 0 == memcmp(m_memEols[k].GetRawPtr(), pData + i, nCompLen) ){
							bSet = true;
						}
					}
					if( bSet ){
						break;
					}
				}
				i = nDataLen;
			}
		}
		break;
	case ENCODING_TRAIT_UTF16LE:
		nLen = nDataLen - 1;
		for( i = nbgn; i < nLen; i += 2 ){
			wchar_t c = static_cast<wchar_t>((pUData[i + 1] << 8) | pUData[i]);
			if( WCODE::IsLineDelimiter(c, bExtEol) ){
				pcEol->SetTypeByStringForFile_uni( &pData[i], nDataLen - i );
				neollen = (Int)pcEol->GetLen() * sizeof(wchar_t);
				break;
			}
		}
		break;
	case ENCODING_TRAIT_UTF16BE:
		nLen = nDataLen - 1;
		for( i = nbgn; i < nLen; i += 2 ){
			wchar_t c = static_cast<wchar_t>((pUData[i] << 8) | pUData[i + 1]);
			if( WCODE::IsLineDelimiter(c, bExtEol) ){
				pcEol->SetTypeByStringForFile_unibe( &pData[i], nDataLen - i );
				neollen = (Int)pcEol->GetLen() * sizeof(wchar_t);
				break;
			}
		}
		break;
	case ENCODING_TRAIT_UTF32LE:
		nLen = nDataLen - 3;
		for( i = nbgn; i < nLen; i += 4 ){
			wchar_t c = static_cast<wchar_t>((pUData[i+1] << 8) | pUData[i]);
			if( pUData[i+3] == 0x00 && pUData[i+2] == 0x00 && WCODE::IsLineDelimiter(c, bExtEol) ){
				wchar_t c2;
				int eolTempLen;
				if( i + 4 < nLen && pUData[i+7] == 0x00 && pUData[i+6] == 0x00 ){
					c2 = static_cast<wchar_t>((pUData[i+5] << 8) | pUData[i+4]);
					eolTempLen = 2 * sizeof(wchar_t);
				}else{
					c2 = 0x0000;
					eolTempLen = 1 * sizeof(wchar_t);
				}
				wchar_t pDataTmp[2] = {c, c2};
				pcEol->SetTypeByStringForFile_uni( reinterpret_cast<char *>(pDataTmp), eolTempLen );
				neollen = (Int)pcEol->GetLen() * 4;
				break;
			}
		}
		break;
	case ENCODING_TRAIT_UTF32BE:
		nLen = nDataLen - 3;
		for( i = nbgn; i < nLen; i += 4 ){
			wchar_t c = static_cast<wchar_t>((pUData[i+2] << 8) | pUData[i+3]);
			if( pUData[i] == 0x00 && pUData[i+1] == 0x00 && WCODE::IsLineDelimiter(c, bExtEol) ){
				wchar_t c2;
				int eolTempLen;
				if( i + 4 < nLen && pUData[i+4] == 0x00 && pUData[i+5] == 0x00 ){
					c2 = static_cast<wchar_t>((pUData[i+6] << 8) | pUData[i+7]);
					eolTempLen = 2 * sizeof(wchar_t);
				}else{
					c2 = 0x0000;
					eolTempLen = 1 * sizeof(wchar_t);
				}
				wchar_t pDataTmp[2] = {c, c2};
				pcEol->SetTypeByStringForFile_uni( reinterpret_cast<char *>(pDataTmp), eolTempLen );
				neollen = (Int)pcEol->GetLen() * 4;
				break;
			}
		}
		break;
	case ENCODING_TRAIT_EBCDIC_CRLF:
	case ENCODING_TRAIT_EBCDIC:
		// EOLコード変換しつつ設定
		for( i = nbgn; i < nDataLen; ++i ){
			if( m_encodingTrait == ENCODING_TRAIT_EBCDIC && bExtEol ){
				if( pData[i] == '\x15' ){
					pcEol->SetType(EOL_NEL);
					neollen = 1;
					break;
				}
			}
			if( pData[i] == '\x0d' || pData[i] == '\x25' ){
				char szEof[3] = {
					(pData[i]  == '\x25' ? '\x0a' : '\x0d'),
					(pData[i+1]== '\x25' ? '\x0a' : (char)
						(pData[i+1] == '\x0a' ? 0 : // EBCDIC の"\x0aがLFにならないように細工する
							(i + 1 < nDataLen ? pData[i+1] : 0))),
					0
				};
				pcEol->SetTypeByStringForFile( szEof, t_min(( int )( nDataLen - i ), 2 ));
				neollen = (Int)pcEol->GetLen();
				break;
			}
		}
		break;
	}

	if( neollen < 1 ){
		// EOLがなかった場合
		if( i != nDataLen ){
			i = nDataLen;		// 最後の半端なバイトを落とさないように
		}
	}

	*pnBgn = i + neollen;
	*pnLineLen = i - nbgn;
	*pnEolLen = neollen;

	return &pData[nbgn];
}

// 指定位置以降の行頭を検索
size_t CFileLoad::GetNextLineTop( size_t uPos ){
	size_t	nLineLen;	// 不使用
	CEol	cEol;		// 不使用
	int		nEolLen;	// 不使用
	
	if( !uPos ) return 0;
	if( uPos == m_uBufSize ) return uPos;
	
	--uPos;
	
	GetNextLineCharCode( m_pReadBuf, m_uBufSize, &nLineLen, &uPos, &cEol, &nEolLen );
	return uPos;
}

