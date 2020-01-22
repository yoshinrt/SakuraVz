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
#include "StdAfx.h"
#include <io.h>	// _access
#include "CReadManager.h"
#include "CEditApp.h"	// CAppExitException
#include "window/CEditWnd.h"
#include "charset/CCodeMediator.h"
#include "io/CFileLoad.h"
#include "util/window.h"

/*!
	ファイルを読み込んで格納する（分割読み込みテスト版）
	@version	2.0
	@note	Windows用にコーディングしてある
	@retval	TRUE	正常読み込み
	@retval	FALSE	エラー(またはユーザによるキャンセル?)
	@date	2002/08/30 Moca 旧ReadFileを元に作成 ファイルアクセスに関する部分をCFileLoadで行う
	@date	2003/07/26 ryoji BOMの状態の取得を追加
*/
EConvertResult CReadManager::ReadFile_To_CDocLineMgr(
	CDocLineMgr*		pcDocLineMgr,	//!< [out]
	const SLoadInfo&	sLoadInfo,		//!< [in]
	SFileInfo*			pFileInfo		//!< [out]
)
{
	LPCWSTR pszPath = sLoadInfo.cFilePath.c_str();

	// 文字コード種別
	const STypeConfigMini* type = NULL;
	if( !CDocTypeManager().GetTypeConfigMini( sLoadInfo.nType, &type ) ){
		return RESULT_FAILURE;
	}
	ECodeType	eCharCode = sLoadInfo.eCharCode;
	if (CODE_AUTODETECT == eCharCode) {
		CCodeMediator cmediator( type->m_encoding );
		eCharCode = cmediator.CheckKanjiCodeOfFile( pszPath );
	}
	if (!IsValidCodeOrCPType( eCharCode )) {
		eCharCode = type->m_encoding.m_eDefaultCodetype;	// 2011.01.24 ryoji デフォルト文字コード
	}
	bool	bBom;
	if (eCharCode == type->m_encoding.m_eDefaultCodetype) {
		bBom = type->m_encoding.m_bDefaultBom;	// 2011.01.24 ryoji デフォルトBOM
	}
	else{
		bBom = CCodeTypeName( eCharCode ).IsBomDefOn();
	}
	pFileInfo->SetCodeSet( eCharCode, bBom );

	/* 既存データのクリア */
	pcDocLineMgr->DeleteAllLine();

	/* 処理中のユーザー操作を可能にする */
	if( !::BlockingHook( NULL ) ){
		return RESULT_FAILURE; //######INTERRUPT
	}

	EConvertResult eRet = RESULT_COMPLETE;
	
	UINT uMaxThreadNum = std::thread::hardware_concurrency();
	
	std::vector<CFileLoad>		cfl( uMaxThreadNum );
	std::vector<CDocLineMgr>	cDocMgr( uMaxThreadNum );
	std::vector<std::thread>	cThread;
	
	volatile bool	bBreakRead = false;
	
	try{
		cfl[ 0 ].SetEncodingConfig( type->m_encoding );
		
		bool bBigFile;
		#ifdef _WIN64
			bBigFile = true;
		#else
			bBigFile = false;
		#endif
		// ファイルを開く
		// ファイルを閉じるにはFileCloseメンバ又はデストラクタのどちらかで処理できます
		//	Jul. 28, 2003 ryoji BOMパラメータ追加
		cfl[ 0 ].FileOpen( pszPath, bBigFile, eCharCode, GetDllShareData().m_Common.m_sFile.GetAutoMIMEdecode(), &bBom );
		pFileInfo->SetBomExist( bBom );
		
		/* ファイル時刻の取得 */
		FILETIME	FileTime;
		if( cfl[ 0 ].GetFileTime( NULL, NULL, &FileTime )){
			pFileInfo->SetFileTime( FileTime );
		}
		
		auto ReadThread = [ &, this ]( int iThreadID ){
			
			// 処理開始・終了位置探索
			size_t	uBeginPos;
			if( iThreadID == 0 ){
				uBeginPos = 0;
			}else{
				uBeginPos = cfl[ iThreadID ].GetNextLineTop(
					cfl[ iThreadID ].GetFileSize() * iThreadID / uMaxThreadNum
				);
			}
			
			size_t	uEndPos;
			if( iThreadID == uMaxThreadNum - 1 ){
				uEndPos = cfl[ iThreadID ].GetFileSize();
			}else{
				uEndPos = cfl[ iThreadID ].GetNextLineTop(
					cfl[ iThreadID ].GetFileSize() * ( iThreadID + 1 ) / uMaxThreadNum
				);
			}
			
			cfl[ iThreadID ].SetBufLimit( uBeginPos, uEndPos );
			#ifdef DEBUG
				MYTRACE( L"pid:%d range:%u - %u\n", iThreadID, ( UINT )uBeginPos, ( UINT )uEndPos );
			#endif
			
			// ReadLineはファイルから 文字コード変換された1行を読み出します
			// エラー時はthrow CError_FileRead を投げます
			int				nLineNum = 0;
			CEol			cEol;
			CNativeW		cUnicodeBuffer;
			EConvertResult	eRead;
			constexpr DWORD timeInterval = 33;
			ULONGLONG nextTime = GetTickCount64() + timeInterval;
			
			while( RESULT_FAILURE != (eRead = cfl[ iThreadID ].ReadLine( &cUnicodeBuffer, &cEol ))){
				if(eRead==RESULT_LOSESOME){
					eRet = RESULT_LOSESOME;
				}
				const wchar_t*	pLine = cUnicodeBuffer.GetStringPtr();
				int		nLineLen = cUnicodeBuffer.GetStringLength();
				++nLineNum;
				cDocMgr[ iThreadID ].AddNewLine( pLine, nLineLen );
				//経過通知
				if( iThreadID == 0 ){
					ULONGLONG currTime = GetTickCount64();
					if(currTime >= nextTime){
						nextTime += timeInterval;
						NotifyProgress( cfl[ 0 ].GetPercent() / 2 );
						// 処理中のユーザー操作を可能にする
						if( !::BlockingHook( NULL )) bBreakRead = true;
					}
				}
				if( bBreakRead ) break;
			}
		};
		
		for( int iThreadID = uMaxThreadNum - 1; iThreadID >= 0; --iThreadID ){
			if( iThreadID == 0 ){
				ReadThread( iThreadID );
			}else{
				cfl[ iThreadID ].Copy( cfl[ 0 ]);	// cfl インスタンスコピー
				cThread.emplace_back( std::thread( ReadThread, iThreadID ));
			}
		}
		
		// 全スレッド終了待ち
		for( UINT u = 0; u < uMaxThreadNum - 1; ++u ){
			cThread[ u ].join();
		}
		
		if( bBreakRead ) throw CAppExitException(); //中断検出
		
		for( UINT u = 0; u < uMaxThreadNum; ++u ) pcDocLineMgr->Cat( &cDocMgr[ u ]);
		
		// 巨大ファイル判定
		pFileInfo->SetLargeFile(
			GetDllShareData().m_Common.m_sVzMode.m_nLargeFileSize &&
			GetDllShareData().m_Common.m_sVzMode.m_nLargeFileSize * ( 1024 * 1024UL ) <= cfl[ 0 ].GetFileSize()
		);
		
		// ファイルをクローズする
		cfl[ 0 ].FileClose();
	}
	catch(CAppExitException){
		//WM_QUITが発生した
		return RESULT_FAILURE;
	}
	catch( const CError_FileOpen& ex ){
		eRet = RESULT_FAILURE;
		if (ex.Reason() == CError_FileOpen::TOO_BIG) {
			// ファイルサイズが大きすぎる (32bit 版の場合は 2GB あたりが上限)
			ErrorMessage(
				CEditWnd::getInstance()->GetHwnd(),
				LS(STR_ERR_DLGDOCLM_TOOBIG),
				pszPath
			);
		}
		else if( !fexist( pszPath )){
			// ファイルがない
			ErrorMessage(
				CEditWnd::getInstance()->GetHwnd(),
				LS(STR_ERR_DLGDOCLM1),	//Mar. 24, 2001 jepro 若干修正
				pszPath
			);
		}
		else if( -1 == _waccess( pszPath, 4 )){
			// 読み込みアクセス権がない
			ErrorMessage(
				CEditWnd::getInstance()->GetHwnd(),
				LS(STR_ERR_DLGDOCLM2),
				pszPath
			 );
		}
		else{
			ErrorMessage(
				CEditWnd::getInstance()->GetHwnd(),
				LS(STR_ERR_DLGDOCLM3),
				pszPath
			 );
		}
	}
	catch( CError_FileRead ){
		eRet = RESULT_FAILURE;
		ErrorMessage(
			CEditWnd::getInstance()->GetHwnd(),
			LS(STR_ERR_DLGDOCLM4),
			pszPath
		 );
		/* 既存データのクリア */
		pcDocLineMgr->DeleteAllLine();
	} // 例外処理終わり
	
	//NotifyProgress(0);
	/* 処理中のユーザー操作を可能にする */
	if( !::BlockingHook( NULL ) ){
		return RESULT_FAILURE; //####INTERRUPT
	}

	/* 行変更状態をすべてリセット */
//	CModifyVisitor().ResetAllModifyFlag(pcDocLineMgr, 0);
	return eRet;
}

