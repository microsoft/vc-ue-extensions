#pragma once

#include "VisualStudioDTE.h"

class FSmartBSTR
{
public:
	FSmartBSTR() : data(nullptr)
	{
	}

	FSmartBSTR(const FString &Other)
	{
		data = SysAllocString(*Other);
	}
	
	FSmartBSTR(const OLECHAR *Ptr)
	{
		data = SysAllocString(Ptr);
	}
	
	~FSmartBSTR()
	{
		if (data) SysFreeString(data);
	}
	
	BSTR operator*()
	{
		return data;
	}

private:
	BSTR data;
};