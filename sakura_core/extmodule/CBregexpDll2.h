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
#ifndef SAKURA_CBREGEXPDLL2_850005D4_6AA3_41D2_B541_1EE730935E6B_H_
#define SAKURA_CBREGEXPDLL2_850005D4_6AA3_41D2_B541_1EE730935E6B_H_

#include "CDllHandler.h"
#include "bregexp.h"

typedef BREGEXP BREGEXP_W;

//!BREGONIG.DLLをラップしたもの。
//2007.09.13 kobake 作成
class CBregexpDll2 : public CDllImp{
public:
	CBregexpDll2();
	virtual ~CBregexpDll2();

protected:
	// CDllImpインタフェース
	virtual LPCTSTR GetDllNameImp(int nIndex); // Jul. 5, 2001 genta インターフェース変更に伴う引数追加
	virtual bool InitDllImp();

public:
	// UNICODEインターフェースを提供する
	int BMatch(const wchar_t* str, const wchar_t* target,const wchar_t* targetendp,BREGEXP_W** rxp,wchar_t* msg)
	{
		return ::BMatch(const_cast<TCHAR*>( str ),const_cast<TCHAR*>( target ),const_cast<TCHAR*>( targetendp ),rxp,msg);
	}
	int BSubst(const wchar_t* str, const wchar_t* target,const wchar_t* targetendp,BREGEXP_W** rxp,wchar_t* msg)
	{
		return ::BSubst(const_cast<TCHAR*>( str ),const_cast<TCHAR*>( target ),const_cast<TCHAR*>( targetendp ),rxp,msg);
	}
	int BTrans(const wchar_t* str, wchar_t* target,wchar_t* targetendp,BREGEXP_W** rxp,wchar_t* msg)
	{
		return ::BTrans(const_cast<TCHAR*>( str ),target,targetendp,rxp,msg);
	}
	int BSplit(const wchar_t* str, wchar_t* target,wchar_t* targetendp,int limit,BREGEXP_W** rxp,wchar_t* msg)
	{
		return ::BSplit(const_cast<TCHAR*>( str ),target,targetendp,limit,rxp,msg);
	}
	void BRegfree(BREGEXP_W* rx)
	{
		return ::BRegfree(rx);
	}
	const wchar_t* BRegexpVersion(void)
	{
		return ::BRegexpVersion();
	}
	int BMatchEx(const wchar_t* str, const wchar_t* targetbeg, const wchar_t* target, const wchar_t* targetendp, BREGEXP_W** rxp, wchar_t* msg)
	{
		return ::BMatchEx(const_cast<TCHAR*>( str ),const_cast<TCHAR*>( targetbeg ),const_cast<TCHAR*>( target ),const_cast<TCHAR*>( targetendp ),rxp,msg);
	}
	int BSubstEx(const wchar_t* str, const wchar_t* targetbeg, const wchar_t* target, const wchar_t* targetendp, BREGEXP_W** rxp, wchar_t* msg)
	{
		return ::BSubstEx(const_cast<TCHAR*>( str ),const_cast<TCHAR*>( targetbeg ),const_cast<TCHAR*>( target ),const_cast<TCHAR*>( targetendp ),rxp,msg);
	}

	// 関数があるかどうか
	bool ExistBMatchEx() const{ return true; }
	bool ExistBSubstEx() const{ return true; }
};

#endif /* SAKURA_CBREGEXPDLL2_850005D4_6AA3_41D2_B541_1EE730935E6B_H_ */
/*[EOF]*/
