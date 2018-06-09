/*!	@file
	@brief テキストスタック
	@author YoshiNRT
*/

#ifndef SAKURA_ENV_CTEXTSTACK_H_
#define SAKURA_ENV_CTEXTSTACK_H_

#include <list>
#include "mem/CNativeW.h"

#define TEXTSTACK_SIZE	( 32 * 1024 )	//!< テキストスタックサイズ

/*!	@brief テキストスタック
 * テキストスタックは TEXTSTACK_SIZE 分のリングバッファとして
 * 実装される．1 要素分は以下の通り．
 * - データサイズ (byte 単位)，int 3個分のサイズは含まない
 * - モードフラグ
 * - データ (int 単位に伸長される)
 * - データサイズ (byte 単位，前要素へのポインタ代わりに使用)
 * バッファ後端→バッファ前端はつながっているものとして処理する．
 * つまり 1要素はバッファ後端→バッファ前端をまたいで存在しうる．
 */
class CTextStack
{
public:
	//! コンストラクタ
	CTextStack(){
		m_nTopPtr =
		m_nEndPtr =
		m_nSize	= 0;
	}
	
	//~CTextStack()
	
	/*! @brief 引数で指定されたデータを，新たに領域確保しコピーする
	 * @param[in] cmemBuf	スタックに積むテキスト
	 * @param[in] uMode		選択モード (= m_nMode)
	 * @retval 正常終了時は true を返す．
	 */
	bool Push( CNativeW* cmemBuf, UINT uMode );
	
	/*! @brief テキストスタックから pop
	 * @param[in,out] cmemBuf	スタックに積むテキスト
	 * @param[in,out] uMode		選択モード (= m_nMode)
	 * @retval 正常終了時は true を返す．
	 */
	bool Pop( CNativeW* cmemBuf, UINT* puMode, bool bNoPop = false );
	
	//! 要素数取得
	int GetSize( void ){ return m_nSize; };
	
	enum {
		M_CHAR		= 0,	//<! 文字単位
		M_COLUMN	= 1,	//<! 矩形
		M_LINE		= 2,	//<! 行単位
	};
	
private:
	//! ポインタをサイズ分進める
	int Forward( int nPtr, int nSize ){
		if(( nPtr += nSize ) > TEXTSTACK_SIZE ) nPtr -= TEXTSTACK_SIZE;
		return nPtr;
	}
	
	//! ポインタをサイズ分戻す
	int Backward( int nPtr, int nSize ){
		if(( nPtr -= nSize ) < 0 ) nPtr += TEXTSTACK_SIZE;
		return nPtr;
	}
	
	//! サイズを UINT アライメント
	int AlignSize( int nSize ){
		return ( nSize + ( sizeof( int ) - 1 )) & ~( sizeof( int ) - 1 );
	}
	
	/*! @brief int アクセス
	 * @param[in] nPtr バイト単位ポインタ，ただし要 int アライメント
	 * @retval int への ptr
	 */
	int* GetIntPtr( int nPtr ){
		return ( int *)(( BYTE *)m_nStack + nPtr );
	}
	
	int		m_nTopPtr;	//<! 先頭ポインタ (バイト単位)
	int		m_nEndPtr;	//<! 後端ポインタ (バイト単位)
	int		m_nSize;	//<! 使用サイズ
	int		m_nStack[ TEXTSTACK_SIZE / sizeof( int )];	//<! バッファ，要 int アライメント
};

///////////////////////////////////////////////////////////////////////
#endif /* SAKURA_ENV_CTEXTSTACK_H_ */
